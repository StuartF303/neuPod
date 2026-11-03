# Changelog

All notable changes to this project are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Documentation
- Design response of 3 Nov 2026 folded in: ADR-0012 (final-board codec, proposed; ES8311 mono check open), ADR-0013 (BT stage 2 RAM, PSRAM on the final board), ADR-0014 (OTA with bootrom A/B + TBYB), ADR-0015 (clock plan); ADR-0007 (no bare CYW43439 on the final board) and ADR-0011 (audio isolation is a requirement) updated; Audio Pack identified as PCM5100A + PAM8908 with GP22 = XSMT active low; approved volume-restore and secrets policies recorded as not built; requirements for antenna window, SD VDD load switch, BQ25895, haptic footprints, RAM budget lines; Wi-Fi order W2 → W4 → W3; new `docs/bench.md`.

### Changed
- Audio: ALAC decode hot paths run from SRAM (ALAC 16/44.1 load 38.4 to 30.2 %, worst packet 57 to 27.9 ms on the bench); PCM ring 2048 to 4096 frames (`NEUPOD_PCM_RING_FRAMES` in `app/os/memory_budget.h`, 93 ms at 44.1 kHz); codec arena 116 to 99 KB (net RAM -1 KB); 32-bit ALAC is rejected ("32-bit ALAC not supported") because it needs about 115 KB of arena. Vendored ALAC changes are listed in `app/third_party/alac/README.md`.
- RAM budget: FreeRTOS heap 112 to 80 KB and LVGL pool 96 to 72 KB (named constants in `app/os/memory_budget.h`); static RAM 481,588 to 436,480 bytes with BTstack and the settings store included (476,968 with Wi-Fi, secrets and the input widgets). The CYW43 worker task has 6 KB of stack. `md5.h` renamed `neupod_md5.h` (clash with BTstack's `md5.h`).

### Fixed
- Out of memory panic on the first settings save: FatFs scratch memory is now capped at 4 KB and never panics (`app/storage/ff_alloc.c`, `docs/storage.md`); the lwIP tcpip thread stack is 2 KB (was 1 KB, 312 B free after a join); the `wifi:` monitor line shows the network being joined.
- Wi-Fi review fixes: no HAL calls under the service mutex (deadlock), link loss now detected, 32-bit deadline wrap, clock without reader spin and with NTP sanity, settings saved in two alternating files (no rename/unlink) and never while audio plays, 32 settings tags, secrets wiped after use, modal lifetime fixes, CI builds with mbed TLS.

### Added
- Wi-Fi W1 (`docs/wifi.md`): lwIP on the FreeRTOS cyw43 arch beside BTstack, scan and join (WPA2, WPA2/3, WPA3, open), DHCP, DNS, SNTP, remembered networks with
  auto-reconnect and backoff, power policy (off while playing by default), Settings > Wi-Fi, a real clock on the home pane (EU summer-time rule), status-bar icon, `wifi:` monitor line, Extras > Network test.
- Wheel input widgets (`docs/ui-input.md`): text entry with masked and search modes, number, slider (Display backlight), choice and multi-choice, date/time, letter-jump overlay; host-tested models, LVGL views, a modal API.
- Secrets at rest: AES-256-GCM sealed Wi-Fi passwords and Bluetooth link keys, key from the chip ID through one replaceable function (protects a lost card, not a card plus board).
- Bluetooth stage 1 (see `docs/bluetooth.md`): BTstack classic (Pico SDK 2.3.1) on core 0 over the CYW43439, HCI bring-up with the local address and HCI version on the UART,
  device discovery (name, address, Class of Device, RSSI, audio-sink flag) through `hal_bt_*` and a queue to the UI, Settings > Bluetooth screen (status, address, Scan, devices),
  Bluetooth boot check (Wi-Fi stays degraded), host simulator with unit test and tour snapshots. No pairing or A2DP yet.
- Settings store on the SD card (`/.neupod/settings.bin`, CRC, atomic save, RAM-only fallback, an nfs write scope that confines the save's unlocked writes to `/.neupod`):
  volume (never restored louder than -30 dB), backlight and click feedback now persist; link keys for BTstack go here, never to the internal flash (core 1 runs from it).
- `LICENSE.md`: BTstack and cyw43-driver licence terms (non-commercial unless on Raspberry Pi hardware; unresolved for a custom board), see the table there.
- Codecs on the audio core: FLAC and MP3 (vendored `dr_flac` / `dr_mp3`, public domain / MIT-0) and ALAC (Apple's decoder core, Apache-2.0, with a C wrapper), all
  from one 116 KB static arena (no malloc on core 1); our own MP4/M4A demuxer (also for a later AAC) and tag readers (FLAC Vorbis comments, ID3v2, MP4 `ilst`) on core 0;
  Files plays `.wav`, `.flac`, `.mp3`, `.m4a`; Now Playing shows tags, a format badge and the length; FLAC MD5 verify mode (Right on a `.flac`); decoder CPU statistics;
  seek handshake between the cores; `tools/check_core1_closure.py` checks the linked image's call graph after every RP2350 build; core 1 gets its own 16 KB stack.
  Test vectors and bit-exact host tests (FLAC, ALAC), MP3 level/length checks, demuxer, tags, MD5, arena and handshake tests. See `docs/audio.md`, `LICENSE.md`.
- I2S audio output for the Pimoroni Pico Audio Pack (PIO0 + two ping-pong DMA channels, DMA_IRQ_1 on core 1,
  32-bit slots, 44.1/48/88.2/96 kHz, GP22 mute handling incl. the fatal-error path), a null-sink build option, an
  SDL sink on the host, software volume (default -30 dB), a -20 dBFS 1 kHz test tone, an L/R channel ident, a C-major
  scale, and a bring-up WAV player (16/24/32-bit, mono/stereo) streaming from the card. UI: Extras > Test tone, Select
  on a `.wav` in Files opens Now Playing (wheel = volume, Play = pause). Host unit tests for the WAV parser, sample
  conversion, volume maths and the generators. See `docs/audio.md`.
- Home menu UI (LVGL): screen stack with status bar, virtualised list widget with wheel acceleration,
  Music/Extras/Now Playing/placeholder screens, read-only SD file browser (directory reads in the storage
  task), Settings (About, Display backlight, Click feedback, Storage, Battery), haptic click feedback via
  its own task; host `--tour` mode writes snapshots of every screen. See `docs/ui.md`, `docs/runtime.md`.
- Runtime: single-core FreeRTOS on core 0 of the RP2350 and a bare-metal audio core on core 1 (ADR-0011),
  and the POSIX port on the host; `os.h` portable OS layer; LVGL v9 UI with a boot splash
  and placeholder home screen; concurrent read-only boot self-checks with a capabilities struct;
  audio timing stub (lock-free ring, decoder stub, jitter/underrun stats); fatal-error path.
  See `docs/runtime.md`, ADR-0009, ADR-0010.
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

- I²C0 HAL (timeouts, 9-pulse bus recovery, stats, host mock bus), MAX17048 fuel-gauge driver
  (`hal_battery.h`) and DRV2605L haptic driver (`hal_haptic.h`: ERM open/closed loop, auto-cal,
  sequences, RTP, low-latency click with OD_CLAMP limits enforced in tests); `i2c_test` with
  9 tests; host unit tests; `docs/i2c-peripherals.md`. Bench: both devices at 400 kHz, VCELL
  within 0.04 % of a meter, click 84 µs call-to-GO, 60 s soak with zero errors.

### Fixed
- Worked around a carlk3 v3.6.2 bug where 256-block SDIO reads overrun the DMA descriptor table.

### Changed
- RP2350 `clk_sys` is now 125 MHz with `clk_peri` on `clk_sys`, so SPI1 runs at exactly 62.5 MHz.
- Display: MADCTL 0x60, RGB, INVON, offsets 0/0, backlight active-high — confirmed on hardware.
- Default branch renamed `master` → `main`.
- `app/` is now portable; the LED blink + `neuPod alive` loop runs via the HAL on every platform.
- Build output moved to `build/<platform>/`.
