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
- CI: firmware (rp2350, host, esp32s3 allow-failure), docs deploy, issue/PR templates,
  CODEOWNERS, Dependabot.

### Changed
- Default branch renamed `master` → `main`.
- `app/` is now portable; the LED blink + `neuPod alive` loop runs via the HAL on every platform.
- Build output moved to `build/<platform>/`.
