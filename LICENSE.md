# Licensing

neuPod is split across three licences, mirroring the Sprout project:

| Part | Paths | Licence | File |
|------|-------|---------|------|
| Firmware and software | `app/`, `hal/`, `platforms/`, `tests/`, `tools/`, `scripts/`, build files | Mozilla Public License 2.0 | [LICENSE-MPL-2.0](LICENSE-MPL-2.0) |
| Hardware designs | `boards/` (schematics, PCB layouts, BOMs, mechanical files) | CERN Open Hardware Licence v2 – Strongly Reciprocal | [LICENSE-CERN-OHL-S-2.0](LICENSE-CERN-OHL-S-2.0) |
| Documentation | `docs/`, `README.md` and other prose Markdown | Creative Commons Attribution 4.0 International | [LICENSE-CC-BY-4.0](LICENSE-CC-BY-4.0) |

SPDX identifiers: `MPL-2.0`, `CERN-OHL-S-2.0`, `CC-BY-4.0`.

Where a file carries its own SPDX header, that header takes precedence. Third-party code
(e.g. the Pico SDK, ESP-IDF, FatFs, codec libraries, and the microSD driver submodule
`lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico`, which is Apache-2.0) remains under its own licence.

## Third-party components

| Component | Path | Licence | Notes |
|-----------|------|---------|-------|
| FreeRTOS-Kernel (Raspberry Pi fork, pinned commit `4f7299d`, kernel V11.1.0+) | `lib/FreeRTOS-Kernel` (submodule) | MIT | The RP2350 port files carry Raspberry Pi's BSD-3-Clause notice (`freertos_sdk_config.h`). The POSIX port (host build) is MIT. |
| LVGL v9.6.0 | `lib/lvgl` (submodule) | MIT | Only `src/` is compiled; configuration in `app/ui/lv_conf.h`. |
| Montserrat (LVGL built-in text fonts) | inside LVGL `src/font` | SIL OFL 1.1 | Embedded as glyph bitmaps in the firmware. |
| Font Awesome 5 Free subset (LVGL built-in `LV_SYMBOL_*` glyphs) | inside LVGL `src/font` | Icons CC BY 4.0; font files SIL OFL 1.1 (upstream Font Awesome Free terms) | LVGL's own licence files do not restate this; the attribution is recorded here from Font Awesome's published terms. **Confirm before any commercial release.** TODO: ship the OFL (Montserrat, Font Awesome font files) and CC BY 4.0 (Font Awesome icons) notices in an About / third-party-licences screen of the firmware. |
| dr_flac v0.13.4, dr_mp3 (includes minimp3), dr_wav (David Reid, `mackron/dr_libs`, pinned commit `35b5c392cc15456736c440a68dbe073178bbffe0`) | `app/third_party/dr_libs/` | Public domain, or MIT No Attribution (your choice) | FLAC and MP3 decoders, compiled into the firmware (dr_wav is vendored but not used: the WAV header parser is ours). dr_mp3 embeds minimp3 (CC0, lieff). Licence text in `app/third_party/dr_libs/LICENSE` and at the end of each header; configured by `app/include/codec_dr_config.h`. |
| Apple Lossless Audio Codec (ALAC) decoder core, `macosforge/alac`, pinned commit `c38887c5c5e64a4b31108733bd79ca9b2496d987` | `app/third_party/alac/` | Apache License 2.0 | Copyright (c) 2011 Apple Inc. Files kept with their licence headers; the licence text is `app/third_party/alac/LICENSE` (also `APPLE_LICENSE.txt`). The upstream repository ships **no NOTICE file**, so there is nothing further to reproduce under section 4(d). **Modified** (section 4(b)): `alac_dec.c` is a C port of `ALACDecoder.cpp` (class turned into a struct, allocation through a callback instead of `calloc`). The Apple reference *encoder* is vendored only as a host test tool under `tests/tools/alac_encoder/` (never in the firmware). |
| Pico SDK, cyw43-driver | `~/pico/pico-sdk` (external) | BSD-3-Clause / see cyw43-driver | CYW43 firmware blob is Infineon/Cypress-licensed. |

