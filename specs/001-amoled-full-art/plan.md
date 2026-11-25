# Implementation Plan: Full album art on the AMOLED dev boards

**Branch**: `feat/ui-25-amoled-art` | **Date**: 2026-11-25 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/001-amoled-full-art/spec.md` (backlog UI-25)

## Summary

Turn the existing big-RAM build option (`NEUPOD_ART_HIRES`) on by default for the two Waveshare AMOLED boards, so they get
the 160 x 160 art, the 24-album cache, browse art, Cover Flow and the playlists that share the option, with every large
buffer in PSRAM. On the ESP32-S3 the PSRAM is already up and the buffers go there by object file through `linker.lf`. On the
RP2350 AMOLED board PSRAM is brought up for the first time, with the Pico SDK 2.3.1 `hardware_psram` module, re-timed for
the firmware's 125 MHz clock, and the buffers go to its `.psram_noload` section through one macro that is empty on every
other build. A board whose PSRAM fails stops at start-up with a message. Internal RAM does not grow on either board; the
Pico 2 W image does not change by one byte. Details and evidence: [research.md](research.md).

## Technical Context

**Language/Version**: C11 (firmware), CMake 3.13+; Pico SDK 2.3.1 (arm-none-eabi GCC from the CI image); ESP-IDF v5.3.2

**Primary Dependencies**: LVGL (UI, Cover Flow), FreeRTOS SMP, FatFs, TJpgDec (art decode); new use of the SDK's
`hardware_psram`

**Storage**: microSD (unchanged; the playlists, plays and ratings files already exist under `/.neupod`)

**Testing**: host `ctest` (unit tests incl. `test_art`, `test_art_service`, the UI tours incl. `lib-coverflow*`), a new host
configuration with the boards' UI options, CI builds of all five targets, bench runs on both boards (console + screenshots)

**Target Platform**: ESP32-S3-Touch-AMOLED-2.41 (`esp32s3`), RP2350-Touch-AMOLED-2.41 (`rp2350-amoled241`); unchanged: host,
Y2, Pico 2 W

**Project Type**: embedded firmware (shared `app/`, per-platform HAL)

**Performance Goals**: a Cover Flow step redraws in at most 50 ms on both boards; tiles fill within 2 s after stepping stops
(SC-002); 0 audio under-runs (SC-006)

**Constraints**: no net internal-RAM growth on either board; Pico 2 W byte-identical section sizes; boot time +50 ms at most
on the RP2350 board (PSRAM zeroing)

**Scale/Scope**: 4 platform build files, 1 board header, 2 HAL files, 5 `app/` files marked or gated, 1 boot hook, docs

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design (below).*

| Principle | Check | Status |
|---|---|---|
| I. One player UI | Closes an ADR-0022 gap: the boards get the same art, cache, Cover Flow and playlists as the simulator. No board-specific UI code; the only differences are where memory lives (a build option, per board). | Pass |
| II. Firmware is the reference | No behaviour changes in `app/`; the phone app's art vectors (`scripts/app/gen_art_vectors.py`) already use the 160 px build. | Pass |
| III. Owner's music and devices safe | No card writes beyond the existing `/.neupod` files. Flashing the dev boards is approved; not done in the planning step. No partition change (the ESP32-S3 image grows within its app partition, checked in T002). | Pass |
| IV. Fit the hardware | RAM budget per board below; Pico 2 W deltas reported and required to be zero. | Pass (budget below) |
| V. Sound not compromised | Art decode stays on core 0 in the storage task with its existing yields; core 1 untouched (closure check still runs). Under-runs measured (SC-006). | Pass |
| VI. Open, private | No new third-party code; the PSRAM warm-start step is our own MPL-2.0 code from the bring-up probe; no vendor demo code. | Pass |
| VII. Verified, reviewed, merged | Host tests + new host config + CI builds + bench on both boards, then an independent review. | Pass (tasks) |
| VIII. Decisions written down | ADR-0013 amendment for PSRAM use; board docs with dated bench results; CHANGELOG, backlog. | Pass (tasks) |

**Post-design re-check**: still passes. One convention change is flagged for review rather than a violation: the first
section attribute in `app/` (`NEUPOD_PSRAM_BSS`, empty except on the RP2350 AMOLED build), justified in research R4.

## Design

### 1. Build option

- Top-level `CMakeLists.txt`: `NEUPOD_ART_HIRES` defaults ON when `NEUPOD_BOARD STREQUAL "amoled241"` (as well as for the
  hosted builds); its comment names the AMOLED boards. `-DNEUPOD_ART_HIRES=OFF` still gives the old image for comparison.
- `platforms/esp32s3/CMakeLists.txt` + `main/CMakeLists.txt`: the same cache option, default ON, passed as
  `NEUPOD_ART_HIRES=1` to the component (the IDF project does not see the top-level option).
- `NEUPOD_PSRAM_SECTION=1` is defined for the amoled241 build only (`platforms/rp2350/CMakeLists.txt`).

### 2. PSRAM placement per board

| Board | Mechanism | What goes there |
|---|---|---|
| ESP32-S3 | `main/linker.lf`, `extram_bss` by object file (zeroed by the IDF) | `art_service`, `playlist_service`, `plays_store`, `play_queue` (whole objects' `.bss`) |
| RP2350 AMOLED | `NEUPOD_PSRAM_BSS` = `__attribute__((section(".psram_uninitialised.neupod")))` on the marked variables; the SDK's `.psram_noload` section in the 8 MB PSRAM region; zeroed by `hal_system_init()` | `g_slot` (art cache), the playlist pool and tables, the plays table and inbox, the play-queue pool |
| Pico 2 W, host, Y2 | macro empty, no linker change | nothing moves |

### 3. RP2350 PSRAM bring-up (new, `platforms/rp2350`)

1. Board header: `PICO_PSRAM_CS_PIN 47`, `PICO_PSRAM_SIZE_BYTES (8 * 1024 * 1024)` (+ `pico_cmake_set_default` line),
   `PICO_AUTO_DETECT_PSRAM_SIZE 1`. `hardware_psram` linked for amoled241 only.
2. Warm-start reset: a `runtime_init` function ordered before the SDK's PSRAM init sends quad-off and reset on CS1 (from
   the bring-up probe's driver, our code).
3. `hal_system_init()`: after `set_sys_clock_khz(125 MHz)`, with interrupts off: `psram_configure_params(...)` +
   `psram_reinitialize()` (research R5: `MAX_SELECT` computed at 150 MHz would hold CS low 9.2 us at 125 MHz, above the 8 us
   limit). Then check `psram_is_available()` and `psram_check_address(__psram_end__ - 1)`; if both hold, zero
   `__psram_start__`..`__psram_end__` and remember "PSRAM OK".
4. Console line at boot (FR-008): `psram: 8192 KB at 62500 kHz, neupod section 1257 KB` (format in
   [contracts/console-and-build.md](contracts/console-and-build.md)). The ESP32-S3 prints the same summary from
   `esp_psram_get_size()` and the `extram_bss` bounds (`platforms/esp32s3/main/hal/board.c`), so both boards report alike.

### 4. Failure path (FR-007, research R6)

- ESP32-S3: unchanged (the IDF aborts start-up and says why on the console).
- RP2350 AMOLED: `hal_psram_ok()` (declared in `hal/include/hal_system.h`, implemented in the RP2350 HAL; only referenced
  under `#ifdef NEUPOD_PSRAM_SECTION`) is checked in the runtime's boot once the display is up (so the message can be
  shown) and before any service that touches a marked variable starts (T019 confirms the order). False: `boot: psram FAILED: <reason>` on the console every 2 s and in the boot splash's text line,
  and boot stops there. Pico 2 W, host and Y2 compile none of this.

### 5. RAM budget per board (constitution IV)

Static figures for 32-bit builds. "Measured" = from the CI ELFs of `main` at 2d95476; "estimate" = from the headers
(research R2), to be replaced by linked figures in the PR.

**ESP32-S3-Touch-AMOLED-2.41** (512 KB internal SRAM, 8 MB PSRAM)

| | Internal RAM | PSRAM |
|---|---|---|
| Today | about 76 KB heap free after boot, 32 KB lowest (bench) | about 922 KB used (codec 99, ring 32, LVGL 192, UI buffers 72, display shadow 527) |
| Art cache (24 x 51,256 B) | -8,248 B (the 64 px slot leaves) | +1,230,144 B |
| `art_service` browse state | 0 (whole object in PSRAM) | +about 0.7 KB |
| Playlists, plays | 0 | +about 48.5 KB |
| Play-queue object (pool 3 KB to 8 KB) | -3,872 B (whole object moves) | +about 9 KB |
| Ratings 24 to 128 | +1,768 B | 0 |
| Library reader 5th page | 0 to +2,048 B | 0 |
| Cover Flow / album header UI statics | about +0.5 KB | 0 |
| **Net static** | **about -9.9 to -7.8 KB (internal RAM is freed)** | **+about 1.29 MB, total about 2.2 MB of 8 MB** |
| Transient (art decode scratch, `os_alloc`) | typical +5 KB (13 KB vs 8 KB per decode), worst 25 KB for a 5120 px source; blocks over 16 KB go to PSRAM (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`) | as left |
| **Gate** | internal heap free after boot >= 76 KB, lowest >= 32 KB (no worse than today) | <= 2.4 MB used |

**RP2350-Touch-AMOLED-2.41** (520 KB SRAM, 8 MB PSRAM)

| | Internal RAM | PSRAM |
|---|---|---|
| Today (measured) | `.data` 17,708 B, `.bss` 492,668 B; **11,452 B** free above the heap | 0 (not initialised) |
| Art cache | -8,248 B | +1,230,144 B |
| `art_service` browse state | +about 0.7 KB (stays internal: only `g_slot` is marked) | 0 |
| Playlists, plays | 0 | +about 48.5 KB |
| Play-queue pool | -3,072 B (the 3 KB pool leaves; the 8 KB pool is in PSRAM) | +8,192 B |
| Ratings 24 to 128 | +1,768 B | 0 |
| Library reader 5th page | 0 to +2,048 B | 0 |
| Cover Flow / album header UI statics, `hardware_psram` state | about +0.6 KB | 0 |
| **Net static** | **about -8.3 to -6.2 KB (free rises from 11.4 KB to about 18 KB)** | **+about 1.29 MB of 8 MB (16%)** |
| Transient (art decode scratch from the 80 KB FreeRTOS heap via `os_alloc`, 6 KB reserve) | typical +5 KB, worst +17 KB over today; a decode that cannot get it shows the placeholder (existing `ART_E_MEMORY`) | 0 |
| LVGL pool contingency (research R8: only if the measured peak > 85% of 72 KB) | -73,728 B | +196,608 B (192 KB) |
| **Gate** | free above the heap >= 11,452 B (no worse than today); FreeRTOS heap minimum-ever free >= 10 KB in the monitor line during a Cover Flow run | <= 1.5 MB used (1.7 MB with the LVGL contingency) |

**Flash**: both boards +60 to 100 KB estimate (Cover Flow, playlists UI and service, plays store, `hardware_psram`):
RP2350 AMOLED uses about 1.06 MB of 16 MB; the ESP32-S3 app partition headroom is checked in T002.

**Pico 2 W**: required delta **0 B** in every allocated section (`.text` 962,888, `.rodata` 440,180, `.data` 22,108, `.bss`
499,648 today, 64 B free above the heap). Checked in T023; any non-zero delta blocks the PR.

### 6. Performance checks (research R7, R8)

On each board, through the Cover Flow tour and 5 minutes of Now Playing + browsing: frame time of a Cover Flow step, the
monitor line's under-run count and minimum heap, the LVGL pool peak. RP2350 only: if a step exceeds 50 ms or under-runs
appear, switch the browse tiles' pixel pointers to the uncached PSRAM alias (R7).

## Risks

| Risk | Effect | Mitigation |
|---|---|---|
| PSRAM left in QPI mode across a warm reset (R5) | RP2350 board stops at boot until power-cycled | warm-start reset step; SC-007 10/10 warm restarts |
| SDK `psram_reinitialize()` misbehaves after the clock change | corruption or fault | fallback: `PICO_RUNTIME_SKIP_INIT_PSRAM=1` and bring-up after the clock change; pattern test at boot in the debug build |
| XIP cache contention (R7) | slower UI on the RP2350 | measure; uncached alias for tiles |
| LVGL pool too small at 600x450 with Cover Flow (R8) | allocation failure on Albums | measure; PSRAM pool contingency (budgeted) |
| First build of "one profile + ART_HIRES" (R9) | compile or layout bugs | host config with the boards' options, tours in CI, before any bench time |
| ESP32-S3 app grows past its partition | link/flash failure | check partition headroom (T002); no repartitioning without the owner (constitution III) |

## Project Structure

### Documentation (this feature)

```text
specs/001-amoled-full-art/
├── spec.md
├── plan.md                         # this file
├── research.md                     # Phase 0
├── data-model.md                   # Phase 1
├── quickstart.md                   # Phase 1: validation guide
├── contracts/console-and-build.md  # Phase 1: build option, HAL call, console lines
├── checklists/requirements.md
└── tasks.md                        # /speckit-tasks
```

### Source Code (repository root)

```text
CMakeLists.txt                                    # NEUPOD_ART_HIRES default for amoled241
scripts/build.sh                                  # host-amoled configuration (research R9)
.github/workflows/firmware.yml                    # host-amoled job, test counts checked
app/os/memory_budget.h                            # NEUPOD_PSRAM_BSS macro
app/runtime/art_service.c                         # g_slot marked
app/runtime/playlist_service.c                    # pool and tables marked
app/settings/plays_store.c                        # table and inbox marked
app/runtime/play_queue.c                          # pool marked
app/runtime/boot.c (or runtime.c)                 # PSRAM check, NEUPOD_PSRAM_SECTION builds only
app/include/art.h, app/ui/lv_conf.h               # comments; LV_ATTRIBUTE_LARGE_RAM_ARRAY only if R8 contingency
hal/include/hal_system.h                          # hal_psram_ok() declaration (guarded)
platforms/rp2350/CMakeLists.txt                   # NEUPOD_PSRAM_SECTION, hardware_psram (amoled241)
platforms/rp2350/boards/amoled241/waveshare_rp2350_touch_amoled_2.41.h   # PSRAM pin and size
platforms/rp2350/boards/amoled241/psram_amoled.c  # warm-start reset, re-time, check, zero, hal_psram_ok()
platforms/rp2350/hal/hal_system.c                 # calls into psram_amoled.c (amoled241 only)
platforms/esp32s3/CMakeLists.txt, main/CMakeLists.txt, main/linker.lf
platforms/esp32s3/main/hal/board.c                # psram: summary line
docs/ (esp32s3.md, rp2350-amoled241.md, ui.md, backlog.md, adr/0013, adr/0022), CHANGELOG.md
```

**Structure Decision**: no new directories. The PSRAM driver lives with the board's other HAL files in
`platforms/rp2350/boards/amoled241/`.

## Complexity Tracking

No constitution violations. One convention change, recorded for review:

| Change | Why needed | Simpler alternative rejected because |
|---|---|---|
| A section-attribute macro in `app/` (`NEUPOD_PSRAM_BSS`) | the Pico SDK places PSRAM data by section name | object-file linker overrides need two copied SDK linker includes and silently fall back to internal RAM if a pattern stops matching (research R4) |
