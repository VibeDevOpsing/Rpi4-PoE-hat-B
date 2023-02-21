##Contol FAN and LED screen on Poe-HAT-B for Raspberry Pi 4

This is the default script on Python3 for control FUN and LED screen On PoE-HAT-B

**Example to run:**
```console
git clone https://github.com/Toli-sman/Rpi4-PoE-hat-B.git
sudo apt update && sudo apt install -y -qq python3 python3-pip
sudo pip3 install RPi.GPIO smbus
cd Rpi4-PoE-hat-B
python3 main.py &
```

1. Path to drivers:
`waveshare_POE_HAT_B/POE_HAT_B.py` - FAN driver
`waveshare_POE_HAT_B/SSD1306.py` -LED driver

2. Pin connections can be viewed in waveshare_TSL2591\TSL2591.py and will be repeated here:
TSL25911    =>    Jetson Nano/RPI(BCM)
VCC         ->    3.3V
GND         ->    GND
SDA         ->    SDA
SCL         ->    SCL
INT         ->    4

Link of documentation: [PoE-HAT-B][def]

[def]: https://www.waveshare.com/wiki/PoE_HAT_(B)?spm=a2g0o.detail.1000023.17.3e603f2dt24UJD