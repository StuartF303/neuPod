# ADR-0008: Card format: exFAT with 128 KB clusters

*(Numbered like the other ADRs, `000N-name.md`; the request called it ADR-0008-card-format.)*

## Status

Proposed

## Context

ADR-0006 puts the library on a microSD card read through FatFs, and the card must also mount
on a PC. The Phase 1 test card (SanDisk SU64G, SDXC, 63.9 GB) arrived formatted FAT32. Choices
are FAT32, exFAT, or a mix. Constraints: SDXC cards are exFAT by specification; some lossless
albums and DSD files exceed 4 GB; the player reads in large sequential runs and a library scan
touches many directories; a cluster size that is too small makes the FAT large and slow to
scan, one that is too big wastes space on small files such as cover art.

## Decision

- **The project format is exFAT**, 128 KB clusters, one FAT, data area aligned to the card's
  allocation unit (AU) as reported in the SD Status register (4 MiB on the Phase 1 card; 4 MiB
  assumed if the card does not report one).
- **FAT32 must still mount** and work for every read and write path as a fallback, because
  cards often arrive that way and the player must not refuse them. Nothing in the player may
  depend on exFAT-only features; files over 4 GB simply fail to be created on FAT32.
- **Layout: MBR with one exFAT partition** (type 0x07) starting at the card's allocation-unit
  boundary and covering the rest of the card in whole units; the exFAT data area is aligned the
  same way. This matches the SD Association formatter. FatFs `f_fdisk` cannot place a partition
  (it always starts at sector 63), so the firmware writes the MBR itself and then lets `f_mkfs`
  format partition 1 (FatFs multi-partition support is on; logical drive 0 auto-detects).
- Formatting happens only on explicit request (storage test 9, typed confirmation) and the
  player never formats on its own.
- Users are advised not to reformat the card on a PC or phone, or to use the SD Association's
  formatter, which also keeps the AU alignment.

## Consequences

- 128 KB clusters: large files (audio, albums) fill clusters almost fully, the FAT stays small
  (a 64 GB card needs about 0.5 M entries), and sequential reads stay mostly contiguous.
  Cover art and small files waste up to 128 KB each; acceptable because the library is
  dominated by audio. Revisit if the measured scan or space numbers say otherwise.
- Formatting erases the whole card including other partitions (for example a Linux root file
  system on a Raspberry Pi OS card); the confirmation prompt says so. A PC should see a normal
  single-partition card; this still has to be confirmed on the bench.
- Free-space queries (`f_getfree`) on FAT32 scan the whole FAT and are slow; exFAT keeps an
  allocation bitmap and is faster. The player must not call it in time-critical paths.
- The 4 GB file limit and weaker power-loss behaviour of FAT32 remain for fallback cards.

## Evidence (bench, 2026-11-02)

Formatted with `hal_storage_format()` (test 9) on a 64 GB SanDisk SDXC: MBR partition at sector
8192, exFAT, 128 KB clusters, 656 ms. Compared with the same card's 2 KB-cluster FAT32 volume,
sequential reads rose from 3.8 to 7.3 MB/s and random 4 KB reads from 18.7 ms to 1.27 ms average
(see [storage](../storage.md)). 128 KB clusters also exposed a 256-block read bug in the SD
library, now worked around in the HAL.

