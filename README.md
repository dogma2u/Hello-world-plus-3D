# Hello world plus 3D

Arduino demo for **RP2350** driving a **128×64 SSD1306 OLED** over **I2C0** on **GPIO 0 (SDA)** and **GPIO 1 (SCL)**.

The sketch shows a custom font “Hello, World”, grid overlays, a Lissajous curve, a 3D smiley face with wobble and roll, and several wireframe 3D shapes. Display updates use pico-sdk I2C with DMA at 2 MHz.

## Hardware

| Item | Detail |
|------|--------|
| MCU | RP2350 |
| Display | SSD1306, 128×64, I2C address `0x3C` |
| I2C port | `i2c0` |
| SDA | GPIO 0 |
| SCL | GPIO 1 |

## Software

- [Arduino-Pico](https://github.com/earlephilhower/arduino-pico) board support (RP2350)
- Adafruit GFX Library
- Adafruit SSD1306 Library

Open `Hello-world-plus-3D.ino` in the Arduino IDE (folder name must match the `.ino` filename).
