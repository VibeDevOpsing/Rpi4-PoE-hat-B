#ifndef HDC1080_H
#define HDC1080_H

#define HDC1080_I2C_ADDR 0x40

// Initialize HDC1080 temperature/humidity sensor. Returns 0 if found and configured, -1 otherwise.
int hdc1080_init(int fd);

// Read temperature (Celsius) and relative humidity (%). Returns 0 on success, -1 on error.
int hdc1080_read(int fd, float *temp, float *hum);

#endif // HDC1080_H
