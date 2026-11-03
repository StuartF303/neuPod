# ADR-0014: Over-the-air updates with bootrom A/B partitions

## Status

Accepted (design response, 3 Nov 2026). Two review points are listed under Consequences.

## Context

Wi-Fi W4 is OTA. The RP2350 bootrom supports a partition table, A/B application partitions and
try-before-you-buy (TBYB) booting, so a custom bootloader is not needed.

## Decision

- **Order of Wi-Fi work:** W2 (device HTTP API + C# companion; library sync is why Wi-Fi exists) → W4 OTA →
  W3 podcasts through the companion.
- **Layout:** bootrom partition table, A/B app partitions, TBYB.
  - Pico 2 W (4 MB): 2 × 1.75 MB app slots + 0.5 MB reserved (settings cache / scratch).
  - Final board: 16 MB **W25Q128** (JLCPCB Basic part): 2 × 4 MB slots, rest reserved.
- **Rules:**
  - the image downloads to SD as `/.neupod/update.bin`, checked by CRC32 and a signature before flashing;
  - flashing only with playback stopped and core 1 parked in SRAM under multicore lockout, **never** while
    core 1 executes from XIP;
  - the new slot boots under TBYB and is only made permanent after its boot checks pass.

## Consequences

- Image size limit on the testbed is 1.75 MB; today's `text` is about 1.12 MB.
- **Review point 1:** the partition table itself occupies flash (at least one 4 KB sector at the start), so
  2 × 1.75 MB + 0.5 MB does not fit 4 MB exactly; the reserved area shrinks by that amount.
- **Review point 2:** "settings cache" in internal flash conflicts with the current rule that settings and
  link keys live on the SD card and never in internal flash (core 1 runs from XIP;
  [Bluetooth](../bluetooth.md#why-the-sd-card-holds-the-pairing-data-and-never-the-internal-flash)). Any
  write there must follow the same lockout rule as OTA, or the area stays scratch-only until decided.
- Signature scheme and key storage are not decided here (ties to secure boot, which is a later decision).
- **RAM budget:** download buffer through the SD write path (size set in W4); no new static RAM on core 1.
