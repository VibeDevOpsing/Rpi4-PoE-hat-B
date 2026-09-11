#include "ssd1306.h"
#include "font5x7.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <linux/i2c-dev.h>
#else
#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif
#endif

// Internal 512-byte display buffer (128 columns x 4 pages)
static uint8_t s_buffer[SSD1306_BUFFER_SIZE];

// 4 rotating fan animation frames (16x16 pixels, 32 bytes per frame)
static const uint8_t s_fan_frames[4][32] = {
    // Frame 0 (0 deg)
    {
        0x03, 0xC0, 0x0D, 0x70, 0x11, 0x68, 0x21, 0xC4,
        0x41, 0xC2, 0x60, 0x82, 0xFB, 0xC1, 0x9F, 0xC1,
        0xFF, 0xFF, 0x83, 0xDF, 0x41, 0x86, 0x43, 0x82,
        0x23, 0x84, 0x16, 0x88, 0x0E, 0xB0, 0x03, 0xC0
    },
    // Frame 1 (22.5 deg)
    {
        0x03, 0xC0, 0x0C, 0x30, 0x10, 0x38, 0x30, 0x6C,
        0x68, 0x72, 0x7C, 0xE2, 0x9F, 0xC1, 0x87, 0xC1,
        0x83, 0xE1, 0x83, 0xF9, 0x47, 0x3E, 0x4E, 0x16,
        0x36, 0x0C, 0x1C, 0x08, 0x0C, 0x30, 0x03, 0xC0
    },
    // Frame 2 (45 deg)
    {
        0x03, 0xC0, 0x0C, 0x30, 0x1C, 0x08, 0x34, 0x0C,
        0x4E, 0x16, 0x46, 0x3E, 0x83, 0xF1, 0x83, 0xC1,
        0x83, 0xC1, 0x8F, 0xC1, 0x7C, 0x62, 0x68, 0x72,
        0x30, 0x2C, 0x10, 0x38, 0x0C, 0x30, 0x03, 0xC0
    },
    // Frame 3 (67.5 deg)
    {
        0x03, 0xC0, 0x0F, 0x30, 0x15, 0x08, 0x23, 0x04,
        0x43, 0x02, 0x43, 0x06, 0x83, 0xFB, 0x83, 0xFF,
        0xFF, 0xC1, 0xDF, 0xC1, 0x60, 0xC2, 0x40, 0xC2,
        0x20, 0xC4, 0x10, 0xA8, 0x0C, 0xF0, 0x03, 0xC0
    }
};

const uint8_t *ssd1306_get_fan_frame(int frame_idx) {
    if (frame_idx < 0) frame_idx = 0;
    return s_fan_frames[frame_idx % 4];
}

static int ssd1306_send_cmd(int fd, uint8_t cmd) {
    if (fd < 0) return 0;
    if (ioctl(fd, I2C_SLAVE, SSD1306_I2C_ADDR) < 0) return -1;
    uint8_t buf[2] = {0x00, cmd};
    return (write(fd, buf, 2) == 2) ? 0 : -1;
}

int ssd1306_init(int fd) {
    if (fd < 0) return 0;
    static const uint8_t init_cmds[] = {
        0xAE,       // Display OFF
        0x40,       // Set start line 0
        0xB0,       // Set page address 0
        0xC8,       // COM output scan direction reversed
        0x81, 0xFF, // Set contrast (maximum)
        0xA1,       // Set segment re-map
        0xA6,       // Normal display (non-inverted)
        0xA8, 0x1F, // Multiplex ratio (32 rows = 0x1F)
        0xD3, 0x00, // Display offset 0
        0xD5, 0xF0, // Display clock divide ratio / osc freq
        0xD9, 0x22, // Pre-charge period
        0xDA, 0x02, // COM pins hardware config
        0xDB, 0x49, // VCOMH deselect level
        0x8D, 0x14, // Enable charge pump
        0xAF        // Display ON
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        if (ssd1306_send_cmd(fd, init_cmds[i]) < 0) {
            return -1;
        }
    }
    ssd1306_clear();
    ssd1306_flush(fd);
    return 0;
}

int ssd1306_display_off(int fd) {
    if (fd < 0) return 0;
    ssd1306_clear();
    ssd1306_flush(fd);
    return ssd1306_send_cmd(fd, 0xAE);
}

void ssd1306_clear(void) {
    memset(s_buffer, 0x00, sizeof(s_buffer));
}

void ssd1306_draw_pixel(int x, int y, int color) {
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }
    int page = y / 8;
    int bit = y % 8;
    int index = page * SSD1306_WIDTH + x;

    if (color) {
        s_buffer[index] |= (1 << bit);
    } else {
        s_buffer[index] &= ~(1 << bit);
    }
}

void ssd1306_draw_line_v(int x, int y0, int y1, int color) {
    if (y0 > y1) {
        int t = y0; y0 = y1; y1 = t;
    }
    for (int y = y0; y <= y1; y++) {
        ssd1306_draw_pixel(x, y, color);
    }
}

void ssd1306_draw_line_h(int y, int x0, int x1, int color) {
    if (x0 > x1) {
        int t = x0; x0 = x1; x1 = t;
    }
    for (int x = x0; x <= x1; x++) {
        ssd1306_draw_pixel(x, y, color);
    }
}

void ssd1306_draw_rect(int x, int y, int w, int h, int color) {
    if (w <= 0 || h <= 0) return;
    ssd1306_draw_line_h(y, x, x + w - 1, color);
    ssd1306_draw_line_h(y + h - 1, x, x + w - 1, color);
    ssd1306_draw_line_v(x, y, y + h - 1, color);
    ssd1306_draw_line_v(x + w - 1, y, y + h - 1, color);
}

void ssd1306_fill_rect(int x, int y, int w, int h, int color) {
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < h; i++) {
        ssd1306_draw_line_h(y + i, x, x + w - 1, color);
    }
}

void ssd1306_draw_char(int x, int y, char c, int color) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t *glyph = font5x7[c - 32];

    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                ssd1306_draw_pixel(x + col, y + row, color);
            } else {
                ssd1306_draw_pixel(x + col, y + row, !color);
            }
        }
        // 8th row blank
        ssd1306_draw_pixel(x + col, y + 7, !color);
    }
    // 6th column is 1px spacing
    for (int row = 0; row < 8; row++) {
        ssd1306_draw_pixel(x + 5, y + row, !color);
    }
}

void ssd1306_draw_string(int x, int y, const char *str, int color) {
    if (!str) return;
    int cur_x = x;
    while (*str && cur_x + 5 < SSD1306_WIDTH) {
        ssd1306_draw_char(cur_x, y, *str, color);
        cur_x += 6; // 5px width + 1px spacing
        str++;
    }
}

void ssd1306_draw_progress_bar(int x, int y, int w, int h, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // Draw outline
    ssd1306_draw_rect(x, y, w, h, 1);

    // Inner area
    int inner_w = w - 2;
    int inner_h = h - 2;
    int fill_w = (inner_w * percent) / 100;

    if (fill_w > 0) {
        ssd1306_fill_rect(x + 1, y + 1, fill_w, inner_h, 1);
    }
    if (inner_w - fill_w > 0) {
        ssd1306_fill_rect(x + 1 + fill_w, y + 1, inner_w - fill_w, inner_h, 0);
    }
}

void ssd1306_draw_bitmap_16x16(int x, int y, const uint8_t *bmp) {
    if (!bmp) return;
    for (int r = 0; r < 16; r++) {
        uint16_t row_bits = ((uint16_t)bmp[r * 2] << 8) | (uint16_t)bmp[r * 2 + 1];
        for (int c = 0; c < 16; c++) {
            int color = (row_bits & (0x8000 >> c)) ? 1 : 0;
            ssd1306_draw_pixel(x + c, y + r, color);
        }
    }
}

int ssd1306_flush(int fd) {
    if (fd < 0) return 0; // Mock mode
    if (ioctl(fd, I2C_SLAVE, SSD1306_I2C_ADDR) < 0) return -1;

    uint8_t chunk[129];
    chunk[0] = 0x40; // Data stream prefix

    for (int page = 0; page < SSD1306_PAGES; page++) {
        uint8_t cmd[3];
        cmd[0] = 0x00;
        cmd[1] = 0xB0 + page; // Set page address
        write(fd, cmd, 2);

        cmd[1] = 0x00;        // Low col 0
        write(fd, cmd, 2);

        cmd[1] = 0x10;        // High col 0
        write(fd, cmd, 2);

        memcpy(&chunk[1], &s_buffer[page * SSD1306_WIDTH], SSD1306_WIDTH);
        if (write(fd, chunk, 129) != 129) {
            return -1;
        }
    }
    return 0;
}

void ssd1306_render_ascii_preview(void) {
    printf("+--------------------------------------------------------------------------------------------------------------------------------+\n");
    // Render 32 rows using 2-row half blocks for terminal (16 console lines)
    for (int y = 0; y < SSD1306_HEIGHT; y += 2) {
        printf("|");
        for (int x = 0; x < SSD1306_WIDTH; x++) {
            int top = (s_buffer[(y / 8) * SSD1306_WIDTH + x] >> (y % 8)) & 1;
            int bot = (s_buffer[((y + 1) / 8) * SSD1306_WIDTH + x] >> ((y + 1) % 8)) & 1;
            if (top && bot) {
                printf("#");
            } else if (top) {
                printf("^");
            } else if (bot) {
                printf(".");
            } else {
                printf(" ");
            }
        }
        printf("|\n");
    }
    printf("+--------------------------------------------------------------------------------------------------------------------------------+\n");
}
