# Changelog

All notable changes to this project are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Repository hygiene: `.gitignore`, `.editorconfig`, `.clang-format`, licence files
  (MPL-2.0 / CERN-OHL-S-2.0 / CC-BY-4.0), code of conduct, contributing guide, security policy.
- Platform split: top-level `NEUPOD_PLATFORM=rp2350|esp32s3|host`; `platforms/rp2350` (Pico SDK,
  `pico2_w`), `platforms/esp32s3` (ESP-IDF skeleton, unbuilt), `hal/host`.
- Portable HAL interface headers in `hal/include/` (system, display, input, storage, audio, I²C,
  power, radio).
- RP2350 ST7789 display driver (SPI1 + DMA, backlight PWM) and `display_test` target — not
  hardware-tested.
- RP2350 PIO click-wheel receiver skeleton; portable frame decoder with resync — not
  hardware-tested.
- Host build: SDL2 window/input when available, headless otherwise; FatFs R0.15p1 over a
  disk-image file.
- Host unit tests (click-wheel decoder/resync, FatFs round-trip).
- Scripts: `build.sh`, platform-aware `flash.sh`/`console.sh`, `setup-esp-idf.sh`.
- Docs: wiring, click-wheel protocol, ADR-0001..0003, placeholders for requirements/design/
  reference; MkDocs Material site for GitHub Pages.
- CI: firmware (rp2350, host, esp32s3), docs deploy, issue/PR templates,
  CODEOWNERS, Dependabot.

- Display bring-up suite (`display_test`): UART menu, 8 tests (backlight, colours, sharpness,
  geometry, image, text/Now Playing, animation fps + MB/s, sleep), verified on the Phase 1 panel.
- `app/gfx/`: portable 2D layer, 8x8 (font8x8, public domain) and 15 px proportional (DejaVu
  Sans-derived "neuPod 16") fonts, strip renderer with 2×10 KB double buffer.
- HAL: `hal_display_fill_rect`, `hal_display_sleep`, MADCTL/inversion bring-up hooks;
  `hal_console.h` single-key input.
- Tools: `png2rgb565.py`, `gen_font.py`, `gen_test_image.py`; host `now_playing` viewer;
  gfx unit tests.

- microSD over PIO SDIO 4-bit (carlk3 no-OS-FatFS v3.6.2 submodule): `hal_storage` with card info,
  write guard + write-fault latch, High Speed (CMD6) support, `hal_storage_format()` (MBR +
  AU-aligned exFAT, 128 KB clusters); portable `nfs` file API (FatFs on RP2350, POSIX on host);
  `storage_test` with 9 tests + cleanup key; host unit tests; `docs/storage.md`, ADR-0008.
  Verified on the bench: tests 1-7 pass on exFAT at 15.625 MHz.

### Fixed
- Worked around a carlk3 v3.6.2 bug where 256-block SDIO reads overrun the DMA descriptor table.

### Changed
- RP2350 `clk_sys` is now 125 MHz with `clk_peri` on `clk_sys`, so SPI1 runs at exactly 62.5 MHz.
- Display: MADCTL 0x60, RGB, INVON, offsets 0/0, backlight active-high — confirmed on hardware.
- Default branch renamed `master` → `main`.
- `app/` is now portable; the LED blink + `neuPod alive` loop runs via the HAL on every platform.
- Build output moved to `build/<platform>/`.
