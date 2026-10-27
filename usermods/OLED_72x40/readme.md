# OLED 72x40 (0.42" SSD1306)

Minimal status display for the tiny 0.42" OLED (72x40 pixels, SSD1306 controller) found on
"ESP32-C3 0.42 OLED" boards.

The standard *Four Line Display ALT* usermod only supports 128x32/128x64 panels. On this
display the 72x40 visible pixels sit at a column offset of 28 inside the 128 column GDDRAM,
so the 128-pixel layout shows up shifted and cut off. This usermod uses U8g2's dedicated
`U8G2_SSD1306_72X40_ER_F_HW_I2C` device and a layout made for 72x40 pixels.

## Display

```
Rainbow Runner     <- effect (scrolls if too long)
Party Colors       <- palette (scrolls if too long)
[#######   ] 63%   <- brightness
Sx128  Ix128       <- speed / intensity
192.168.1.42       <- IP, or "AP <ssid>" in AP mode
```

When the light is off, a clock is shown (requires NTP; shows `OFF` until the time is synced).
If the clock is disabled, the display turns off together with the light. After the configured idle
time the display goes to sleep and wakes up on the next change. The clock stays on while the light is off.

## Hardware (ESP32-C3 0.42" OLED board)

| Function      | GPIO |
|---------------|------|
| SDA           | 5    |
| SCL           | 6    |
| Onboard LED   | 8    |
| I2C address   | 0x3C |

GPIO8 and GPIO9 are strapping pins. Use a different pin (e.g. GPIO4) for the LED data line.

## Installation

Add `OLED_72x40` to `custom_usermods` of your build environment, or copy
`platformio_override.ini.sample` to the WLED root as `platformio_override.ini` and build:

```
pio run -e esp32c3_oled42
```

The sample presets the global I2C pins to SDA=5/SCL=6 and the default LED data pin to GPIO4 (only used on first boot, change it later under Config → LED Preferences).
Otherwise set the pins under **Config → Usermods → global I2C GPIOs** and reboot.

## Settings (Config → Usermods → OLED_72x40)

| Setting      | Default | Description                                          |
|--------------|---------|------------------------------------------------------|
| enabled      | true    | turn the display on/off                              |
| flip         | false   | rotate 180°                                          |
| contrast     | 128     | 0–255                                                |
| i2cAddress   | 60      | decimal (60 = 0x3C)                                  |
| sleepMin     | 5       | minutes without change before the display sleeps, 0 = never |
| clockOff     | true    | show clock while the light is off (NTP needed)       |

Requires the U8g2 library (pulled in automatically via `library.json`).
