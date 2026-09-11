#!/usr/bin/env python3
# -*- coding:utf-8 -*-
"""
SDL_Pi_HDC1080
Python 3 Driver for HDC1080 Temperature & Humidity Sensor
Updated with instance-based handles, exception handling, and graceful offline fallback.
"""

import struct
import array
import time
import io
import fcntl
import os

# I2C Address
HDC1080_ADDRESS = 0x40

# Registers
HDC1080_TEMPERATURE_REGISTER = 0x00
HDC1080_HUMIDITY_REGISTER = 0x01
HDC1080_CONFIGURATION_REGISTER = 0x02
HDC1080_MANUFACTURERID_REGISTER = 0xFE
HDC1080_DEVICEID_REGISTER = 0xFF
HDC1080_SERIALIDHIGH_REGISTER = 0xFB
HDC1080_SERIALIDMID_REGISTER = 0xFC
HDC1080_SERIALIDBOTTOM_REGISTER = 0xFD

# Configuration Register Bits
HDC1080_CONFIG_RESET_BIT = 0x8000
HDC1080_CONFIG_HEATER_ENABLE = 0x2000
HDC1080_CONFIG_ACQUISITION_MODE = 0x1000
HDC1080_CONFIG_BATTERY_STATUS = 0x0800
HDC1080_CONFIG_TEMPERATURE_RESOLUTION = 0x0400
HDC1080_CONFIG_HUMIDITY_RESOLUTION_HBIT = 0x0200
HDC1080_CONFIG_HUMIDITY_RESOLUTION_LBIT = 0x0100

HDC1080_CONFIG_TEMPERATURE_RESOLUTION_14BIT = 0x0000
HDC1080_CONFIG_TEMPERATURE_RESOLUTION_11BIT = 0x0400

HDC1080_CONFIG_HUMIDITY_RESOLUTION_14BIT = 0x0000
HDC1080_CONFIG_HUMIDITY_RESOLUTION_11BIT = 0x0100
HDC1080_CONFIG_HUMIDITY_RESOLUTION_8BIT = 0x0200

I2C_SLAVE = 0x0703


class SDL_Pi_HDC1080:
    def __init__(self, twi=1, addr=HDC1080_ADDRESS):
        self.fr = None
        self.fw = None
        self.available = False
        dev_path = f"/dev/i2c-{twi}"

        if not os.path.exists(dev_path):
            return

        try:
            self.fr = io.open(dev_path, "rb", buffering=0)
            self.fw = io.open(dev_path, "wb", buffering=0)

            fcntl.ioctl(self.fr, I2C_SLAVE, addr)
            fcntl.ioctl(self.fw, I2C_SLAVE, addr)
            time.sleep(0.015)

            config = HDC1080_CONFIG_ACQUISITION_MODE
            s = [HDC1080_CONFIGURATION_REGISTER, config >> 8, 0x00]
            self.fw.write(bytearray(s))
            time.sleep(0.015)
            self.available = True
        except (OSError, IOError):
            self.available = False
            self.close()

    def close(self):
        """Close opened I2C handles."""
        if self.fr is not None:
            try:
                self.fr.close()
            except Exception:
                pass
            self.fr = None
        if self.fw is not None:
            try:
                self.fw.close()
            except Exception:
                pass
            self.fw = None

    def readTemperature(self):
        """Read temperature in Celsius."""
        if not self.available or self.fw is None or self.fr is None:
            return 0.0
        try:
            self.fw.write(bytearray([HDC1080_TEMPERATURE_REGISTER]))
            time.sleep(0.020)
            data = self.fr.read(2)
            if len(data) == 2:
                buf = array.array('B', data)
                temp = (buf[0] * 256) + buf[1]
                return (temp / 65536.0) * 165.0 - 40.0
        except (OSError, IOError):
            pass
        return 0.0

    def readHumidity(self):
        """Read relative humidity in percent."""
        if not self.available or self.fw is None or self.fr is None:
            return 0.0
        try:
            self.fw.write(bytearray([HDC1080_HUMIDITY_REGISTER]))
            time.sleep(0.020)
            data = self.fr.read(2)
            if len(data) == 2:
                buf = array.array('B', data)
                humidity = (buf[0] * 256) + buf[1]
                return (humidity / 65536.0) * 100.0
        except (OSError, IOError):
            pass
        return 0.0

    def readConfigRegister(self):
        if not self.available or self.fw is None or self.fr is None:
            return 0
        try:
            self.fw.write(bytearray([HDC1080_CONFIGURATION_REGISTER]))
            time.sleep(0.020)
            data = self.fr.read(2)
            if len(data) == 2:
                buf = array.array('B', data)
                return buf[0] * 256 + buf[1]
        except (OSError, IOError):
            pass
        return 0

    def turnHeaterOn(self):
        if not self.available:
            return
        config = self.readConfigRegister() | HDC1080_CONFIG_HEATER_ENABLE
        try:
            self.fw.write(bytearray([HDC1080_CONFIGURATION_REGISTER, config >> 8, 0x00]))
            time.sleep(0.015)
        except Exception:
            pass

    def turnHeaterOff(self):
        if not self.available:
            return
        config = self.readConfigRegister() & ~HDC1080_CONFIG_HEATER_ENABLE
        try:
            self.fw.write(bytearray([HDC1080_CONFIGURATION_REGISTER, config >> 8, 0x00]))
            time.sleep(0.015)
        except Exception:
            pass

    def setHumidityResolution(self, resolution):
        if not self.available:
            return
        config = (self.readConfigRegister() & ~0x0300) | resolution
        try:
            self.fw.write(bytearray([HDC1080_CONFIGURATION_REGISTER, config >> 8, 0x00]))
            time.sleep(0.015)
        except Exception:
            pass

    def setTemperatureResolution(self, resolution):
        if not self.available:
            return
        config = (self.readConfigRegister() & ~0x0400) | resolution
        try:
            self.fw.write(bytearray([HDC1080_CONFIGURATION_REGISTER, config >> 8, 0x00]))
            time.sleep(0.015)
        except Exception:
            pass
