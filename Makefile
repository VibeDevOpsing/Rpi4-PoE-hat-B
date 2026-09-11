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

install: $(TARGET)
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	install -d /etc/systemd/system
	install -m 644 rpi4-poe-hat.service /etc/systemd/system/rpi4-poe-hat.service
	systemctl daemon-reload
	@echo "Installed successfully. To enable and start:"
	@echo "  sudo systemctl enable --now rpi4-poe-hat.service"

uninstall:
	-systemctl stop rpi4-poe-hat.service
	-systemctl disable rpi4-poe-hat.service
	rm -f /etc/systemd/system/rpi4-poe-hat.service
	rm -f /usr/local/bin/$(TARGET)
	systemctl daemon-reload
	@echo "Uninstalled successfully."

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all install uninstall clean
