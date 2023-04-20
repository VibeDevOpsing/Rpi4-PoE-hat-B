#!/usr/bin/python3
# Important: This file is not part of the original waveshare library
import logging
import sys
import time
import math
import smbus
import RPi.GPIO as GPIO

import datetime
from . import SDL_Pi_HDC1080

import os
import socket
import fcntl
import struct
from PIL import Image,ImageDraw,ImageFont
from . import SSD1306

# ----------------------------------------------------------

# init Temp an humkidity sensor
hdc1080 = SDL_Pi_HDC1080.SDL_Pi_HDC1080()
hdc1080.turnHeaterOn()
hdc1080.turnHeaterOff()
hdc1080.setTemperatureResolution(SDL_Pi_HDC1080.HDC1080_CONFIG_TEMPERATURE_RESOLUTION_11BIT)
hdc1080.setTemperatureResolution(SDL_Pi_HDC1080.HDC1080_CONFIG_TEMPERATURE_RESOLUTION_14BIT)
hdc1080.setHumidityResolution(SDL_Pi_HDC1080.HDC1080_CONFIG_HUMIDITY_RESOLUTION_8BIT)
hdc1080.setHumidityResolution(SDL_Pi_HDC1080.HDC1080_CONFIG_HUMIDITY_RESOLUTION_14BIT)

# Configur PoE HUT B
show = SSD1306.SSD1306()
show.Init();
dir_path = os.path.dirname(os.path.abspath(__file__))

font = ImageFont.truetype(dir_path+'/Courier_New.ttf',13)

image1 = Image.new('1', (show.width, show.height), "WHITE")
draw = ImageDraw.Draw(image1)
class POE_HAT_B:
    def __init__(self,address = 0x20):
        self.i2c = smbus.SMBus(1)
        self.address = address#0x20
        self.FAN_ON()
        self.FAN_MODE = 0;
    def FAN_ON(self):
        self.i2c.write_byte(self.address, 0xFE & self.i2c.read_byte(self.address))
        
    def FAN_OFF(self):
        self.i2c.write_byte(self.address, 0x01 | self.i2c.read_byte(self.address))
        
    def GET_IP(self):
        s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        s.connect(('8.8.8.8',80))
        ip=s.getsockname()[0]
        s.close()
        return ip
        
    def GET_Temp(self):
        with open('/sys/class/thermal/thermal_zone0/temp', 'rt') as f:
            temp = (int)(f.read() ) / 1000.0
        return temp

    def GET_Temp_Hum(self):
        temp_out = ("%3.1f" % hdc1080.readTemperature())
        hum_out = ("%3.1f" % hdc1080.readHumidity())
        return (temp_out, hum_out)

    def POE_HAT_Display(self, FAN_TEMP):
        # show.ClearBlack() # Clear the screen the black color (0x00) Flahs black
        show.ClearWhite() # Clear the screen the white color (0xFF) Flahs white
        
        image1 = Image.new('1', (show.width, show.height), "WHITE")
        draw = ImageDraw.Draw(image1)
        ip = self.GET_IP()
        temp = self.GET_Temp()
        th = self.GET_Temp_Hum()
        draw.text((0,0), 'IP:'+ str(ip), font = font, fill = 0)
        draw.text((0,10), 'T:'+ str(((int)(temp*10))/10.0) + 'C', font = font, fill = 0)
        draw.text((0,20), 'T:'+ str(th[0])+ 'C H:'+ str(th[1])+ '%' , font = font, fill = 0)
        if(temp>=FAN_TEMP):
            self.FAN_MODE = 1

        elif(temp<FAN_TEMP-2):
            self.FAN_MODE = 0

        if(self.FAN_MODE == 1):
            draw.text((76,10), 'FAN:ON', font = ImageFont.truetype(dir_path+'/Courier_New.ttf',12), fill = 0)
            self.FAN_ON()
        else:
            draw.text((76,10), 'FAN:OFF', font = ImageFont.truetype(dir_path+'/Courier_New.ttf',12), fill = 0)
            self.FAN_OFF()
        show.ShowImage(show.getbuffer(image1))

