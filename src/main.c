#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#include "ssd1306.h"
#include "hdc1080.h"
#include "poe_hat.h"

// Configuration parameters
typedef struct {
    float temp_high;      // Fan ON threshold (°C)
    float temp_low;       // Fan OFF threshold (°C)
    float temp_load;      // Fan ON when under high load (°C)
    float load_thresh;    // High CPU load threshold (%)
    int cooldown_sec;     // Minimum running time once activated (sec)
    int mock_mode;        // Simulation mode
    int daemon_mode;      // Fork into background
    const char *i2c_dev;  // I2C device path
} config_t;

static volatile sig_atomic_t s_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    s_running = 0;
}

// -----------------------------------------------------------------------------
// System Metrics Collectors
// -----------------------------------------------------------------------------

static float get_cpu_temp(int mock) {
    if (mock) {
        // Return simulated temperature around 52°C
        return 52.4f;
    }
    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!fp) return 0.0f;
    long millideg = 0;
    if (fscanf(fp, "%ld", &millideg) == 1) {
        fclose(fp);
        return (float)millideg / 1000.0f;
    }
    fclose(fp);
    return 0.0f;
}

static float get_cpu_load(int mock) {
    if (mock) {
        return 42.0f;
    }
    static unsigned long long prev_active = 0, prev_total = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0f;

    unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal;
    if (fscanf(fp, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal) < 4) {
        fclose(fp);
        return 0.0f;
    }
    fclose(fp);

    unsigned long long active = user + nice + sys + irq + softirq + steal;
    unsigned long long total = active + idle + iowait;

    if (prev_total == 0) {
        prev_active = active;
        prev_total = total;
        return 0.0f;
    }

    unsigned long long delta_active = active - prev_active;
    unsigned long long delta_total = total - prev_total;

    prev_active = active;
    prev_total = total;

    if (delta_total == 0) return 0.0f;

    float load = (float)delta_active / (float)delta_total * 100.0f;
    if (load < 0.0f) load = 0.0f;
    if (load > 100.0f) load = 100.0f;
    return load;
}

static int get_ram_usage(int *used_mb, int *total_mb, int *percent, int mock) {
    if (mock) {
        *used_mb = 1420;
        *total_mb = 3906;
        *percent = 36;
        return 0;
    }
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1;

    char line[128];
    long total_kb = 0, avail_kb = 0, free_kb = 0, buffers_kb = 0, cached_kb = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %ld kB", &total_kb) == 1) continue;
        if (sscanf(line, "MemAvailable: %ld kB", &avail_kb) == 1) continue;
        if (sscanf(line, "MemFree: %ld kB", &free_kb) == 1) continue;
        if (sscanf(line, "Buffers: %ld kB", &buffers_kb) == 1) continue;
        if (sscanf(line, "Cached: %ld kB", &cached_kb) == 1) continue;
    }
    fclose(fp);

    if (total_kb <= 0) return -1;
    if (avail_kb <= 0) {
        avail_kb = free_kb + buffers_kb + cached_kb;
    }

    long used_kb = total_kb - avail_kb;
    *used_mb = (int)(used_kb / 1024);
    *total_mb = (int)(total_kb / 1024);
    *percent = (int)((used_kb * 100) / total_kb);
    return 0;
}

static void get_ip_address(char *ip_buf, size_t buf_len, int mock) {
    if (mock) {
        snprintf(ip_buf, buf_len, "192.168.1.105");
        return;
    }
    strncpy(ip_buf, "Disconnect", buf_len - 1);
    ip_buf[buf_len - 1] = '\0';

    // 1. Try UDP connect trick to primary gateway (non-blocking, doesn't send packets)
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s >= 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);

        if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            struct sockaddr_in local;
            socklen_t len = sizeof(local);
            if (getsockname(s, (struct sockaddr*)&local, &len) == 0) {
                inet_ntop(AF_INET, &local.sin_addr, ip_buf, buf_len);
                close(s);
                return;
            }
        }
        close(s);
    }

    // 2. Fallback: inspect interface addresses (eth0, wlan0, end0)
    struct ifaddrs *ifaddr = NULL, *ifa = NULL;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;
            if (!(ifa->ifa_flags & IFF_UP)) continue;

            struct sockaddr_in *pAddr = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &pAddr->sin_addr, ip_buf, buf_len);
            break;
        }
        freeifaddrs(ifaddr);
    }
}

static void get_uptime_str(char *buf, size_t len, int mock) {
    if (mock) {
        snprintf(buf, len, "UP: 3d 14h 20m");
        return;
    }
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) {
        snprintf(buf, len, "UP: N/A");
        return;
    }
    double sec = 0;
    if (fscanf(fp, "%lf", &sec) == 1) {
        int days = (int)(sec / 86400);
        int hours = (int)((sec - days * 86400) / 3600);
        int mins = (int)((sec - days * 86400 - hours * 3600) / 60);
        if (days > 0) {
            snprintf(buf, len, "UP: %dd %02dh %02dm", days, hours, mins);
        } else {
            snprintf(buf, len, "UP: %02dh %02dm", hours, mins);
        }
    } else {
        snprintf(buf, len, "UP: N/A");
    }
    fclose(fp);
}

// -----------------------------------------------------------------------------
// Display Rendering
// -----------------------------------------------------------------------------

static void render_dashboard(float cpu_temp, float cpu_load,
                             int ram_used, int ram_total, int ram_pct,
                             const char *ip_str,
                             int hdc_valid, float hdc_t, float hdc_h,
                             const char *uptime_str,
                             int fan_active, int anim_frame) {
    ssd1306_clear();

    // 1. Vertical separator line between dashboard and fan widget
    ssd1306_draw_line_v(104, 0, 31, 1);

    // 2. Right Pane: Fan Widget (x = 106..127)
    // Label "FAN"
    ssd1306_draw_string(107, 0, "FAN", 1);

    // 16x16 Animated Fan Propeller
    const uint8_t *frame_bmp = ssd1306_get_fan_frame(anim_frame);
    ssd1306_draw_bitmap_16x16(108, 8, frame_bmp);

    // Status: [ON] (inverted highlight) or OFF
    if (fan_active) {
        ssd1306_fill_rect(106, 24, 21, 8, 1);
        ssd1306_draw_string(110, 24, "ON", 0); // Black text on white box
    } else {
        ssd1306_draw_string(106, 24, "OFF", 1);
    }

    // 3. Left Pane: Metrics Dashboard (x = 0..103)
    char line[32];

    // Line 0 (y = 0): IP Address
    snprintf(line, sizeof(line), "IP:%s", ip_str);
    ssd1306_draw_string(0, 0, line, 1);

    // Line 1 (y = 8): CPU Temp + Progress Bar + Load %
    snprintf(line, sizeof(line), "CPU:%2.0fC", cpu_temp);
    ssd1306_draw_string(0, 8, line, 1);

    // Graphic progress bar at x = 46, y = 9, w = 32, h = 6
    ssd1306_draw_progress_bar(46, 9, 32, 6, (int)cpu_load);

    // Load percentage at x = 80
    snprintf(line, sizeof(line), "%3.0f%%", cpu_load);
    ssd1306_draw_string(80, 8, line, 1);

    // Line 2 (y = 16): RAM Usage
    if (ram_total >= 1024) {
        snprintf(line, sizeof(line), "RAM:%1.1fG %d%%", (float)ram_used / 1024.0f, ram_pct);
    } else {
        snprintf(line, sizeof(line), "RAM:%dM %d%%", ram_used, ram_pct);
    }
    ssd1306_draw_string(0, 16, line, 1);

    // Line 3 (y = 24): Ambient Sensor (HDC1080) or System Uptime
    if (hdc_valid) {
        snprintf(line, sizeof(line), "ENV:%2.1fC %2.0f%%", hdc_t, hdc_h);
    } else {
        snprintf(line, sizeof(line), "%s", uptime_str);
    }
    ssd1306_draw_string(0, 24, line, 1);
}

// -----------------------------------------------------------------------------
// CLI and Main Loop
// -----------------------------------------------------------------------------

static void print_usage(const char *prog) {
    printf("Waveshare PoE HAT (B) Ultra-Fast C Daemon\n\n");
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -t, --temp-high <deg>    Temperature threshold to turn fan ON (default: 56.0 C)\n");
    printf("  -l, --temp-low  <deg>    Temperature threshold to turn fan OFF (default: 46.0 C)\n");
    printf("  -p, --temp-load <deg>    Temperature threshold for high load trigger (default: 50.0 C)\n");
    printf("  -u, --load-thresh <pct>  CPU load threshold for predictive trigger (default: 65.0 %%)\n");
    printf("  -c, --cooldown <sec>     Minimum fan running duration in seconds (default: 25)\n");
    printf("  -d, --daemon             Run in background as system daemon\n");
    printf("  -m, --mock               Mock mode (simulate without hardware I2C)\n");
    printf("  -i, --i2c-dev <path>     I2C device path (default: /dev/i2c-1)\n");
    printf("  -h, --help               Display this help message\n");
}

int main(int argc, char *argv[]) {
    config_t cfg = {
        .temp_high = 56.0f,
        .temp_low = 46.0f,
        .temp_load = 50.0f,
        .load_thresh = 65.0f,
        .cooldown_sec = 25,
        .mock_mode = 0,
        .daemon_mode = 0,
        .i2c_dev = "/dev/i2c-1"
    };

    static struct option long_options[] = {
        {"temp-high",   required_argument, 0, 't'},
        {"temp-low",    required_argument, 0, 'l'},
        {"temp-load",   required_argument, 0, 'p'},
        {"load-thresh", required_argument, 0, 'u'},
        {"cooldown",    required_argument, 0, 'c'},
        {"daemon",      no_argument,       0, 'd'},
        {"mock",        no_argument,       0, 'm'},
        {"i2c-dev",     required_argument, 0, 'i'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "t:l:p:u:c:dmi:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 't': cfg.temp_high = atof(optarg); break;
            case 'l': cfg.temp_low = atof(optarg); break;
            case 'p': cfg.temp_load = atof(optarg); break;
            case 'u': cfg.load_thresh = atof(optarg); break;
            case 'c': cfg.cooldown_sec = atoi(optarg); break;
            case 'd': cfg.daemon_mode = 1; break;
            case 'm': cfg.mock_mode = 1; break;
            case 'i': cfg.i2c_dev = optarg; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

#ifndef __linux__
    cfg.mock_mode = 1; // Always mock on non-Linux platforms
#endif

    // Setup signal handlers for clean exit
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (cfg.daemon_mode && !cfg.mock_mode) {
        if (daemon(0, 0) < 0) {
            perror("daemon fork failed");
            return 1;
        }
    }

    int i2c_fd = -1;
    if (!cfg.mock_mode) {
        i2c_fd = poe_hat_open_i2c(cfg.i2c_dev);
        if (i2c_fd < 0) {
            fprintf(stderr, "Warning: Could not open %s, falling back to mock mode.\n", cfg.i2c_dev);
            cfg.mock_mode = 1;
        }
    }

    // Initialize display & sensors
    if (i2c_fd >= 0) {
        ssd1306_init(i2c_fd);
    }
    int hdc_available = 0;
    if (i2c_fd >= 0) {
        hdc_available = (hdc1080_init(i2c_fd) == 0);
    }

    int fan_active = 0;
    time_t fan_started_time = 0;
    int anim_frame = 0;
    unsigned long tick_count = 0;

    char ip_str[32] = "Searching...";
    char uptime_str[32] = "";
    float cpu_temp = 0.0f;
    float cpu_load = 0.0f;
    int ram_used = 0, ram_total = 0, ram_pct = 0;
    float hdc_temp = 0.0f, hdc_hum = 0.0f;

    // Initial query
    get_ip_address(ip_str, sizeof(ip_str), cfg.mock_mode);
    get_uptime_str(uptime_str, sizeof(uptime_str), cfg.mock_mode);

    // Main event loop: ticks every 125ms (~8 FPS)
    while (s_running) {
        time_t now = time(NULL);

        // Every 8 ticks (~1 second): update system metrics and evaluate fan rules
        if (tick_count % 8 == 0) {
            cpu_temp = get_cpu_temp(cfg.mock_mode);
            cpu_load = get_cpu_load(cfg.mock_mode);
            get_ram_usage(&ram_used, &ram_total, &ram_pct, cfg.mock_mode);

            if (hdc_available) {
                if (hdc1080_read(i2c_fd, &hdc_temp, &hdc_hum) < 0) {
                    hdc_available = 0; // Disable if sensor disconnected
                }
            }

            // -------------------------------------------------------------
            // Smart Fan Decision Engine
            // -------------------------------------------------------------
            if (!fan_active) {
                bool trigger_temp = (cpu_temp >= cfg.temp_high);
                bool trigger_load = (cpu_temp >= cfg.temp_load && cpu_load >= cfg.load_thresh);

                if (trigger_temp || trigger_load) {
                    fan_active = 1;
                    fan_started_time = now;
                    poe_hat_fan_set(i2c_fd, 1);
                }
            } else {
                int elapsed = (int)(now - fan_started_time);
                if (elapsed >= cfg.cooldown_sec) {
                    if (cpu_temp < cfg.temp_low && cpu_load < cfg.load_thresh) {
                        fan_active = 0;
                        poe_hat_fan_set(i2c_fd, 0);
                    }
                }
            }
        }

        // Every 80 ticks (~10 seconds): refresh IP address and Uptime
        if (tick_count % 80 == 0) {
            get_ip_address(ip_str, sizeof(ip_str), cfg.mock_mode);
            get_uptime_str(uptime_str, sizeof(uptime_str), cfg.mock_mode);
        }

        // Advance animation frame if fan is running
        if (fan_active) {
            anim_frame = (anim_frame + 1) % 4;
        } else {
            anim_frame = 0; // Static propeller when idle
        }

        // Render dashboard
        render_dashboard(cpu_temp, cpu_load,
                         ram_used, ram_total, ram_pct,
                         ip_str,
                         hdc_available, hdc_temp, hdc_hum,
                         uptime_str,
                         fan_active, anim_frame);

        // In mock mode without daemon: render console preview
        if (cfg.mock_mode && !cfg.daemon_mode && (tick_count % 8 == 0 || fan_active)) {
            printf("\033[H\033[J"); // Clear terminal screen
            printf("--- Waveshare PoE HAT (B) Simulation (Ctrl+C to quit) ---\n");
            printf("Fan Status: %s | Temp: %.1f C | Load: %.1f %%\n",
                   fan_active ? "RUNNING (ACTIVE)" : "IDLE (OFF)", cpu_temp, cpu_load);
            ssd1306_render_ascii_preview();
        }

        // Flush buffer to physical SSD1306 over I2C
        if (i2c_fd >= 0) {
            ssd1306_flush(i2c_fd);
        }

        usleep(125000); // 125ms = 8 FPS
        tick_count++;

        // For mock testing in non-daemon mode, limit iterations if needed
        if (cfg.mock_mode && !cfg.daemon_mode && tick_count >= 16) {
            break; // Sample run completed
        }
    }

    // Graceful cleanup
    if (i2c_fd >= 0) {
        poe_hat_fan_set(i2c_fd, 0);
        ssd1306_display_off(i2c_fd);
        close(i2c_fd);
    }

    return 0;
}
