# microSD storage and its tests

Phase 1 reads and writes the library on a microSD card over **4-bit SDIO**, driven from a PIO
program (not SPI). The firmware talks to it through `hal_storage.h` (card, raw sectors, mount)
and the portable file API `nfs.h` (files and directories); see
[ADR-0006](adr/0006-storage-microsd-sdio.md) and [ADR-0008](adr/0008-card-format.md).

## Card format decision

- **Project format: exFAT, 128 KB clusters**, aligned to the card's allocation unit. Reasons:
  files over 4 GB, SDXC cards are exFAT natively, and large clusters keep the FAT small and
  reads sequential.
- **FAT32 must mount as a fallback.** The Phase 1 card arrived as FAT32 and every test except
  the format test runs on it unchanged.
- **The layout** is an MBR with one exFAT partition (type 07) starting at the card's allocation
  unit and a data area aligned the same way, which is what the SD Association formatter makes.
- **Do not reformat the card on a PC or phone.** Operating-system formatters pick their own
  cluster size and ignore the card's alignment. If you must, use the SD Association's *SD
  Memory Card Formatter*, which keeps the alignment. The firmware only formats when you ask
  (test 9).
- 128 KB clusters waste space on small files (cover art) but make audio-heavy libraries fast;
  the numbers are in ADR-0008 and should be revisited with the measured results below.

## Wiring

From `wiring-phase1.md` (3.3 V, DAT0-DAT3 must be consecutive GPIOs, CLK is DAT0 - 2):

| Signal | GPIO |
|--------|------|
| CLK | GP2 |
| CMD | GP3 |
| DAT0 | GP4 |
| DAT1 | GP5 |
| DAT2 | GP6 |
| DAT3 | GP7 |

## Library and resource use

- Driver: carlk3's `no-OS-FatFS-SD-SDIO-SPI-RPi-Pico`, release **v3.6.2** (Apache-2.0), a git
  submodule in `lib/`. After cloning run `git submodule update --init --recursive`. Its FatFs
  is used on the RP2350 (the host build uses the vendored FatFs); the FatFs configuration is
  `platforms/rp2350/ffconf/ffconf.h`, kept identical to the host's by a build-time check.
- **PIO / DMA:** the SD driver owns `pio1` (two state machines), two DMA channels claimed
  dynamically, and shares `DMA_IRQ_0`. The Wi-Fi chip and the click-wheel receiver land on
  `pio2`. If something else has claimed a state machine on `pio1` the card refuses to start with
  a clear error instead of misbehaving.
- **Clock:** the PIO program needs 4 PIO cycles per SD clock, so the SDIO clock is
  `clk_sys / (4 x divider)` = 125 MHz / (4 x 2) = **15.625 MHz** by default (an integer divider;
  see the observations below). It can be changed at run time (`hal_storage_set_bus_clock`).
- **High Speed:** the card says 25 MHz in default-speed mode (CSD TRAN_SPEED 0x32). Faster
  clocks need the card switched to High Speed (command CMD6). The driver library never sends
  it; the HAL does (`hal_storage_enable_high_speed`) using the library's low-level calls, and
  refuses clocks above 25 MHz until it has succeeded.
- **Write safety:** writes are locked by default. Only the tests that need them unlock writes,
  for as long as they run, and always lock again and delete their files (even after a failure
  or `q`). In the runtime firmware one other piece of code opens the lock: the settings save, for the length of one save and only for
  paths under `/.neupod` (an nfs write scope; [Bluetooth](bluetooth.md)). Before unlocking, a test reads 16 MB of the card twice and compares; test files are
  created exclusively (an existing file is never overwritten; the test stops and tells you).
- **Write-fault latch:** the driver library ignores the card's status after a write and can leave
  the card waiting for data after a failed write. The HAL therefore checks the card status after
  every write, and after any failure refuses all further writes until the card is re-initialised;
  test cleanup re-initialises and remounts first, and if that is impossible it leaves writes
  locked and lists the files to delete by hand.
- **Free space** is computed with a full scan of the FAT (the stored FAT32 hint is not trusted),
  so it is slow on a big FAT32 volume; the time is printed.
- **Stack:** the main stack is 4 KB for the storage test (the most core 0's scratch RAM bank can
  hold); the lowest free stack seen is printed after each test.

## Running the tests

```bash
git submodule update --init --recursive
export PICO_SDK_PATH=$HOME/pico/pico-sdk
scripts/build.sh rp2350
scripts/flash.sh rp2350 storage_test
scripts/console.sh      # type the keys here (the PC's serial console), not on the Pico
```

| Key | Test | Writes? |
|-----|------|---------|
| `1` | Detect: card identification, CID/CSD/SD status, sector 0 | No |
| `2` | Mount: file system type, cluster size, label, serial, total/free space | No |
| `3` | List: recursive listing of the root, depth 2, capped at 200 entries | No |
| `4` | Integrity: 16 MB pseudo-random file round trip across a remount, then a 2 MB raw-sector round trip inside a file the test allocated | Under `/neupod-test/` |
| `5` | Sequential speed at 512 B, 4 KB, 32 KB, 128 KB blocks, read and write | Under `/neupod-test/` |
| `6` | 2000 random 4 KB reads (latency average/min/max/p99), with and without fast seek | Allocates under `/neupod-test/` |
| `7` | Streaming: 32 KB every 20 ms for 10 s, counts missed deadlines | Allocates under `/neupod-test/` |
| `8` | Clock sweep: steps the SDIO clock up until an error, suggests a setting. Run it on its own; it needs `/neupod-test` not to exist beforehand | Raw sectors inside one file under `/neupod-test/` |
| `9` | **Format** the card: MBR plus one exFAT partition (needs typing `YES`) | **Erases everything** |
| `a` | Runs 1-7 and prints a summary; never runs 8 or 9 | as above |
| `q` | Stop the running test | |
| `c` | Clean up leftover test files: lists `/neupod-test/`, then (after `y`) deletes only `pattern.bin`, `raw.bin`, `speed.bin`, `sweep.bin` and the directory if empty; never part of `a` | Deletes under `/neupod-test/` |

Before any write test the card must have at least the test's size plus 64 MB free. Everything is
written under `/neupod-test/`, which is created if missing and removed again if it was created
by the test. A full run of 1-8 takes a few minutes (estimates: test 4 about 15 s, test 5 about
40 s, test 6 about 10 s, test 7 about 15 s, test 8 about 20 s); test 9 on a 64 GB card can take a
minute or more.

Test 8 allocates one contiguous file with the file system at the safe clock, then at each higher
clock does only a read-only soak (4 MB read twice plus 100 status queries) followed by raw
writes inside that file; the file system is not touched until the clock is back to normal. A
failure at a test clock cannot damage the file system, only that file.

Test 9 shows the card name and capacity, the current volume (type, label, space used), warns
that all data will be erased, including other partitions such as a Linux root file system, and
only continues if you type exactly `YES` and press Enter. It writes a partition table with one
exFAT partition starting at the card's allocation-unit boundary, creates exFAT with 128 KB
clusters aligned the same way, and labels it `NEUPOD`; then it remounts and shows test 2's
output.

On the PC, `scripts/build.sh host` builds the same menu as `build/host/hal/host/storage_test`
over a disk image (`NEUPOD_DISK_IMAGE`) and a files directory (`NEUPOD_FS_ROOT`, default
`build/host/fsroot`). Mount, raw-sector, clock and format tests are skipped there.

## Confirmed card facts (stage 1, on the bench)

| Item | Value |
|------|-------|
| Card | SanDisk SU64G, SDXC, 124,735,488 sectors (63.86 GB), writable |
| CID | MID 0x03, OID "SD", PRV 8.0, PSN 0x1AC15256, date 2012-07 |
| Bus | 4-bit at 12.5 MHz; CSD v2, TRAN_SPEED 0x32 (25 MHz) |
| SD status | Speed class C10, UHS U1, allocation unit 4096 KiB |
| Layout | MBR, partition 1 type 0x0C (FAT32) |
| Seen by FatFs (before test 9) | Raspberry Pi OS card: 256 MB FAT32 partition "bootfs" (2 KB clusters); the Linux root partition is invisible to FatFs |
| After test 9 (2026-11-02) | MBR, one partition type 0x07 at sector 8192; exFAT, 128 KB clusters, label NEUPOD, 63.85 GB free; format took 656 ms |
| Timing | init 122 ms, one sector read 6 ms |

## Measured results (bench, 2026-11-02, SanDisk SU64G, Phase 1 breadboard)

MB = 1,000,000 bytes. Test 5 uses a pre-allocated 32 MB file; random reads are 2000 × 4 KB.

| Measurement | FAT32 "bootfs", 2 KB clusters, 12.5 MHz | FAT32, 25 MHz | **exFAT, 128 KB clusters, 15.625 MHz (current)** |
|-------------|------|------|------|
| Integrity (test 4): 16 MB file + 2 MB raw | PASS, 0 mismatches | PASS, 0 mismatches | PASS, 0 mismatches |
| Sequential write, best | 4.39 MB/s (32 KB) | 8.45 MB/s (32 KB) | 6.50 MB/s (128 KB) |
| Sequential read, best | 2.84 MB/s | 3.82 MB/s | 7.28 MB/s (128 KB) |
| Random 4 KB read, plain seek, avg | 21.0 ms | 18.7 ms | 1.27 ms (p99 1.59 ms) |
| Random 4 KB read, fast seek, avg / p99 | 1.47 / 2.00 ms | — | 1.26 / 1.55 ms |
| Streaming 32 KB / 20 ms, 10 s | 0 misses, headroom 20% | 0 misses, headroom 46% | **0 misses, worst 4.9 ms, headroom 76%** |
| Clock sweep (test 8) | all steps to 31.25 MHz OK (HS) | leaving HS failed; later run failed at 25 MHz on response CRC | 10.4 and 15.6 MHz OK; HS switch failed (response CRC) |

Observations and open items:

- **exFAT with 128 KB clusters fixed the slow reads.** On FAT32 with 2 KB clusters FatFs splits
  every transfer at cluster boundaries (at most 4 sectors per disk call), and the driver pays a
  read command plus a stop command per call. With 128 KB clusters, reads went from 3.8 to
  7.3 MB/s, and random reads no longer need fast seek (1.27 ms either way).
- **Streaming is comfortable:** 32 KB every 20 ms (1.64 MB/s, about 2.7× FLAC 24/96) with 76 %
  headroom.
- **Library bug, worked around:** carlk3 v3.6.2 `rp2040_sdio_rx_start()` writes its DMA list
  terminator one entry past `dma_blocks[SDIO_MAX_BLOCKS * 2]` for a 256-block read, wedging the
  driver. FAT32 with 2 KB clusters never triggers it; exFAT with 128 KB clusters does on every
  128 KB read. The HAL splits reads into chunks of at most 128 blocks (`guarded_read` in
  `hal_storage.c`), and a failed read latches the write fault so the card is re-initialised
  before any further write. Worth reporting upstream.
- **Command-response CRC errors (open):** the same responses are misread identically every time:
  CMD13 calculated 0x7c vs received 0x7e, CMD17 0xcc vs 0xce, CMD55 0x64 vs 0x66. They always
  differ in the last CRC7 bit. They appear around clock changes and the CMD6 (High Speed)
  sequence, at fractional dividers (12.5, 20.8, 25 MHz) and once at 15.625 MHz during CMD6. They
  never appeared during data tests 4–7 at 15.625 MHz. They look deterministic (a response-sampling
  or decoding issue in the PIO/driver path) rather than noise. Not yet root-caused. Because of
  them, the default is 15.625 MHz in default-speed mode and High Speed is not used.
- **The 10 MB/s USB mass-storage target is not reachable** at clk_sys 125 MHz with this PIO
  program (4 PIO cycles per SD clock → 31.25 MHz maximum, High Speed only). Best measured:
  6.5 MB/s write, 7.3 MB/s read. Open item, no decision (options: higher clk_sys, a different
  SDIO PIO program, or accept a slower sync).
- **Leaving High Speed mode** needs care: the card is quieted at the current clock (open write
  closed, not busy), the clock lowered, and only then is the card reset and re-identified, with up
  to three tries; the sweep test fails if the card cannot be brought back.

## Failure checklist

| Symptom | Likely cause / action |
|---------|-----------------------|
| Card ignores the first identification after a debugger reset | A reset mid-transfer can leave the card in a data state; a power cycle always fixes it. `hal_storage_init()` makes up to three attempts; before the 2nd and 3rd it sends four CMD12 (stop transmission: no data, harmless to an idle card, ends an open read or write-multiple) at the 400 kHz identification clock, reads each response, waits 50 ms and logs `storage: card did not identify, wake-up attempt N`. Nothing is written. A missing card still fails in well under a second (about 0.65 s in all, 0.5 s more than with a single attempt); it is called once at boot, never in a loop |
| Test 1: NO_CARD or init timeout | Card not fully inserted, 3.3 V missing, CLK/CMD/DAT wiring (GP2-GP7), no pull-up on CMD, card not powered |
| CRC errors, or tests pass at 12.5 MHz but fail higher | Clock too fast for the wiring: lower the clock, shorten wires, check pull-ups on CMD and DAT0-3 |
| Test 2: NO_FILESYSTEM | No FAT/exFAT volume (blank card, unusual partition layout), or the file system configuration is wrong (sector size not 512, exFAT disabled) |
| Test 2 slow | Free-space scan on a big FAT32 volume is slow by nature; it is printed separately |
| Slow random reads (test 6) | Small clusters or a fragmented file; compare plain and fast seek |
| Streaming misses deadlines | Read time varies: look at the worst read time; lower the clock only if errors appear, otherwise raise it |
| Test 4 mismatches | Bus errors that slipped past the CRC, or a bad card; repeat at a lower clock |
| Test 8 stops at 25 MHz / "High Speed switch failed" | The card refused High Speed, or a command response failed its CRC (see the open item above); keep the default |
| SDIO "Timed out waiting for data block" on large reads | The library's 256-block DMA bug: make sure reads go through the HAL's chunked `guarded_read` |
| Card unreadable on a PC after test 9 | Check the partition table with a PC tool: one partition, type 07, starting at the allocation-unit boundary; see ADR-0008 |
| "writes locked until re-init" or "write fault" | A write failed or the card status after a write was bad; the card was reset, check wiring and clock |
| A test says a file already exists | An earlier run was interrupted; delete `/neupod-test` on a PC and run again |
