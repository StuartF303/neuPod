# ADR-0006: Storage — microSD over 4-bit SDIO, exFAT

## Status

Proposed

## Context

The original 1.8" ZIF hard drive is removed. The library may reach 100k+ tracks and 2 TB;
the card must mount on a PC over USB without special software; the display and the card must
not stall each other during playback.

## Decision

- One microSD socket, SDXC up to 2 TB, driven as **SDIO 4-bit** from a PIO program (GP2–GP7
  on the Phase 1 testbed), FatFs with exFAT enabled.
- SPI mode is the fallback only (same wires: CLK = SCK, CMD = MOSI, DAT0 = MISO, DAT3 = CS).
- The card is on its own bus, never shared with the display's SPI.
- The library index is cached (PSRAM on the final board, or an on-card database) so browsing
  does not rescan the card.

## Consequences

- 4-bit SDIO gives roughly 4× the throughput of SPI mode at the same clock and higher clocks;
  needed for library scans and album-art loads while audio keeps playing.
- The PIO driver needs DAT0–DAT3 on consecutive GPIOs and internal pull-ups on CMD and DAT.
- A 2 TB card stays on the compatibility test list; controller quirks are where iFlash users
  report trouble.
- The display's own SD slot on the Adafruit breakout is not used.
