# ADR-0013: Bluetooth stage 2 RAM on the testbed, PSRAM on the final board

## Status

Accepted (design response, 3 Nov 2026).

## Context

Bluetooth is the primary output path. Stage 2 (A2DP source, AVRCP target) needs an SBC encoder and a
core 1 → core 0 PCM ring ([Bluetooth](../bluetooth.md#stage-2-design-notes-not-built)). The Pico 2 W has
520 KB SRAM and no PSRAM; after Wi-Fi W1 the static RAM is about 477 KB, so there is no free 32 KB.

## Decision

- **Priority:** stage 2 starts immediately after the Audio Pack tone test and MP3/ALAC playback on the bench.
- **Budget:** 32 KB for the SBC encoder state and the PCM ring, on core 0.
- **Funded by:**
  1. sizing the codec arena as max(FLAC, MP3, ALAC), each **measured** at its worst case, instead of the
     fixed 116 KB; the arena never goes below the measured FLAC 24/96 need;
  2. releasing LVGL's second render buffer while the display sleeps (BT playback is mostly screen-off),
     and re-acquiring it on wake.
- **Final board:** 8 MB **APS6404L** PSRAM on QSPI CS1. Artwork decode, the HTTP client and AAC are
  PSRAM-only features; on the Pico 2 W testbed they stay out of scope.

## Consequences

- The arena size becomes a build-time constant derived from measured figures; the unit tests that check
  worst-case sizes become the source of the number.
- LVGL must be able to run with one render buffer (partial refresh only) and switch at sleep/wake without
  a redraw glitch.
- **RAM budget (testbed):** +32 KB stage 2; −(116 KB − measured max) arena; −one LVGL draw buffer while
  asleep. To be filled with linked figures when built.
