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

## Amendment 3 Nov 2026: album art on the testbed (JPEG only)

The owner decided to have album art now, on the Pico 2 W, instead of waiting for the PSRAM board. This amends the last
bullet of the decision above ("Artwork decode ... PSRAM-only"); the original text is left as it was.

- **Scope:** baseline JPEG only, **streamed** from the card (embedded picture in MP3 / FLAC / MP4, then `cover.jpg`, `folder.jpg`,
  `front.jpg` in the track's folder). PNG and progressive JPEG show the note placeholder until the PSRAM board. The whole
  picture is never in RAM: the decoder works one MCU row at a time and only a 64 x 64 thumbnail is kept.
- **Where it runs:** core 0 only, in the storage task (FatFs is not re-entrant, so the storage task stays its only user), between
  audio top-ups; it sleeps 1 ms after every 2 ms of work (the storage task outranks the input and UI tasks) and gives way at once
  to any queued request. Core 1 and its closure are untouched.
- **Stays PSRAM-only:** the HTTP client, AAC, and PNG art.
- **RAM budget line (testbed):** static **+8,552 bytes** (8,192 thumbnail + 360 state; RP2350 `.bss` 476,232 to 484,784) and **+17.4 KB
  of flash** (decoder and glue; `.text` 1,221,776 to 1,239,184). Transient, from the FreeRTOS heap for the length of one decode and
  freed after: 4,096 (decoder tables) + a strip of 1 to 4 KB + 0.5 KB, at most about 8.6 KB, or the picture is skipped
  (placeholder) if the heap cannot give it. The static cost comes out of the roughly 27 KB that were free before, which leaves
  about 18 KB; stage 2's 32 KB above is still funded by the arena sizing and the LVGL buffer release, not by this.
- **Consequences:** `art_service` must not be called from core 1 or hold the storage task for more than a few milliseconds without
  yielding; the 64 x 64 thumbnail is shown 1.75 times larger by LVGL (soft edges, accepted); the thumbnail buffer is a candidate
  to be reclaimed (or to become a bigger PSRAM-backed image) when the PSRAM board arrives.
