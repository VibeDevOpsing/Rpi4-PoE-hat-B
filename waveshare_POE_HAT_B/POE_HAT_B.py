#!/usr/bin/env python3
# -*- coding:utf-8 -*-
"""
Waveshare PoE HAT (B) Python Controller
Updated for universal Python 3 compatibility, predictive cooling, and zero-flicker display.
"""

import os
import sys
import time
import socket
import logging
from PIL import Image, ImageDraw, ImageFont

from . import SSD1306
from . import SDL_Pi_HDC1080
from .SSD1306 import SMBus


class POE_HAT_B:
    def __init__(self, address=0x20, bus=1):
        self.address = address
        self.bus_num = bus
        self.i2c = SMBus(bus)
        self.logger = logging.getLogger(__name__)

        # Fan state tracking
        self.FAN_MODE = 0  # 0 = OFF, 1 = ON
        self.fan_on_time = 0
        self.cooldown_sec = 25
        self.anim_frame = 0

        # CPU Load tracking
        self._prev_active = 0
        self._prev_total = 0

        # Initialize OLED display
        try:
            self.show = SSD1306.SSD1306(bus=bus)
            self.show.Init()
            self.display_available = True
        except Exception as e:
            self.logger.warning("Could not initialize SSD1306 OLED display: %s", e)
            self.display_available = False
            self.show = None

        # Initialize HDC1080 temp/humidity sensor (optional)
        try:
            self.hdc1080 = SDL_Pi_HDC1080.SDL_Pi_HDC1080(twi=bus)
            if not self.hdc1080.available:
                self.hdc1080 = None
        except Exception as e:
            self.logger.warning("HDC1080 sensor not detected: %s", e)
            self.hdc1080 = None

        # Load fonts once during initialization
        dir_path = os.path.dirname(os.path.abspath(__file__))
        font_path = os.path.join(dir_path, 'Courier_New.ttf')
        try:
            if os.path.exists(font_path):
                self.font_main = ImageFont.truetype(font_path, 11)
                self.font_small = ImageFont.truetype(font_path, 9)
            else:
                self.font_main = ImageFont.load_default()
                self.font_small = ImageFont.load_default()
        except Exception:
            self.font_main = ImageFont.load_default()
            self.font_small = ImageFont.load_default()

        # Turn fan OFF initially
        self.FAN_OFF()

    def FAN_ON(self):
        """Turn cooling fan ON (pull pin P0 low on PCF8574)."""
        try:
            cur = self.i2c.read_byte(self.address)
            self.i2c.write_byte(self.address, 0xFE & cur)
            self.FAN_MODE = 1
        except Exception as e:
            self.logger.debug("FAN_ON error: %s", e)

    def FAN_OFF(self):
        """Turn cooling fan OFF (pull pin P0 high on PCF8574)."""
        try:
            cur = self.i2c.read_byte(self.address)
            self.i2c.write_byte(self.address, 0x01 | cur)
            self.FAN_MODE = 0
        except Exception as e:
            self.logger.debug("FAN_OFF error: %s", e)

    def GET_IP(self):
        """Get primary non-loopback IP address with safe offline fallback."""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.settimeout(0.2)
            # Connecting to non-routable address doesn't send packets
            s.connect(('8.8.8.8', 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            pass

        # Fallback: inspect socket hostname
        try:
            hostname = socket.gethostname()
            ip = socket.gethostbyname(hostname)
            if not ip.startswith('127.'):
                return ip
        except Exception:
            pass
        return "Offline"

    def GET_Temp(self):
        """Read Raspberry Pi SoC temperature in Celsius."""
        try:
            with open('/sys/class/thermal/thermal_zone0/temp', 'rt') as f:
                return float(f.read().strip()) / 1000.0
        except Exception:
            return 0.0

    def GET_CPU_Load(self):
        """Calculate CPU load percentage from /proc/stat."""
        try:
            with open('/proc/stat', 'rt') as f:
                line = f.readline()
            parts = [float(x) for x in line.strip().split()[1:9]]
            user, nice, system, idle, iowait, irq, softirq, steal = parts

            active = user + nice + system + irq + softirq + steal
            total = active + idle + iowait

            if self._prev_total == 0:
                self._prev_active = active
                self._prev_total = total
                return 0.0

            delta_active = active - self._prev_active
            delta_total = total - self._prev_total

            self._prev_active = active
            self._prev_total = total

            if delta_total <= 0:
                return 0.0
            return max(0.0, min(100.0, (delta_active / delta_total) * 100.0))
        except Exception:
            return 0.0

    def GET_RAM_Usage(self):
        """Read RAM usage from /proc/meminfo."""
        try:
            total_kb, avail_kb = 0, 0
            with open('/proc/meminfo', 'rt') as f:
                for line in f:
                    if line.startswith('MemTotal:'):
                        total_kb = int(line.split()[1])
                    elif line.startswith('MemAvailable:'):
                        avail_kb = int(line.split()[1])
            if total_kb > 0:
                used_kb = total_kb - avail_kb
                pct = int((used_kb * 100) / total_kb)
                return (used_kb / 1024.0 / 1024.0, pct)  # GB, %
        except Exception:
            pass
        return (0.0, 0)

    def GET_Temp_Hum(self):
        """Read temperature & relative humidity from HDC1080 sensor."""
        if self.hdc1080 and self.hdc1080.available:
            try:
                t = self.hdc1080.readTemperature()
                h = self.hdc1080.readHumidity()
                return (f"{t:3.1f}", f"{h:3.1f}")
            except Exception:
                pass
        return (None, None)

    def POE_HAT_Display(self, FAN_TEMP=54):
        """
        Smart display refresh and predictive cooling cycle:
        - Evaluates temperature & CPU load
        - Applies hysteresis and anti-cycling cooldown timer
        - Renders dashboard on SSD1306 without screen flicker
        """
        now = time.time()
        temp = self.GET_Temp()
        load = self.GET_CPU_Load()
        ip = self.GET_IP()
        th = self.GET_Temp_Hum()
        ram_gb, ram_pct = self.GET_RAM_Usage()

        # -------------------------------------------------------------
        # Predictive Cooling Decision Engine
        # -------------------------------------------------------------
        if self.FAN_MODE == 0:
            # Trigger 1: High temperature
            # Trigger 2: Predictive trigger (temperature >= FAN_TEMP-6 and high CPU load)
            if temp >= FAN_TEMP or (temp >= (FAN_TEMP - 6) and load >= 65.0):
                self.FAN_ON()
                self.fan_on_time = now
        else:
            # Fan is currently ON: check cooldown and low temperature
            elapsed = now - self.fan_on_time
            if elapsed >= self.cooldown_sec:
                if temp < (FAN_TEMP - 6) and load < 60.0:
                    self.FAN_OFF()

        # -------------------------------------------------------------
        # Render OLED Frame (128x32)
        # -------------------------------------------------------------
        if not self.display_available or self.show is None:
            return

        image = Image.new('1', (self.show.width, self.show.height), 0)  # Black background
        draw = ImageDraw.Draw(image)

        # Vertical divider line at x = 104
        draw.line([(104, 0), (104, 31)], fill=1)

        # Right Pane: Fan Status & Animation (x = 106..127)
        draw.text((107, 0), "FAN", font=self.font_small, fill=1)

        # Animated propeller blades
        fan_icons = ["|", "/", "-", "\\"]
        if self.FAN_MODE == 1:
            self.anim_frame = (self.anim_frame + 1) % len(fan_icons)
            fan_char = fan_icons[self.anim_frame]
            draw.text((112, 10), fan_char, font=self.font_main, fill=1)
            # Inverted badge for ON
            draw.rectangle([(106, 23), (126, 31)], fill=1)
            draw.text((109, 23), "ON", font=self.font_small, fill=0)
        else:
            draw.text((112, 10), "x", font=self.font_main, fill=1)
            draw.text((107, 23), "OFF", font=self.font_small, fill=1)

        # Left Pane: System Metrics (x = 0..103)
        # Line 0: IP address
        draw.text((0, 0), f"IP:{ip}", font=self.font_main, fill=1)

        # Line 1: CPU Temp + graphical progress bar
        draw.text((0, 8), f"CPU:{int(temp)}C", font=self.font_main, fill=1)
        # Progress bar at x=46, y=9, w=30, h=6
        draw.rectangle([(46, 9), (76, 15)], outline=1, fill=0)
        bar_fill = int(28 * (min(100.0, load) / 100.0))
        if bar_fill > 0:
            draw.rectangle([(47, 10), (47 + bar_fill, 14)], outline=1, fill=1)
        draw.text((80, 8), f"{int(load)}%", font=self.font_main, fill=1)

        # Line 2: RAM
        draw.text((0, 16), f"RAM:{ram_gb:.1f}G {ram_pct}%", font=self.font_main, fill=1)

        # Line 3: Ambient Sensor or Info
        if th[0] is not None and th[1] is not None:
            draw.text((0, 24), f"ENV:{th[0]}C {th[1]}%", font=self.font_main, fill=1)
        else:
            draw.text((0, 24), "Waveshare PoE", font=self.font_small, fill=1)

        # Flush frame to SSD1306 without flickering
        self.show.ShowImage(self.show.getbuffer(image))

    def cleanup(self):
        """Turn off fan, blank display, and close buses on exit."""
        self.FAN_OFF()
        if self.show:
            try:
                self.show.ClearBlack()
                self.show.DisplayOff()
                self.show.Closebus()
            except Exception:
                pass
        if self.hdc1080:
            try:
                self.hdc1080.close()
            except Exception:
                pass
