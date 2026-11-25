# ADR-0013: Bluetooth stage 2 RAM on the testbed, PSRAM on the final board

> **12 Nov 2026:** the Pico 2 W's 520 KB SRAM is no longer a budget limit (the target is frozen as a compile check; the product target is the RP2350B with PSRAM): see the amendment in [ADR-0001](0001-rp2350-phase1-platform.md#amendment-12-november-2026-the-pico-2-w-is-frozen-the-product-target-is-the-rp2350b-with-psram). The memory figures below stay as the record of what the testbed ran.

## Status

Accepted (design response, 3 Nov 2026).

## Context

Bluetooth is the primary output path. Stage 2 (A2DP source, AVRCP target) needs an SBC encoder and a
core 1 → core 0 PCM ring ([Bluetooth](../bluetooth.md#stage-2-design-notes-2a-built-2b-and-2c-not-built)). The Pico 2 W has
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

## Amendment 4 Nov 2026: the Bluetooth PCM ring reuses the I2S ring storage (funding change)

This amends **Funded by** and the **RAM budget** line of the decision above; the original text is left as it was. It was agreed with the owner while
planning stage 2a ([Bluetooth](../bluetooth.md)).

- **What changed.** The two funding sources no longer exist. The codec arena was sized down in #17 and that saving was spent there, and free static
  SRAM on the testbed is now about **15.5 KB** (15,320 B measured on `main` at 4944981, `__HeapLimit - __end__`; 14,876 B with stage 2a). A new 32 KB budget cannot be found.
- **New decision.** The **core 1 to core 0 Bluetooth PCM ring uses the same 32 KB storage as the I2S PCM ring** (`NEUPOD_PCM_RING_FRAMES` = 4096 frames, 32 KB). The two outputs are exclusive (audio goes to
  I2S **or** to Bluetooth, never both), so one set of bytes serves whichever sink is active; the ring is emptied and its counters reset when the output path changes. This adds **no new static RAM**.
  The 32 KB of the decision above is therefore **withdrawn** as new memory. What stage 2b still needs of its own (the SBC encoder state and its working buffers) is small, is **measured when 2b is built**, and comes out of the free SRAM.
- **Decimation on core 1.** Hi-res audio is decimated on core 1 by a 2:1 or 4:1 half-band FIR to 44.1 / 48 kHz, with 24 to 16 bit TPDF dither, before the samples enter the ring (SBC carries 16 bit at up to 48 kHz).
  Core 1 keeps the ADR-0011 rules (no RTOS, no heap, closure-checked); the FIR's state and coefficients are measured with the closure check in 2b.
- **Wi-Fi during A2DP is allowed.** Provisional, like the Wi-Fi-with-Bluetooth rule of stage 2a: one policy macro (`NEUPOD_BT_WIFI_COEX_ALLOW`), no interlock; bench testing may reverse it.
- **Consequences.** (1) The LVGL render-buffer release while the display sleeps is **no longer needed** for funding; it stays a possible later saving, not a dependency of stage 2. (2) Changing the output path at run time needs a quiet ring
  (stopped or paused, as the audio engine already does to change sink or rate). (3) A Bluetooth link that drops mid-track must pause or switch the output back to I2S before the ring is handed over.
  (4) The closure check has to cover the new ring state when 2b adds it.

## Amendment 25 Nov 2026: PSRAM in use on the AMOLED dev boards (UI-25)

The decision above names the 8 MB APS6404L PSRAM as where album art goes on the final board. The Waveshare RP2350-Touch-AMOLED-2.41
has that part (and the ESP32-S3-Touch-AMOLED-2.41 has 8 MB of octal PSRAM), so the big-RAM player now runs there. Design and evidence:
[specs/001-amoled-full-art](https://github.com/StuartF303/neuPod/blob/main/specs/001-amoled-full-art/plan.md) (research R1 to R11). The lead confirmed the scope and the failure
policy on 25 Nov 2026.

- **What goes to PSRAM:** the 24-album art cache (1,230,144 B), the playlist buffers, the play counts and the play queue: the cold,
  big buffers of `NEUPOD_ART_HIRES`. Hot buffers (PCM ring, library pages, display DMA strips) stay internal; core 1 touches none of
  the moved buffers.
- **How:** ESP32-S3 by object file (`main/linker.lf`, `extram_bss`, zeroed by the IDF); RP2350 by section name, the macro
  `NEUPOD_PSRAM_BSS` (`app/include/psram_bss.h`, empty on every other build) into the Pico SDK's `.psram_noload`, zeroed by the HAL.
  The SDK's `hardware_psram` brings the chip up; we add a warm-start reset before it and compute the timing again after the
  clock drops from 150 to 125 MHz (otherwise CS stays low 9.2 us, over the 8 us limit).
- **Failure policy:** no fallback. A board whose PSRAM is missing or fails its check does not start the runtime and says why
  (console every 2 s; red screen on the RP2350 board). The ESP32-S3's IDF already stops the boot without PSRAM.
- **RAM budget:** internal RAM does not grow on either board; PSRAM use and the linked figures are in
  [docs/rp2350-amoled241.md](../rp2350-amoled241.md#psram-and-the-big-ram-player-ui-25) and [docs/esp32s3.md](../esp32s3.md).
  The Pico 2 W image is unchanged.
