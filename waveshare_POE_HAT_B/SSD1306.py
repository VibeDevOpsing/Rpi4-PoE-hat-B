#!/usr/bin/env python3
# -*- coding:utf-8 -*-
"""
SSD1306 128x32 OLED Driver for Waveshare PoE HAT (B)
Supports smbus2, smbus, and pure Python fcntl fallback for universal compatibility.
"""

import os
import time

# Universal I2C SMBus Provider
try:
    from smbus2 import SMBus
except ImportError:
    try:
        from smbus import SMBus
    except ImportError:
        import fcntl
        I2C_SLAVE = 0x0703

        class FallbackSMBus(object):
            """Pure-Python I2C bus implementation using Linux fcntl ioctl."""
            def __init__(self, bus=1):
                self.bus_num = bus
                self.fd = None
                self._current_addr = None
                dev_path = f"/dev/i2c-{bus}"
                if os.path.exists(dev_path):
                    try:
                        self.fd = os.open(dev_path, os.O_RDWR)
                    except OSError:
                        self.fd = None

            def _set_address(self, addr):
                if self.fd is not None and self._current_addr != addr:
                    try:
                        fcntl.ioctl(self.fd, I2C_SLAVE, addr)
                        self._current_addr = addr
                    except OSError:
                        pass

            def write_byte_data(self, addr, cmd, val):
                if self.fd is None:
                    return
                self._set_address(addr)
                try:
                    os.write(self.fd, bytes([cmd & 0xFF, val & 0xFF]))
                except OSError:
                    pass

            def write_byte(self, addr, val):
                if self.fd is None:
                    return
                self._set_address(addr)
                try:
                    os.write(self.fd, bytes([val & 0xFF]))
                except OSError:
                    pass

            def read_byte(self, addr):
                if self.fd is None:
                    return 0xFF
                self._set_address(addr)
                try:
                    data = os.read(self.fd, 1)
                    return data[0] if data else 0xFF
                except OSError:
                    return 0xFF

            def close(self):
                if self.fd is not None:
                    try:
                        os.close(self.fd)
                    except OSError:
                        pass
                    self.fd = None

        SMBus = FallbackSMBus


class SSD1306(object):
    def __init__(self, width=128, height=32, addr=0x3C, bus=1):
        self.width = width
        self.height = height
        self.Column = width
        self.Page = int(height / 8)
        self.addr = addr
        self.bus = SMBus(bus)

    def SendCommand(self, cmd):
        """Send command to SSD1306."""
        try:
            self.bus.write_byte_data(self.addr, 0x00, cmd)
        except Exception:
            pass

    def SendData(self, val):
        """Write pixel data to SSD1306 RAM."""
        try:
            self.bus.write_byte_data(self.addr, 0x40, val)
        except Exception:
            pass

    def Closebus(self):
        """Close I2C bus."""
        try:
            self.bus.close()
        except Exception:
            pass

    def DisplayOff(self):
        """Turn display off into sleep mode."""
        self.SendCommand(0xAE)

    def Init(self):
        """Initialize display controller."""
        cmds = [
            0xAE,        # Display OFF
            0x40,        # Set display start line to 0
            0xB0,        # Set page address to 0
            0xC8,        # Set COM output scan direction reversed
            0x81, 0xFF,  # Set contrast control to max
            0xA1,        # Set segment re-map (col 127 mapped to SEG0)
            0xA6,        # Normal display (non-inverted)
            0xA8, 0x1F,  # Set multiplex ratio (32 rows = 0x1F)
            0xD3, 0x00,  # Display offset 0
            0xD5, 0xF0,  # Display clock divide ratio / oscillator frequency
            0xD9, 0x22,  # Set pre-charge period
            0xDA, 0x02,  # Set COM pins hardware configuration
            0xDB, 0x49,  # Set VCOMH deselect level
            0x8D, 0x14,  # Enable charge pump
            0xAF         # Display ON
        ]
        for cmd in cmds:
            self.SendCommand(cmd)

    def ClearBlack(self):
        """Clear the screen to black."""
        for i in range(0, self.Page):
            self.SendCommand(0xB0 + i)
            self.SendCommand(0x00)
            self.SendCommand(0x10)
            for j in range(0, self.Column):
                self.SendData(0x00)

    def ClearWhite(self):
        """Fill the screen with white."""
        for i in range(0, self.Page):
            self.SendCommand(0xB0 + i)
            self.SendCommand(0x00)
            self.SendCommand(0x10)
            for j in range(0, self.Column):
                self.SendData(0xFF)

    def getbuffer(self, image):
        """Convert PIL 1-bit Image into SSD1306 page buffer."""
        buf = [0x00] * (self.Page * self.Column)
        image_mono = image.convert('1')
        imwidth, imheight = image_mono.size
        pixels = image_mono.load()

        if imwidth == self.width and imheight == self.height:
            for y in range(imheight):
                page_idx = int(y / 8) * self.width
                bit_mask = 1 << (y % 8)
                for x in range(imwidth):
                    # In 1-bit PIL, 255 is white (pixel lit), 0 is black
                    if pixels[x, y] != 0:
                        buf[x + page_idx] |= bit_mask
        return buf

    def ShowImage(self, pBuf):
        """Write page buffer to SSD1306 display."""
        for i in range(0, self.Page):
            self.SendCommand(0xB0 + i)  # Set page address
            self.SendCommand(0x00)      # Set low column address
            self.SendCommand(0x10)      # Set high column address
            page_offset = self.width * i
            for j in range(0, self.Column):
                self.SendData(pBuf[j + page_offset])
