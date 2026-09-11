# Waveshare PoE HAT (B) — Ultra-Fast C Daemon & OLED Dashboard

A high-performance, ultra-lightweight C daemon for the **Waveshare PoE HAT (B)** on **Raspberry Pi 4**.

It provides **intelligent predictive cooling** based on both CPU temperature and load, paired with a compact, high-refresh **128×32 OLED dashboard** featuring a smooth, live-animated cooling turbine.

---

## Why This Project? (C Daemon vs. Stock Python Scripts)

| Feature | Stock Python Script | This C Daemon |
| :--- | :--- | :--- |
| **CPU Overhead** | ~2.0% – 5.0% continuous CPU usage | **< 0.1%** (virtually zero) |
| **Memory Footprint** | ~35 MB – 50 MB RAM | **< 1.8 MB** RAM |
| **Cooling Logic** | Naive fixed threshold (45°C), noisy on/off clicking | **Predictive Hybrid Engine** (Temp + CPU Load + Hysteresis + Cooldown) |
| **Fan Sound** | Constantly roars or cycles repeatedly | **Whisper-quiet in idle**, pro-active under heavy load |
| **OLED Display** | Sluggish refresh, flickering due to full screen clearing | **Zero-flicker**, direct 512B framebuffer flush, graphical progress bar |
| **Visuals** | Basic static text | Metric dashboard + **16×16 animated 4-frame rotating fan sprite** |
| **Process Management** | Background command (`&`), dies if shell terminates | Production-ready **systemd service** with auto-restart on boot |

---

## OLED Dashboard Layout (128×32 pixels)

The screen is split into a metrics pane and a dedicated animated hardware widget:

```text
+-------------------------------------------------------------+-------+
| IP: 192.168.1.105                                           |  FAN  |
| CPU: 52C  [■■■■■■■■□□□□]  45%                               |  [@]  | <- Animated spinning fan
| RAM: 1.2G  30%                                              | [ON]  | <- Inverted status badge
| ENV: 23.5C  48%   (or UP: 3d 14h if sensor absent)          |       |
+-------------------------------------------------------------+-------+
  <-------------------- Left Pane (104px) ------------------->  <22px>
```

- **Line 0:** Current IPv4 address (Ethernet or Wi-Fi) for seamless headless SSH access.
- **Line 1:** CPU SoC temperature + **real-time graphical progress bar** + load percentage.
- **Line 2:** System RAM consumption (`Used / Total` and percentage).
- **Line 3:** Ambient temperature and relative humidity from the onboard **HDC1080** sensor (gracefully falls back to system **Uptime** if sensor is not present).
- **Right Pane:** Vertical divider, `FAN` label, a **16×16 4-frame smooth spinning turbine animation** that rotates at 8 FPS while cooling, and an inverted `[ON]` / `OFF` indicator.

---

## How It Works

### 1. Hardware Architecture & I2C Bus (`/dev/i2c-1`)
The daemon communicates directly with the I2C peripherals using native Linux `ioctl` calls without third-party library overhead:
- **`0x20` (PCF8574 I/O Expander):** Controls the fan on/off state via pin **P0** (pull-down to turn fan ON, pull-up to turn fan OFF).
- **`0x3C` (SSD1306 OLED):** 128×32 monochrome display updated via an in-memory 512-byte page buffer.
- **`0x40` (HDC1080 Sensor):** Precision digital temperature and humidity sensor.

> ⚠️ **IMPORTANT HARDWARE SWITCH:**  
> Verify that the physical slide switch on the PoE HAT board is set to **`P0`** (Program Control), **not** `EN` (Always On). If set to `EN`, the fan is hardwired to 5V and software control has no effect.

### 2. Predictive Cooling Algorithm
Traditional fan scripts only monitor temperature. Because heat takes several seconds to conduct from the SoC silicon into the heatsink, waiting for temperature to spike can cause thermal throttling.

This daemon uses a **hybrid predictive model**:
1. **High Temperature Trigger:** If $T \ge 56^\circ\text{C}$, fan turns **ON** immediately.
2. **Predictive Load Trigger:** If CPU Load $\ge 65\%$ AND $T \ge 50^\circ\text{C}$, the fan starts **pro-actively** before the heatsink heats up.
3. **Hysteresis:** The fan only turns **OFF** when temperature cools down below $46^\circ\text{C}$ and CPU load drops.
4. **Anti-Cycling Cooldown Timer:** Once activated, the fan runs for a **minimum of 25 seconds**. This prevents the constant on/off "clicking" and revving that wears out bearings and produces annoying acoustic noise.

---

## Codebase Organization

```text
.
├── Makefile                # Automated compilation and systemd installation
├── rpi4-poe-hat.service     # Systemd service unit definition
├── src/
│   ├── main.c              # Main daemon loop, metric gatherer, decision engine
│   ├── ssd1306.h / .c      # OLED SSD1306 driver, framebuffer & graphics primitives
│   ├── font5x7.h           # Ultra-compact 5x7 ASCII bitmap font (475 bytes)
│   ├── hdc1080.h / .c      # HDC1080 temperature & humidity sensor reader
│   └── poe_hat.h / .c      # PCF8574 fan controller and I2C bus abstraction
├── waveshare_POE_HAT_B/    # Legacy Waveshare Python modules (retained for reference)
└── main.py                 # Legacy Python entry point
```

---

## Installation & Setup on Raspberry Pi

You can install and run the daemon using either the **interactive installer script**, the **one-command Make setup**, or via manual steps.

### Method 1: Interactive Installer (Recommended)
An interactive script that checks dependencies, configures I2C, scans hardware, compiles the binary, and enables the service:

```bash
git clone https://github.com/VibeDevOpsing/Rpi4-PoE-hat-B.git
cd Rpi4-PoE-hat-B
chmod +x install.sh
./install.sh
```

---

### Method 2: One-Command Make Setup
Automates dependency installation via `apt`, enables I2C, builds the binary, installs the systemd service, and starts it:

```bash
git clone https://github.com/VibeDevOpsing/Rpi4-PoE-hat-B.git
cd Rpi4-PoE-hat-B
sudo make setup
```

---

### Method 3: Step-by-Step Manual Setup

1. **Install Dependencies:**
   ```bash
   sudo make deps
   ```
2. **Build the C Daemon:**
   ```bash
   make
   ```
3. **Install and Enable System Service:**
   ```bash
   sudo make install
   sudo make service-start
   ```

---

### Service Management Commands
```bash
# Check service status:
sudo make service-status
# or: sudo systemctl status rpi4-poe-hat.service

# View live daemon logs:
journalctl -u rpi4-poe-hat.service -f

# Restart service:
sudo make service-restart

# Stop service:
sudo make service-stop

# Uninstall and remove daemon completely:
sudo make uninstall
```

---

## Command-Line Options

You can test or run the daemon manually with custom temperature and load thresholds:

```bash
./poe_daemon [options]
```

| Option | Flag | Default | Description |
| :--- | :--- | :--- | :--- |
| `--temp-high` | `-t <deg>` | `56.0` | Temperature (°C) threshold to turn fan ON |
| `--temp-low` | `-l <deg>` | `46.0` | Temperature (°C) hysteresis threshold to turn fan OFF |
| `--temp-load` | `-p <deg>` | `50.0` | Minimum temperature (°C) required for load-based trigger |
| `--load-thresh` | `-u <pct>` | `65.0` | CPU load (%) required for predictive trigger |
| `--cooldown` | `-c <sec>` | `25` | Minimum runtime (seconds) once fan starts |
| `--daemon` | `-d` | `off` | Detach and run as background daemon |
| `--mock` | `-m` | `off` | Simulation mode (renders ASCII dashboard in console without hardware) |
| `--i2c-dev` | `-i <dev>` | `/dev/i2c-1` | Path to I2C device node |
| `--help` | `-h` | - | Display help menu |

**Example of custom aggressive cooling:**
```bash
# Turns fan on at 52°C, off at 42°C, or whenever CPU load exceeds 50%
./poe_daemon --temp-high 52 --temp-low 42 --temp-load 45 --load-thresh 50
```

---

## Python Alternative (Modernized)

A modernized, dependency-light Python version is also included for environments where a Python runtime is preferred.

### Key Python Improvements:
- **Zero Heavy Dependencies**: Completely removed `numpy` and `RPi.GPIO`.
- **Universal I2C Fallback**: Uses `smbus2` -> `smbus` -> **pure-Python `fcntl` fallback** (works on modern Debian 12 / Bookworm PEP 668 environments even without `pip` or C-extensions installed).
- **Graceful Fault Tolerance**: Does not crash if HDC1080 is disconnected, and handles offline network status gracefully.
- **Predictive Cooling & No Screen Flicker**: Cached fonts, no `ClearWhite()` flashes, and fan cooldown timer.

```bash
# Run with defaults (target 54°C, 2s interval):
python3 main.py

# Run with custom parameters:
python3 main.py --temp 52 --interval 1.5 --cooldown 25
```

---

## License

This project is licensed under the GPLv3 License — see the [LICENSE](LICENSE) file for details.