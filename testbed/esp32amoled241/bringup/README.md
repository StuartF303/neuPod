# ESP32-S3-Touch-AMOLED-2.41 bring-up probe

Dev-only capability probe for the Waveshare ESP32-S3-Touch-AMOLED-2.41 (ESP32-S3R8, 16 MB flash,
8 MB octal PSRAM, RM690B0 450x600 AMOLED on QSPI, FT6336 touch). Not part of any neuPod product build.
Every step prints PASS/FAIL with numbers on the USB-C port (native USB-Serial/JTAG, so the baud rate does not matter).
A summary table reprints every 20 s; send `s` on the console to reprint it at once.

## What it checks

| Step | Result row | Notes |
|------|-----------|-------|
| GPIO16 high first, chip / flash / heap | `boot/chip` | PASS needs 16 MB flash and >= 7 MB PSRAM |
| PSRAM: biggest block, 3 patterns, MB/s | `psram` | write/read speed measured on the first pattern |
| I2C scan, TCA9554 (EXIO1 PWR_EN high) | `i2c scan`, `tca9554` | expects 0x20, 0x38, 0x51, 0x6A |
| Display 600x450 landscape, test pattern | `display` | bars, 1 px border, corner marks (TL red, TR green, BL blue, BR white), banner |
| Full-frame push time and fps | `frame push` | PSRAM framebuffer -> 2 internal DMA strips -> QSPI 40 MHz |
| Brightness 50 % then 100 % | `brightness` | panel command 0x51; confirm the dim step by eye |
| Touch FT6336 id, then points | `touch id`, `touch point` | dot drawn per touch; two fingers clear the screen |
| IMU WHO_AM_I, RTC time | `imu`, `rtc` | QMI8658 (0x6A) answers 0x05; RTC is read only, never written |
| Battery mV and power key every 2 s | `battery`, `power key` | hold the key 3 s to drive GPIO16 low |
| SD over SPI | `sd card` | identification only, nothing is written |
| Wi-Fi scan and BLE scan | `wifi scan`, `ble scan` | scan only; top 10 by RSSI; radios off afterwards |

Console keys: `s` summary, `r` cycle display mode, `t` cycle touch mapping, `c` clear/redraw, `b` brightness 50/100 %.

## Pins

| Function | Pin |
|----------|-----|
| AMOLED QSPI | CS 9, CLK 10, D0-D3 11-14, RST 21 |
| AMOLED power enable / TE | TCA9554 EXIO1 (driven high) / EXIO0 |
| I2C (one bus) | SDA 47, SCL 48 |
| TCA9554 0x20 / FT6336 0x38 / PCF85063 0x51 / QMI8658 0x6A | touch RST GPIO3, touch INT EXIO2 (not used: the controller is polled) |
| SD (SPI3) | CS 2, SCK 4, MOSI 5, MISO 6 |
| Power | Key_BAT GPIO15 (sense), BAT_Control GPIO16 (HIGH holds battery power), BAT_ADC GPIO17 (ADC2 ch 6, divider 200k/100k, x3) |

## Build

Same container as CI (`.github/workflows/firmware.yml`): ESP-IDF v5.3.2. Nothing needed v5.5. Run as the host user so
no root-owned files land in the tree (run from the repo root):

```sh
docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp -v "$PWD":/project \
  -w /project/testbed/esp32amoled241/bringup espressif/idf:v5.3.2 \
  sh -c 'idf.py set-target esp32s3 build && idf.py merge-bin'
```

The component manager fetches `espressif/esp_lcd_sh8601` 2.0.1 (Apache-2.0, pinned in `main/idf_component.yml` and
`dependencies.lock`) and its helper `espressif/cmake_utilities`. Output: `build/merged-binary.bin` (bootloader at
0x0, partition table at 0x8000, app at 0x10000; no extra partitions).

## Flash

Merged image at 0x0 (this overwrites the factory firmware; restore it below):

```sh
~/.venvs/esptool/bin/esptool --chip esp32s3 --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:B1:65:40-if00 \
  write-flash 0x0 build/merged-binary.bin
```

Read the log from the same port (it re-enumerates after a reset; for example `screen <port> 115200`, or
`python -m serial.tools.miniterm <port>`).

## Restore the factory firmware

```sh
~/.venvs/esptool/bin/esptool --chip esp32s3 --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:B1:65:40-if00 \
  write-flash 0 ~/esp32amoled-backup/factory-flash-16MB.bin
```

## Things the probe cannot know in advance (check on the bench)

- **Landscape direction.** The SH8601 driver used here does not implement `swap_xy`, so landscape is MADCTL 0x30
  sent directly (hardware mode, offset applied to the rows). Press `r` to cycle: hardware, software rotate A (meant
  to match the hardware picture), software rotate B (180 degrees from A). The corner marks show which is right.
- **Touch direction.** The default mapping follows the factory demo (swap + mirror y); press `t` to cycle the eight
  swap/mirror combinations until the dot lands under the finger. Mode changes reset it.
- **Power key polarity.** Assumed active low (pull-up, key to ground), from the schematic. If the first
  `power key` line reports a level of 0 with nothing pressed, flip `KEY_PRESSED_LEVEL` in `main/board.h`. Power-off
  only arms after a released state was seen; on USB power GPIO16 low has no effect and the probe says so and re-holds.
- **Battery reading.** With no battery fitted the row FAILs (outside 2.8-4.4 V). ADC2 is shared with Wi-Fi, so the
  battery is read before the radios start and skipped (last value shown) while they run.
- **Flash header.** The image header says DIO; with the bootloader's flash auto-detect the chip runs QIO at runtime.
- **Console input.** `s` and the other keys use the USB-Serial/JTAG driver. If your terminal does not forward keystrokes,
  the 20 s summary still prints.

## Provenance and licences

All source here is written for this probe (MPL-2.0, like the rest of `testbed/`). The RM690B0 init sequence uses the
register values of Waveshare's published demo (Arduino/ESP-IDF "ESP32-S3-Touch-AMOLED-2.41-Demo"); the demo archive
carries no licence file for Waveshare's own code (only LVGL's), so none of it is copied: the table in
`main/display.c` is a re-typed list of register writes. Fetched at build time and not vendored: ESP-IDF v5.3.2
(Apache-2.0), `espressif/esp_lcd_sh8601` and `espressif/cmake_utilities` (Apache-2.0).
