# Hello world plus 3D

> **Warning — overclocked CPU**
>
> This sketch runs the RP2350 at **200 MHz** (`set_sys_clock_khz(200000, true)` in `setup1()` on core 1). That is above the default clock and may cause instability, higher power use, or reduced long-term reliability on some boards. Use at your own risk.
>
> **To run at the default clock**, remove the overclock call:
>
> 1. Open `Hello-world-plus-3D.ino`.
> 2. Find `setup1()` (near the top of the file, after the display driver class).
> 3. Delete or comment out this line:
>    ```cpp
>    set_sys_clock_khz(200000, true);
>    ```
> 4. Optional cleanup: if you also remove the commented `vreg_set_voltage(...)` line, you can delete these includes if nothing else needs them:
>    ```cpp
>    #include "hardware/vreg.h"
>    #include "hardware/clocks.h"
>    ```
> 5. Leave `setup1()` / `loop1()` in place — core 1 still updates `lissa_phase` for the Lissajous animation.
>
> Re-upload the sketch after editing. Animations may run slower at the default clock; that is expected.

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
