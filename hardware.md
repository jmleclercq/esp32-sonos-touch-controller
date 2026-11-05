# Hardware

## Board

**ESP32-3248S035C** — 3.5" all-in-one ESP32 display board ("CYD" family, capacitive variant).

| | |
|---|---|
| MCU | ESP32 (dual core, 4 MB flash, **no PSRAM**) |
| Display | 3.5" TFT, 320 × 480, **ST7796** controller, SPI |
| Touch | **GT911** capacitive, I²C (address 0x5D) |
| USB | CH340 USB-to-serial, used for flashing and logs |

> The resistive variant (ESP32-3248S035**R**) uses an XPT2046 touch controller on a separate SPI bus and is **not** supported by this configuration.

No wiring is needed: display and touch are on the board. The project is used in **landscape** (480 × 320).

## Pin mapping

| Function | GPIO |
|---|---:|
| Display SCK | 14 |
| Display MOSI | 13 |
| Display MISO | 12 |
| Display CS | 15 |
| Display DC | 2 |
| Display RST | 4 |
| Backlight (PWM) | 27 |
| Touch SDA | 33 |
| Touch SCL | 32 |
| Touch RST | 25 |
| Touch INT | 21 (not used, touch is polled) |

## Touch orientation

The GT911 reports its axes swapped relative to the landscape display. `map_touch()` in `ui.h` converts raw coordinates
to screen coordinates (calibrated on the four corners of one unit):

```
x = 30 + (raw_y - 20) * 1.5
y = 30 + (421 - raw_x) * 0.655
```

If touches land in the wrong place on your unit, log the raw values of the four corners and adjust these two lines.

## Memory constraints

Without PSRAM, a full 480 × 320 frame buffer does not fit in RAM. The display uses a partial buffer (`buffer_size: 33%`,
4 bands of 80 lines) and `ui.h` repaints **one zone at a time**. See the README for details.
