# Changelog

All notable changes to this project are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Resolution-independent UI ([ADR-0018](docs/adr/0018-ui-resolution-profiles.md), [UI](docs/ui.md#resolution-profiles)): every pixel size and the fonts live in one metrics struct chosen at UI start from the panel size. 320x240 (the product panel) keeps its exact layout; a 480x360 profile (1.5x, Montserrat 20 / 28) is built only for hosted builds (CMake `NEUPOD_UI_HIRES`, ON for host and y2, OFF for rp2350 and esp32s3), for the Y2 testbed. RP2350 image: flash and RAM byte-identical in size. The host display size is selectable with `NEUPOD_DISPLAY_SIZE=480x360`; new host tests `ui_metrics` (layout invariants of every profile), `hires_*` tours with a widget-tree layout audit (`NEUPOD_UI_AUDIT=1`), `hires_art_snapshot`.
- Bluetooth stage 2a, pairing and connection management, no audio yet ([Bluetooth](docs/bluetooth.md#pairing-and-the-connection-stage-2a)): SSP "just works" pairing started by Select on an audio device in Settings > Bluetooth > Devices (outgoing link, authentication, an SDP check that the device has an A2DP sink); the radio is never discoverable and is connectable and bondable only while that Pair action runs, for the chosen headset only (a connection filter rejects every other address and any stray key is deleted); Connect refuses to authenticate without a stored key; link keys stay in the SD-card settings store, never the internal flash. One saved headset (settings key `NPbd`: address, class, name), remembered only after a Pair that ended connected; a second headset replaces it and its key is deleted. `hal_bt_*` gains `pair`, `connect`, `disconnect`, `forget`, `link_state`, `link_reason`, `link_device`, `accepting` and the `HAL_BT_EVT_LINK` event, with states Disconnected, Pairing, Connecting, Connected and Failed (not found, pairing refused, no A2DP sink, timeout, link lost, stack refused); the host simulator pairs, fails and drops links per device so every path is tested. Settings > Bluetooth gets the headset's name and state, Connect / Disconnect / Cancel and Forget rows, and the right-hand pane shows its state. No reconnect at power-up (stage 3). Static RAM +444 B (free SRAM 15,320 to 14,876 B), flash +22,608 B.
- Wi-Fi and Bluetooth may run together (provisional, to be confirmed on the bench): one policy macro `NEUPOD_BT_WIFI_COEX_ALLOW` in `runtime_config.h` (default 1); no interlock is built.
- Host tests `bt_saved` (saved-headset record, damage handling, capacity, reboot, Pair rules, replacement and orphan-key clean-up, keys reaching the settings store) and an extended `bt_sim` (pairing state machine, every failure reason, discoverable only while pairing); host tour `--tour-bt` (ctest `bt_tour`) with Bluetooth snapshots.
- Setting "Resume playing" (default Off, key `NPra`): when On and the saved resume record says the player was playing, boot resumes playing instead of paused (a paused/stopped record still restores paused). Volume goes through the same restore policy ([Audio](docs/audio.md#resume-last-track-and-position)).
- Resume: the folder track, position and play/pause state are saved in the settings store (on pause, resume, Stop and track start) and restored at boot to Now Playing, paused at the saved place, in a queue built with the saved shuffle and repeat modes. There is no periodic position save while playing (the card is not written during playback): a power cut mid-track resumes at the last pause, Stop or track start. Exact frame for WAV, ALAC and FLAC with a seek table, nearest frame for MP3, start of track for FLAC without a seek table ([Audio](docs/audio.md#resume-last-track-and-position)).
- Settings store: 48 items (was 32; worst case with Wi-Fi, Bluetooth and the new settings is about 38) in about 9.9 KB of RAM, 3.0 KB less than the 32-item store was (the load no longer keeps two staged copies); file version 2, version-1 files still load. Older firmware sets a version-2 file aside and starts from defaults.
- Volume restore by output path ([Bluetooth](docs/bluetooth.md#volume-restore-policy)): the saved level comes back as saved on the same known path, capped at -30 dB otherwise. No path source exists yet, so restores are still capped until one is wired.
- Shuffle (off / songs) and repeat (off / one / all) for the folder queue (Settings, saved; small icons on Now Playing). Shuffle keeps the current track when switched on mid-play; repeat-one loops gaplessly when gapless is on; repeat-all wraps (re-shuffling, never opening with the track just played). .bss +532 B (queue order array + rollback snapshot). Main menu "Shuffle Songs" is still a placeholder.
- Auto-advance and gapless playback ([ADR-0016](docs/adr/0016-gapless-playback-and-play-queue.md)): playing a file from Files queues the rest of its folder; Now Playing Right/Left = next/previous (Previous restarts after 3 s), the status line shows "n of N", the end of the queue returns Now Playing to idle. Setting "Gapless playback" (default on): the next track is opened while the current one plays and the audio core switches decoders without closing the sink or draining the PCM ring; a different sample rate or channel count falls back to the ordinary stop/re-clock/start. Static RAM +6.4 KB (.bss +6,400 B, .data +152 B).
- Encoder delay and padding are trimmed: MP3 length now follows the LAME tag (as dr_mp3 plays it), ALAC/M4A uses the edit list or iTunSMPB; the displayed length matches.
- Host tests `gapless` (real core-1 loop and core-0 stream controller against a fake sink: sample-exact concatenation across WAV/FLAC/MP3/ALAC, track-change timing, rate-change fallback, skip and stop during a pending handoff) and `play_queue`; trim vectors via `tools/gen_trim_vectors.py`.

### Fixed
- Gapless review fixes: album art retargeted by a track change in the middle of a decode no longer publishes the old cover; a late gapless announcement after a skip no longer blocks gapless for the rest of the album; handoffs counted while a skip ramps down no longer advance the queue; a refused start request puts the queue position back; iTunSMPB length cannot wrap the fit check, and a misfit edit list falls back to iTunSMPB.
- MP3 files without an ID3v2 tag lost their first frame (and the Xing/LAME header): the frame scan treated offset 0 as "not found".

### Documentation
- ADR-0013 amendment of 4 Nov 2026: the Bluetooth PCM ring reuses the 32 KB I2S ring storage (the arena saving was spent in #17; no new static RAM), hi-res audio is decimated on core 1 (half-band FIR, TPDF dither), Wi-Fi is allowed during A2DP (provisional). Stage 2 is split into 2a (pairing, built), 2b (A2DP audio) and 2c (AVRCP) in the Bluetooth page, with a bench checklist for 2a.
- Design response of 3 Nov 2026 folded in: ADR-0012 (final-board codec, proposed; ES8311 mono check open), ADR-0013 (BT stage 2 RAM, PSRAM on the final board), ADR-0014 (OTA with bootrom A/B + TBYB), ADR-0015 (clock plan); ADR-0007 (no bare CYW43439 on the final board) and ADR-0011 (audio isolation is a requirement) updated; Audio Pack identified as PCM5100A + PAM8908 with GP22 = XSMT active low; approved volume-restore and secrets policies recorded as not built; requirements for antenna window, SD VDD load switch, BQ25895, haptic footprints, RAM budget lines; Wi-Fi order W2 → W4 → W3; new `docs/bench.md`.

### Changed
- Audio: ALAC decode hot paths run from SRAM (ALAC 16/44.1 load 38.4 to 30.2 %, worst packet 57 to 27.9 ms on the bench; 20-bit paths included, 32-bit left in flash); PCM ring 2048 to 4096 frames (`NEUPOD_PCM_RING_FRAMES` in `app/os/memory_budget.h`, 93 ms at 44.1 kHz, 43 ms at 96 kHz; ALAC 24/96 now plays with 0 underruns on the bench); codec arena 116 to 99 KB (net RAM -1 KB); 32-bit ALAC is rejected ("32-bit ALAC not supported") because it needs about 115 KB of arena. Vendored ALAC changes are listed in `app/third_party/alac/README.md`.
- RAM budget: FreeRTOS heap 112 to 80 KB and LVGL pool 96 to 72 KB (named constants in `app/os/memory_budget.h`); static RAM 481,588 to 436,480 bytes with BTstack and the settings store included (476,968 with Wi-Fi, secrets and the input widgets). The CYW43 worker task has 6 KB of stack. `md5.h` renamed `neupod_md5.h` (clash with BTstack's `md5.h`).

### Fixed
- I2S sink restart after a sample-rate change could leave the audio core stalled (DMA armed and busy, PIO requesting, no transfer): teardown now disables both DMA channels before aborting them and waits for the abort, clears the interrupt flags, and resets the state machine and FIFOs; open starts from a clean state and clears stale interrupt flags; a late interrupt no longer leaves the sibling channel at a zero count (`hal_audio.c`, `docs/audio.md`). Bench: 12 rate switches while playing plus paused and ALAC-to-WAV switches, no stall, `dma_underruns` 0 after reopen (was 2). Abort-bound expiry is counted and refuses the open; the late-interrupt re-arm restarts the sibling if both channels went idle.
- Out of memory panic on the first settings save: FatFs scratch memory is now capped at 4 KB and never panics (`app/storage/ff_alloc.c`, `docs/storage.md`); the lwIP tcpip thread stack is 2 KB (was 1 KB, 312 B free after a join); the `wifi:` monitor line shows the network being joined.
- Wi-Fi review fixes: no HAL calls under the service mutex (deadlock), link loss now detected, 32-bit deadline wrap, clock without reader spin and with NTP sanity, settings saved in two alternating files (no rename/unlink) and never while audio plays, 32 settings tags, secrets wiped after use, modal lifetime fixes, CI builds with mbed TLS.

### Added
- Album art on Now Playing (JPEG only, streamed; `docs/ui.md`, `docs/adr/0013-bluetooth-ram-psram.md` amendment): the embedded picture (MP3 APIC/PIC, FLAC PICTURE, MP4 `covr`), then `cover.jpg` / `folder.jpg` / `front.jpg` in the track's folder, decoded by vendored TJpgDec (R0.03, `app/third_party/tjpgd/`) in the storage task between audio top-ups into one 64 x 64 thumbnail (8 KB static) that LVGL scales to the 112 px box; a note glyph while loading and for PNG, progressive JPEG, damaged or missing art; the last album's art is kept, so the next track of the album shows it at once. Tag readers record picture offset, length and kind (a PNG of any size is skipped without reading it). Host tests: decoder (4:2:0, 4:4:4, 4:2:2, grey, restart markers, odd sizes, progressive refused, hostile input), source order, tag parsing, Now Playing snapshots (`ctest art`, `art_tour`, `art_snapshot`).
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
