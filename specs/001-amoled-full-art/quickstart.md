# Quickstart: validating full album art on the AMOLED boards

How to prove the feature works, end to end. Commands per target are in `.claude/skills/neupod/reference/workflows.md`; run
heavy builds through `~/bin/neupod-buildlock`. Interfaces: [contracts/console-and-build.md](contracts/console-and-build.md).
Budget to compare against: [plan.md section 5](plan.md#5-ram-budget-per-board-constitution-iv).

## Prerequisites

- A microSD card with tagged music: at least 10 albums with baseline JPEG art (some embedded, some `cover.jpg` /
  `folder.jpg`), one album with no art, one with a PNG cover, one with a progressive JPEG.
- The baseline sizes of `main` (research R10, or `llvm-size -A` of the CI artifacts of the merge base).

## 1. Host (no hardware)

```sh
~/bin/neupod-buildlock scripts/build.sh host
ctest --test-dir build/host --output-on-failure                   # unchanged suite, all pass, counts checked
# the boards' UI options on the host (new configuration, research R9):
~/bin/neupod-buildlock scripts/build.sh host-amoled                # or the cmake line in tasks T004
ctest --test-dir build/host-amoled --output-on-failure        # all of it; at least the art tests and the library tours
```

Expected: every test passes in both configurations; the `lib-coverflow*` tour screens at 600x450 match between them.

## 2. Builds and sizes

```sh
~/bin/neupod-buildlock scripts/build.sh rp2350 --clean
~/bin/neupod-buildlock scripts/build.sh rp2350-amoled241 --clean
# esp32s3: the docker command in workflows.md
```

Expected: Pico 2 W `llvm-size -A build/rp2350/platforms/rp2350/neupod.elf` identical, section by section, to the baseline.
amoled241: `.psram_noload` about 1.29 MB; free space above the heap at least 11,452 B. ESP32-S3: the app fits its partition;
the map shows `g_slot` in `.ext_ram.bss`.

## 3. Bench, each board

1. Flash (dev boards, approved), open the console, power-cycle.
2. Boot log shows `psram: 8192 KB ...` and no `FAILED`.
3. Play a track with art: Now Playing shows it sharp; screenshot compared with the simulator at 600x450 (SC-001).
4. Music > Albums: the album header shows art; open Cover Flow, step through 10 albums, then step back: the earlier tiles
   appear without a new decode (decode counter on the console, SC-003); tiles fill within 2 s of stopping (SC-002).
5. Music > Playlists: play three tracks, Recently Played lists them; add to On-The-Go, restart, it is still there (User
   Story 3).
6. Five minutes of browsing with a track playing: monitor line shows 0 under-runs (SC-006), minimum heap and internal free
   within the plan's gates (SC-005), LVGL pool peak recorded.
7. RP2350 board: 10 warm restarts (`picotool reboot`, reset button, `picotool load -f -x`): PSRAM found every time (SC-007).

## 4. Failure path (RP2350 board, debug build only)

Build with `-DNEUPOD_PSRAM_FORCE_FAIL=ON` (debug aid, tasks T020): the board prints `boot: psram FAILED: forced` every 2 s,
shows it on screen, and starts no service (SC-007).

Record the results, with the date, in `docs/rp2350-amoled241.md`, `docs/esp32s3.md` and the project memory `bench-status.md`.
