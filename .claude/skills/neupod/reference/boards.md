# Boards

Fuller detail lives in the per-board docs; this is the quick sheet.

## USB IDs (what `lsusb` shows)

| ID | Device |
|---|---|
| `2e8a:000c` | Raspberry Pi Debug Probe (Pico 2 W rig's SWD + UART, `/dev/ttyACM0`) |
| `2e8a:000f` | An RP2350 in BOOTSEL (ready for `picotool load`) |
| `2e8a:0009` (factory demo) or the SDK stdio ID | RP2350 AMOLED board running an image with USB stdio (`/dev/serial/by-id/usb-Raspberry_Pi_Pico_*`) |
| `303a:1001` | ESP32-S3 AMOLED board, native USB-Serial/JTAG (`/dev/serial/by-id/usb-Espressif*`) |
| `18d1:4ee8` | Y2 running Buildroot (USB network gadget, host interface `enx*`; host gets 192.168.7.1, device 192.168.7.2) |
| `0e8d:2000` / `0e8d:0003` | Y2 in MTK preloader / BROM (mtkclient: owner only) |

## Pico 2 W rig (`rp2350`, board `pico2w`)

Breadboard testbed: Pico 2 W, Adafruit 2.0" 320x240 ST7789, Pimoroni Pico Audio Pack, SD breakout (SDIO 4-bit on GP2–7),
MAX17048 gauge on a LiPo. CYW43439 radio (Wi-Fi + BTstack A2DP). No wheel fitted: drive it by UART keys. Wiring:
`docs/wiring-phase1.md`. **Frozen**: kept building, no new features that grow RAM.

## Waveshare RP2350-Touch-AMOLED-2.41 (`rp2350-amoled241`)

RP2350B, 16 MB flash, **8 MB PSRAM** (QMI CS1 = GPIO47; Waveshare says 2 MB, wrong). No radio, no audio, no wheel.
- AMOLED RM690B0 450x600 (shown 600x450) on PIO-QSPI: CS 9, CLK 10, D0–3 11–14, RST 15, TE 16, PWR_EN 17. Our driver:
  `platforms/rp2350/boards/amoled241/hal_display_rm690b0.c`. Default MADCTL 0xA0, page offset 16.
- Touch FT6336 at I2C1 0x38: SDA 6, SCL 7, INT 4, RST 5. Chip ID 0x64. Map: x = raw y, y = 449 − raw x.
- microSD on SPI0: SCK 18, MOSI 19, MISO 20, CS 23.
- Power: ETA6098 charger, soft latch: key sense GPIO24, hold-on GPIO25, BAT_ADC GPIO40 (×3 divider).
- Factory demo backup: `~/amoled241-backup/factory-demo.uf2`. Doc: `docs/rp2350-amoled241.md`.

## Waveshare ESP32-S3-Touch-AMOLED-2.41 (`esp32s3`)

ESP32-S3R8 (8 MB octal PSRAM), 16 MB flash. Wi-Fi + **BLE only (no Classic BT, so no A2DP)**. No audio.
- AMOLED RM690B0 on QSPI: CS 9, CLK 10, D0–3 11–14, RST 21 (wiki's GPIO38 is wrong). TE and PWR_EN are on the TCA9554
  expander (0x20). The screen needs the expander set to the factory state (output reg 0x03, then config reg 0xDC; `board.c` does this
  at boot; cold power-on default leaves the panel dark) **and** the factory init table sent verbatim (`hal_display.c`). Expander state
  survives an ESP reset: test display changes from a full power cycle (USB and battery out).
- I2C SDA 47 / SCL 48: FT6336 0x38 (RST GPIO3), QMI8658 at **0x6B**, PCF85063, TCA9554 0x20.
  Touch map with MADCTL 0x30: x = raw y, y = 449 − raw x (bench-confirmed 15 Nov 2026).
- microSD SPI: CS 2, SCK 4, MOSI 5, MISO 6.
- Power: same ETA6098 + latch: key GPIO15, hold-on GPIO16 (latched early in `ESP_SYSTEM_INIT_FN`), BAT_ADC GPIO17.
- Factory backup: `~/esp32amoled-backup/factory-flash-16MB.bin` (sha256 d45cf1d5…). Doc: `docs/esp32s3.md`.

## Innioasis Y2 (`y2`)

MT6582 (2× Cortex-A7, cpu1 hotplugged), 1 GB RAM, MTK 3.4.67 Android-style kernel (earlysuspend, wakelocks, `/dev/alarm`).
480x360 panel (fb0 XRGB8888, virtual 480x1104, stride 1920, page-flipped by `pan`). Inputs: `mtk-kpd` (keys incl. Power),
`mtk-tpd` (touch wheel), `ACCDET` (headset). ERM vibrator (`/sys/class/timed_output/vibrator/enable`). Internal speaker plus
wired out. MTK combo radio (WMT, `6620_launcher`): BT 4.0 (BTstack over `/dev/stpbt`), Wi-Fi recon only. microSD = `mmcblk1`
(never `mmcblk0`). Power off with USB in boots into KPOC charge mode. Private notes: `testbed/y2/docs/` (local only).
