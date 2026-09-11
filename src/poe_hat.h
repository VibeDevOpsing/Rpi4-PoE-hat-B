#ifndef POE_HAT_H
#define POE_HAT_H

#define PCF8574_I2C_ADDR 0x20

// Open I2C bus (e.g. "/dev/i2c-1"). Returns file descriptor or -1 on error.
int poe_hat_open_i2c(const char *dev_path);

// Set fan state: 1 = ON, 0 = OFF. Returns 0 on success, -1 on error.
int poe_hat_fan_set(int fd, int on);

// Get current fan state: 1 = ON, 0 = OFF, -1 on error.
int poe_hat_fan_get(int fd);

#endif // POE_HAT_H
