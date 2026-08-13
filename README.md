# Hello world plus 3D

> **Warning — overclocked CPU and I2C**
>
> This sketch pushes the hardware beyond typical defaults:
>
> - **CPU:** **200 MHz** via `set_sys_clock_khz(200000, true)` in `setup1()` on core 1 (above the default system clock).
> - **I2C:** **2 MHz** via `#define I2C_CLOCK_HZ 2000000` and `i2c_init(OLED_I2C, I2C_CLOCK_HZ)`. Many SSD1306 modules and wiring setups are rated for **400 kHz** (I2C Fast mode) or lower. Running I2C faster can cause garbled displays, missed updates, or bus errors, especially with long wires or weak pull-ups.
>
> Use at your own risk. If anything looks unstable, revert one or both settings below.
>
> **To run at default/safer settings:**
>
> 1. Open `Hello-world-plus-3D.ino`.
> 2. **CPU —** find `setup1()` (near the top of the file, after the display driver class). Delete or comment out:
>    ```cpp
>    set_sys_clock_khz(200000, true);
>    ```
> 3. **I2C —** near the top `#define` block, change the bus speed to a standard rate, for example:
>    ```cpp
>    #define I2C_CLOCK_HZ 400000
>    ```
>    (`400000` = 400 kHz Fast mode; `100000` = 100 kHz Standard mode if your module still misbehaves.)
> 4. Optional cleanup: if you also remove the commented `vreg_set_voltage(...)` line, you can delete these includes if nothing else needs them:
>    ```cpp
>    #include "hardware/vreg.h"
>    #include "hardware/clocks.h"
>    ```
> 5. Leave `setup1()` / `loop1()` in place — core 1 still updates `lissa_phase` for the Lissajous animation.
>
> Re-upload the sketch after editing. Animations may run slower at the default CPU clock; that is expected. Lower I2C speed may also reduce frame rate but should improve reliability.

Arduino demo for **RP2350** driving a **128×64 SSD1306 OLED** over **I2C0** on **GPIO 0 (SDA)** and **GPIO 1 (SCL)**.

The sketch shows a custom font “Hello, World”, grid overlays, a Lissajous curve, a 3D smiley face with wobble and roll, and several wireframe 3D shapes. Display updates use pico-sdk I2C with DMA (see the overclock warning above for bus speed).

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
