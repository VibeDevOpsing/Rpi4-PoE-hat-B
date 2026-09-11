#include "hdc1080.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <linux/i2c-dev.h>
#else
#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif
#endif

int hdc1080_init(int fd) {
    if (fd < 0) return -1;
    if (ioctl(fd, I2C_SLAVE, HDC1080_I2C_ADDR) < 0) return -1;

    // Config register 0x02: Acquisition mode (both temp & humidity), 14-bit resolution
    uint8_t cfg[3] = {0x02, 0x10, 0x00};
    if (write(fd, cfg, 3) != 3) {
        return -1;
    }
    usleep(15000); // 15ms startup delay
    return 0;
}

int hdc1080_read(int fd, float *temp, float *hum) {
    if (fd < 0 || !temp || !hum) return -1;
    if (ioctl(fd, I2C_SLAVE, HDC1080_I2C_ADDR) < 0) return -1;

    // 1. Trigger temperature conversion
    uint8_t reg = 0x00;
    if (write(fd, &reg, 1) != 1) return -1;
    usleep(20000); // 20ms conversion wait (datasheet 14-bit max is ~6.35ms)

    uint8_t buf[2];
    if (read(fd, buf, 2) != 2) return -1;
    uint16_t raw_t = ((uint16_t)buf[0] << 8) | buf[1];
    *temp = ((float)raw_t / 65536.0f) * 165.0f - 40.0f;

    // 2. Trigger humidity conversion
    reg = 0x01;
    if (write(fd, &reg, 1) != 1) return -1;
    usleep(20000); // 20ms conversion wait

    if (read(fd, buf, 2) != 2) return -1;
    uint16_t raw_h = ((uint16_t)buf[0] << 8) | buf[1];
    *hum = ((float)raw_h / 65536.0f) * 100.0f;

    return 0;
}
