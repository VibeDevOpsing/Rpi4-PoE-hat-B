#include "poe_hat.h"

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <linux/i2c-dev.h>
#else
#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif
#endif

static int s_fan_state = 0;

int poe_hat_open_i2c(const char *dev_path) {
    if (!dev_path) dev_path = "/dev/i2c-1";
#ifdef __linux__
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open I2C bus");
        return -1;
    }
    return fd;
#else
    (void)dev_path;
    return -1; // Mock mode on non-Linux
#endif
}

int poe_hat_fan_set(int fd, int on) {
    s_fan_state = on ? 1 : 0;
    if (fd < 0) {
        return 0; // Simulated
    }

    if (ioctl(fd, I2C_SLAVE, PCF8574_I2C_ADDR) < 0) {
        return -1;
    }

    uint8_t cur = 0xFF;
    if (read(fd, &cur, 1) != 1) {
        cur = 0xFF;
    }

    if (on) {
        cur &= 0xFE; // P0 = 0 -> Fan ON
    } else {
        cur |= 0x01; // P0 = 1 -> Fan OFF
    }

    if (write(fd, &cur, 1) != 1) {
        return -1;
    }
    return 0;
}

int poe_hat_fan_get(int fd) {
    if (fd < 0) {
        return s_fan_state;
    }

    if (ioctl(fd, I2C_SLAVE, PCF8574_I2C_ADDR) < 0) {
        return s_fan_state;
    }

    uint8_t cur = 0xFF;
    if (read(fd, &cur, 1) == 1) {
        s_fan_state = (cur & 0x01) ? 0 : 1;
    }
    return s_fan_state;
}
