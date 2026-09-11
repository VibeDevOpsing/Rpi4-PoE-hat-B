#!/usr/bin/env bash
# ==============================================================================
# Interactive Installer for Waveshare PoE HAT (B) Ultra-Fast C Daemon
# ==============================================================================

set -e

# ANSI Color Codes
BOLD="\033[1m"
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
BLUE="\033[0;34m"
RED="\033[0;31m"
CYAN="\033[0;36m"
NC="\033[0m" # No Color

# Helper printing functions
info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

ask_yes_no() {
    local prompt="$1"
    local default="$2"
    local choice

    if [ "$default" = "Y" ]; then
        prompt="$prompt [Y/n]: "
    else
        prompt="$prompt [y/N]: "
    fi

    while true; do
        read -r -p "$(echo -e "${BOLD}${prompt}${NC}")" choice
        choice=${choice:-$default}
        case "$choice" in
            [Yy]* ) return 0 ;;
            [Nn]* ) return 1 ;;
            * ) echo "Please answer yes (y) or no (n)." ;;
        esac
    done
}

# Ensure script is run with sudo or has sudo privileges
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
        info "Running with non-root user. Sudo privileges will be used when required."
    else
        error "This installer requires root privileges or sudo to be installed. Please run as root."
        exit 1
    fi
fi

clear 2>/dev/null || true
echo -e "${CYAN}${BOLD}"
echo "=================================================================="
echo "    Waveshare PoE HAT (B) — Interactive Setup & Daemon Installer  "
echo "=================================================================="
echo -e "${NC}"
echo "This installer will:"
echo "  1. Install compilation & I2C tools (build-essential, i2c-tools)"
echo "  2. Verify and enable the I2C interface (/dev/i2c-1)"
echo "  3. Scan I2C bus for the PoE HAT (PCF8574, SSD1306, HDC1080)"
echo "  4. Compile the ultra-fast C daemon with predictive cooling"
echo "  5. Set up and start the systemd service (auto-run on boot)"
echo ""

if ! ask_yes_no "Do you want to proceed with the installation?" "Y"; then
    echo "Installation aborted by user."
    exit 0
fi

echo ""

# ------------------------------------------------------------------------------
# Step 1: Install System Dependencies
# ------------------------------------------------------------------------------
info "Step 1/5: Checking and installing system dependencies..."

PACKAGES_TO_INSTALL=()
for pkg in build-essential i2c-tools make; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        PACKAGES_TO_INSTALL+=("$pkg")
    fi
done

if [ ${#PACKAGES_TO_INSTALL[@]} -gt 0 ]; then
    info "Installing required packages: ${PACKAGES_TO_INSTALL[*]}..."
    $SUDO apt-get update
    $SUDO apt-get install -y "${PACKAGES_TO_INSTALL[@]}"
    success "Dependencies installed."
else
    success "All build and system dependencies are already installed."
fi

# ------------------------------------------------------------------------------
# Step 2: Configure and Enable I2C Interface
# ------------------------------------------------------------------------------
info "Step 2/5: Checking I2C bus interface..."

I2C_ENABLED=false
if [ -e "/dev/i2c-1" ]; then
    I2C_ENABLED=true
    success "I2C bus device /dev/i2c-1 is active."
else
    warn "/dev/i2c-1 was not found on your system."
    if ask_yes_no "Would you like to automatically enable I2C in boot configuration?" "Y"; then
        # Check modern Debian / Raspberry Pi OS firmware config path
        CONFIG_PATH=""
        if [ -f "/boot/firmware/config.txt" ]; then
            CONFIG_PATH="/boot/firmware/config.txt"
        elif [ -f "/boot/config.txt" ]; then
            CONFIG_PATH="/boot/config.txt"
        fi

        if [ -n "$CONFIG_PATH" ]; then
            if ! grep -q "^dtparam=i2c_arm=on" "$CONFIG_PATH"; then
                echo "dtparam=i2c_arm=on" | $SUDO tee -a "$CONFIG_PATH" >/dev/null
                success "Added 'dtparam=i2c_arm=on' to $CONFIG_PATH."
            fi
        fi

        # Load kernel module immediately
        $SUDO modprobe i2c-dev 2>/dev/null || true
        if [ -e "/dev/i2c-1" ]; then
            I2C_ENABLED=true
            success "I2C kernel module loaded successfully (/dev/i2c-1 is now active)."
        else
            warn "I2C enabled in config.txt, but a system reboot may be required before /dev/i2c-1 becomes active."
        fi
    fi
fi

# Add current user to i2c group if needed
if [ -n "$SUDO_USER" ]; then
    if ! id -nG "$SUDO_USER" | grep -qw "i2c"; then
        $SUDO usermod -aG i2c "$SUDO_USER" 2>/dev/null || true
        info "Added user '$SUDO_USER' to 'i2c' group."
    fi
elif [ "$(id -u)" -ne 0 ]; then
    if ! id -nG "$USER" | grep -qw "i2c"; then
        $SUDO usermod -aG i2c "$USER" 2>/dev/null || true
        info "Added user '$USER' to 'i2c' group."
    fi
fi

# ------------------------------------------------------------------------------
# Step 3: Scan I2C Devices
# ------------------------------------------------------------------------------
info "Step 3/5: Checking PoE HAT I2C devices..."

if [ -e "/dev/i2c-1" ]; then
    SCAN_OUTPUT=$($SUDO i2cdetect -y 1 2>/dev/null || true)
    
    # Check PCF8574 (0x20)
    if echo "$SCAN_OUTPUT" | grep -q " 20 "; then
        success "Found PCF8574 Fan Controller at 0x20."
    else
        warn "Address 0x20 (Fan controller) not detected on I2C bus 1."
        echo -e "${YELLOW}--> Please check the physical slide switch on the PoE HAT board.${NC}"
        echo -e "${YELLOW}--> It must be set to 'P0' (Program Control), not 'EN'.${NC}"
    fi

    # Check SSD1306 (0x3c)
    if echo "$SCAN_OUTPUT" | grep -q " 3c "; then
        success "Found SSD1306 OLED Display at 0x3C."
    else
        warn "Address 0x3c (OLED Display) not detected. Check HAT connector pins."
    fi

    # Check HDC1080 (0x40)
    if echo "$SCAN_OUTPUT" | grep -q " 40 "; then
        success "Found HDC1080 Temperature/Humidity sensor at 0x40."
    else
        info "Address 0x40 (HDC1080 sensor) not detected (will fall back to system uptime display)."
    fi
else
    warn "Skipping I2C scan (/dev/i2c-1 is not yet active)."
fi

# ------------------------------------------------------------------------------
# Step 4: Compile the C Daemon
# ------------------------------------------------------------------------------
info "Step 4/5: Compiling poe_daemon binary..."

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

make clean
make

if [ -f "poe_daemon" ]; then
    success "Compiled 'poe_daemon' successfully."
else
    error "Compilation failed. Please check build logs."
    exit 1
fi

# ------------------------------------------------------------------------------
# Step 5: Install & Start Systemd Service
# ------------------------------------------------------------------------------
info "Step 5/5: Installing binary and configuring systemd service..."

$SUDO make install

if ask_yes_no "Do you want to enable and start 'rpi4-poe-hat.service' now?" "Y"; then
    $SUDO systemctl enable --now rpi4-poe-hat.service
    success "Service 'rpi4-poe-hat.service' started and enabled on boot!"
    echo ""
    $SUDO systemctl status rpi4-poe-hat.service --no-pager || true
fi

echo ""
echo -e "${GREEN}${BOLD}==================================================================${NC}"
echo -e "${GREEN}${BOLD}                Installation Completed Successfully!              ${NC}"
echo -e "${GREEN}${BOLD}==================================================================${NC}"
echo ""
echo "Useful Commands:"
echo "  - View service status:   sudo systemctl status rpi4-poe-hat.service"
echo "  - View live daemon logs: journalctl -u rpi4-poe-hat.service -f"
echo "  - Restart service:       sudo systemctl restart rpi4-poe-hat.service"
echo "  - Stop service:          sudo systemctl stop rpi4-poe-hat.service"
echo "  - Uninstall:             sudo make uninstall"
echo ""
if [ "$I2C_ENABLED" = false ]; then
    warn "A reboot is recommended to activate the I2C interface:"
    echo "  sudo reboot"
fi
