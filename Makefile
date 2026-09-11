CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu99 -Isrc
LDFLAGS ?= 

TARGET = poe_daemon
SRCS = src/main.c src/ssd1306.c src/hdc1080.c src/poe_hat.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ------------------------------------------------------------------------------
# Dependency & Environment Setup
# ------------------------------------------------------------------------------
deps:
	@echo "==> Installing system dependencies via apt-get..."
	sudo apt-get update
	sudo apt-get install -y build-essential i2c-tools make

enable-i2c:
	@echo "==> Enabling I2C bus interface..."
	@sudo modprobe i2c-dev 2>/dev/null || true
	@if [ -f /boot/firmware/config.txt ]; then \
		if ! grep -q "^dtparam=i2c_arm=on" /boot/firmware/config.txt; then \
			echo "dtparam=i2c_arm=on" | sudo tee -a /boot/firmware/config.txt; \
		fi; \
	elif [ -f /boot/config.txt ]; then \
		if ! grep -q "^dtparam=i2c_arm=on" /boot/config.txt; then \
			echo "dtparam=i2c_arm=on" | sudo tee -a /boot/config.txt; \
		fi; \
	fi
	@echo "I2C configuration verified."

# All-in-one setup command: installs deps, enables I2C, builds, installs, and starts service
setup: deps enable-i2c all install service-start
	@echo "=================================================================="
	@echo "  Setup complete! Waveshare PoE HAT (B) daemon is now running.   "
	@echo "=================================================================="
	@sudo systemctl status rpi4-poe-hat.service --no-pager || true

# ------------------------------------------------------------------------------
# Service & Installation Management
# ------------------------------------------------------------------------------
install: $(TARGET)
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	install -d /etc/systemd/system
	install -m 644 rpi4-poe-hat.service /etc/systemd/system/rpi4-poe-hat.service
	systemctl daemon-reload
	@echo "Installed binary and systemd service successfully."

service-start:
	sudo systemctl enable --now rpi4-poe-hat.service

service-stop:
	-sudo systemctl stop rpi4-poe-hat.service

service-restart:
	sudo systemctl restart rpi4-poe-hat.service

service-status:
	sudo systemctl status rpi4-poe-hat.service

uninstall: service-stop
	-sudo systemctl disable rpi4-poe-hat.service
	rm -f /etc/systemd/system/rpi4-poe-hat.service
	rm -f /usr/local/bin/$(TARGET)
	sudo systemctl daemon-reload
	@echo "Uninstalled service and binary successfully."

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all deps enable-i2c setup install service-start service-stop service-restart service-status uninstall clean
