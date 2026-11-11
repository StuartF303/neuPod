# neuPod

[![firmware](https://github.com/StuartF303/neuPod/actions/workflows/firmware.yml/badge.svg)](https://github.com/StuartF303/neuPod/actions/workflows/firmware.yml)
[![docs](https://github.com/StuartF303/neuPod/actions/workflows/docs.yml/badge.svg)](https://stuartf303.github.io/neuPod)
[![firmware licence: MPL-2.0](https://img.shields.io/badge/firmware-MPL--2.0-blue)](LICENSE-MPL-2.0)
[![hardware licence: CERN-OHL-S-2.0](https://img.shields.io/badge/hardware-CERN--OHL--S--2.0-green)](LICENSE-CERN-OHL-S-2.0)
[![docs licence: CC BY 4.0](https://img.shields.io/badge/docs-CC%20BY%204.0-lightgrey)](LICENSE-CC-BY-4.0)

neuPod is a drop-in replacement logic board for the Apple iPod Classic 6G (A1238). It keeps the
original shell, click wheel and (optionally) screen, and adds Wi-Fi, Bluetooth, microSD storage
and USB-C. The firmware is written from scratch. Two compute platforms are being evaluated side
by side from one tree: Raspberry Pi Pico 2 W / RP2350 (primary) and ESP32-S3 (comparison).

## Status

Phase 1 testbed (Pico 2 W on a breadboard):

| Item | Status |
|------|:------:|
| RP2350 build | ✅ |
| UART console | ✅ |
| Display (ST7789 SPI1 + DMA, 62.5 MHz; `display_test` suite) | ✅ |
| microSD (SDIO 4-bit, exFAT; `storage_test` suite) | ✅ |
| Runtime (FreeRTOS on core 0, bare-metal audio core on core 1, LVGL boot splash + home placeholder, boot self-checks) | 🟡 builds; host smoke-tested |
| Audio (bare-metal core 1 timing stub; no I2S/DAC yet) | 🟡 |
| I²C (MAX17048 gauge, DRV2605L haptics; `i2c_test` suite) | ✅ |
| Click wheel (decoder ✅ tested on host; PIO receiver skeleton) | 🟡 |
| Bluetooth A2DP | ⬜ |
| Wi-Fi | ⬜ |
| USB MSC | ⬜ |
| Host build + unit tests | ✅ |
| ESP32-S3 build | ⬜ skeleton, not yet built |

✅ working · 🟡 code written, not yet tested on hardware · ⬜ not started

## Features (planned)

- Reuses the iPod Classic 6G shell, click wheel and optionally the original screen
- microSD storage over 4-bit SDIO, FAT via FatFs
- Lossless audio end-to-end on wired/digital paths: decode once to a 32-bit mixer, no
  resampling unless the sink requires it
- Codecs: MP3, AAC, FLAC, ALAC, Opus
- Bluetooth A2DP (SBC / SBC-XQ) via BTstack, Wi-Fi, USB-C
- Desktop (SDL2) build of the UI for development without hardware

## Hardware platforms

| Platform | Role | Rationale |
|----------|------|-----------|
| Raspberry Pi Pico 2 W (RP2350 + CYW43439) | Primary | PIO handles SDIO and the click-wheel protocol in hardware, and the CYW43439 provides Wi-Fi and Bluetooth with BTstack support. |
| ESP32-S3 | Comparison | Integrated Wi-Fi/BLE and a mature ESP-IDF toolchain, built from the same portable `app/` sources for a like-for-like comparison. |

See [ADR-0001](docs/adr/0001-rp2350-phase1-platform.md).

## Repository layout

```
neuPod/
├── app/                 portable C: runtime/tasks, LVGL UI, boot checks, audio engine, drivers, FatFs
├── hal/
│   ├── include/         HAL interface headers (hal_display.h, hal_input.h, …)
│   └── host/            SDL2 desktop implementation
├── platforms/
│   ├── rp2350/          Pico SDK build entry + RP2350 HAL
│   ├── esp32s3/         ESP-IDF project skeleton + ESP32-S3 HAL
│   └── y2/              Innioasis Y2 testbed (Linux, armv7): same firmware as a process, Y2 HALs
├── lib/                 submodules: SD driver, FreeRTOS-Kernel (RP2350 SMP port), LVGL
├── tests/               host unit tests + headless runtime smoke test
├── tools/               PC-side utilities (C#/.NET welcome)
├── scripts/             build / flash / console helpers
├── docs/                design docs, ADRs, wiring (published to GitHub Pages)
└── boards/              KiCad (later)
```

## Quick start (RP2350)

Dependencies (Debian/Ubuntu):

    sudo apt install cmake ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib openocd minicom

Pico SDK ≥ 2.1 (tested with 2.3.1):

    export PICO_SDK_PATH=$HOME/pico/pico-sdk

Build, flash via a Raspberry Pi Debug Probe, and open the UART0 console (115200 8N1):

    cmake -S . -B build/rp2350 -G Ninja -DNEUPOD_PLATFORM=rp2350
    ninja -C build/rp2350          # or: scripts/build.sh rp2350
    scripts/flash.sh
    scripts/console.sh
    scripts/flash.sh rp2350 display_test   # ST7789 interactive test menu (then scripts/console.sh)

Host build and tests (SDL2 optional — `sudo apt install libsdl2-dev` for the window):

    scripts/build.sh host
    ctest --test-dir build/host --output-on-failure

Innioasis Y2 testbed (cross-build, then copy to the device over ssh; see [docs/y2.md](docs/y2.md)):

    scripts/build.sh y2
    testbed/y2/scripts/deploy-fw.sh --select

## Documentation

Everything lives in [`docs/`](docs/index.md) and is published at
<https://stuartf303.github.io/neuPod>: [requirements](docs/requirements.md),
[design](docs/design.md), [reference](docs/reference.md),
[Phase 1 wiring](docs/wiring-phase1.md), [click-wheel protocol](docs/click-wheel-protocol.md)
and [architecture decision records](docs/adr/).

## Licence

- Firmware and software: [MPL-2.0](LICENSE-MPL-2.0)
- Hardware: [CERN-OHL-S-2.0](LICENSE-CERN-OHL-S-2.0)
- Documentation: [CC BY 4.0](LICENSE-CC-BY-4.0)

See [LICENSE.md](LICENSE.md) for which applies where.

## Acknowledgements

- [Rockbox](https://www.rockbox.org/) for the iPod Classic 6G (ipod6g) reverse-engineering,
  including the click-wheel protocol.
- [Sprout](https://certainthings.studio/sprout/) by certain things for the prior art and licensing model.
- The iFlash, Moonlit and Elite Obsolete communities for keeping the iPod Classic alive.
