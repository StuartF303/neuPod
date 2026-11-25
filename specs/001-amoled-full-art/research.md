# Research: Full album art on the AMOLED dev boards (UI-25)

Phase 0 of [plan.md](plan.md). Each item: what was found (with where), the decision, the rationale, the alternatives.
Figures marked **measured** come from the CI artifacts of `main` at 2d95476 (firmware run 45282854323, `llvm-size -A` and
`llvm-nm -S` on the ELFs); figures marked **estimate** are computed from the headers and must be replaced by linked figures
in the PR (tasks T022 to T024).

## R1. Does each board have PSRAM, how much, and what uses it today?

| | ESP32-S3-Touch-AMOLED-2.41 (`esp32s3`) | RP2350-Touch-AMOLED-2.41 (`rp2350-amoled241`) |
|---|---|---|
| PSRAM | 8 MB octal (ESP32-S3R8), `docs/esp32s3.md` | 8 MB quad APS6404L-class on QMI CS1 = GPIO47 (`boards.md`; Waveshare's "2 MB" is wrong; the bring-up probe read the EID as 64 Mbit) |
| Brought up by | ESP-IDF at start-up (`CONFIG_SPIRAM=y`, octal, 80 MHz, memtest off) | **nothing**: "PSRAM ... is not initialised by the firmware" (`docs/rp2350-amoled241.md`, "What is not done") |
| Used today for | codec arena 99 KB, compressed ring 32 KB, LVGL pool 192 KB, UI render buffers 72 KB (all static, `main/linker.lf` by object file, `extram_bss`), display shadow 527 KB (heap, `heap_caps_aligned_alloc(... MALLOC_CAP_SPIRAM)`): about **922 KB** | nothing (`.psram_load` / `.psram_noload` are 0 bytes in the ELF) |
| Internal RAM left | about 76 KB of internal heap free after boot, 32 KB at the lowest (`docs/esp32s3.md`, bench 14 Nov 2026) | **11,452 B** between the end of `.heap` (0x2007d344) and the end of main SRAM (0x20080000) (**measured**) |
| PSRAM failure today | the IDF aborts start-up (`CONFIG_SPIRAM_IGNORE_NOTFOUND` is not set, and `extram_bss` needs the PSRAM anyway) | n/a |

**Decision**: both boards qualify for the big-RAM player (ADR-0022: "where a board has the memory (PSRAM), this is a gap to
close"). **Finding that shapes the plan**: neither board can take the feature's extra buffers in internal RAM (the RP2350
board has 11 KB spare; the ESP32-S3 has 32 KB at its lowest), so everything big goes to PSRAM on both, and the internal-RAM
budget is "no net growth".

## R2. What does `NEUPOD_ART_HIRES` switch on besides the art?

`grep -rn NEUPOD_ART_HIRES app/` shows the option is the firmware's "big-RAM" switch, not only art:

| Gated | Off (Pico 2 W) | On | Static cost on a 32-bit build |
|---|---|---|---|
| `art.h` `ART_MAX_DIM` / `ART_CACHE_N` | 64 px, 1 slot (8,248 B **measured** `g_slot`) | 160 px, 24 slots | 24 x 51,256 B = **1,230,144 B** (`art_slot_t`: 56 B of header + 51,200 B of pixels) |
| `art_service.c` browse state (`ART_CACHE_N > 1`) | none | wanted / pinned sets, failed list | about 0.7 KB (estimate) |
| Cover Flow (`ui_coverflow.c`), library album header art (`ui_library.c`), `ui_frame.c` | compiled out | built | flash; small statics |
| `playlist_service.h` `PLS_AVAILABLE` | 0 | Playlists | about 23.9 KB: pool 16,384 + paths 3,072 + names 960 + 2 x 29 x 48 B rows + offsets (estimate) |
| `plays_store.h` `PLAYS_MAX` | 0 | 128 | 128 x 184 B + inbox 6 x 176 B = about 24.6 KB (estimate) |
| `ratings_store.h` `RATINGS_MAX` | 24 | 128 | +1,248 B table, +520 B file image = **+1,768 B** (estimate) |
| `play_queue.h` `PLAY_QUEUE_POOL_BYTES` | 3,072 | 8,192 | +5,120 B (the whole `g_q` is 3,872 B **measured** today) |
| `library.h` `LIB_PAGES` | 4 | 5 | +2,048 B in `lib_mem_t`, which is asserted to stay within the Files buffer + 1 KB, so 0 to +2 KB (estimate) |

**Decision**: turn the one option on for both AMOLED boards and budget all of it (spec Clarifications, FR-002).
**Rationale**: the owner's backlog item says "turn `NEUPOD_ART_HIRES` on"; ADR-0022 forbids differences without a hardware
reason; a new option that splits art from playlists would create a third kind of player.
**Alternatives**: split the option into `NEUPOD_ART_HIRES` and `NEUPOD_PLAYLISTS` (rejected: more build combinations to test,
and the boards can afford both); art only, by hand-editing the gates (rejected: same).

## R3. How does the cache get into PSRAM on the ESP32-S3?

**Found**: the established way is `platforms/esp32s3/main/linker.lf`, which maps the `.bss` of whole object files of
`libmain.a` to `extram_bss` (needs `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`, already set). Its header says placement
is done "by object file, not with attributes in `app/`".

**Decision**: add four entries: `art_service`, `playlist_service`, `plays_store`, `play_queue`. The IDF zeroes `extram_bss`
at start-up, so the statics keep their C semantics.
**Rationale**: one line per object, no `app/` change, the convention already reviewed and on the bench.
**Alternatives**: `heap_caps_malloc(MALLOC_CAP_SPIRAM)` at start-up (rejected: turns the statics into pointers in shared code
for no gain on a board that cannot boot without PSRAM anyway); `EXT_RAM_BSS_ATTR` on the variables (rejected: contradicts
the file's convention).

## R4. How does the cache get into PSRAM on the RP2350 AMOLED board?

**Found** (Pico SDK 2.3.1, the CI image's version, `.github/ci/Dockerfile`):

- `hardware_psram` (`src/rp2_common/hardware_psram`) brings PSRAM up in `runtime_init` (`PICO_RUNTIME_INIT_PSRAM` "11080",
  before `main`) when the board header defines `PICO_PSRAM_CS_PIN` and `PICO_PSRAM_SIZE_BYTES`; with
  `PICO_AUTO_DETECT_PSRAM_SIZE=1` it also reads the chip's ID and only then uses it. `psram_is_available()`,
  `psram_get_size()`, `psram_check_address()` report the result.
- The linker scripts have a PSRAM region (`pico_psram_region.ld`, sized from `PICO_PSRAM_SIZE_BYTES`, 0 when unset: that is
  why the Pico 2 W ELF has empty `.psram_*` sections) and two sections: `.psram_load` (initialised, copied from flash) and
  `.psram_noload` (`NOLOAD`, **not zeroed**). Variables go there with `__in_psram(group)` / `__uninitialized_psram(group)`
  (`pico/platform/sections.h`), which put them in `.psram_initialised.<group>` / `.psram_uninitialised.<group>`.
- With variables in PSRAM, XIP is set to bus-fault on any access beyond the detected size.

**Decision**:

1. The board header `platforms/rp2350/boards/amoled241/waveshare_rp2350_touch_amoled_2.41.h` defines `PICO_PSRAM_CS_PIN 47`,
   `PICO_PSRAM_SIZE_BYTES (8 * 1024 * 1024)` (with the `pico_cmake_set_default` line so CMake sizes the region), and
   `PICO_AUTO_DETECT_PSRAM_SIZE 1` (so a missing chip reads as size 0 instead of being assumed). `hardware_psram` is linked
   only into the amoled241 image.
2. The big arrays are marked with one portable macro, `NEUPOD_PSRAM_BSS`, defined in `app/os/memory_budget.h`: on a build
   that defines `NEUPOD_PSRAM_SECTION` (only the amoled241 build) it is
   `__attribute__((section(".psram_uninitialised.neupod")))`; everywhere else it is empty, so the host, the Y2, the
   ESP32-S3 and the Pico 2 W compile exactly what they do today. Marked: `g_slot` (`art_service.c`), the playlist pool and
   tables (`playlist_service.c`), the plays table and inbox (`plays_store.c`), the queue pool (`play_queue.c`).
3. `hal_system_init()` zeroes `__psram_start__` to `__psram_end__` once the PSRAM has passed its check (R6), before any
   runtime code runs, so the marked statics start at zero as C promises. About 1.29 MB at 62.5 MHz quad: roughly 40 ms of
   boot (estimate; measured in T025).

**Rationale**: the SDK's own mechanism, which does not depend on the order of the SDK's linker include files; the macro
costs nothing where it is empty; the list of marked variables is short and greppable.
**Alternatives**: (a) override the SDK's `sections_psram.incl` with `pico_add_linker_script_override_path()` and list object
files, as the ESP32-S3 does (rejected: the default `.bss` statement may claim those input sections first, so `.bss` would
need an `EXCLUDE_FILE` override too, two copied SDK files, BSD-3 notices, and a silent fallback to internal RAM if a
pattern stops matching); (b) a run-time bump allocator over 0x11000000 and pointers in `app/` (rejected: changes shared code
and every user of these statics for no behavioural gain, since a failed PSRAM stops the board anyway, R6); (c) our own
driver from the bring-up probe instead of `hardware_psram` (kept only for the warm-start reset step, R5).
**Convention note**: this is the first section attribute in `app/`. The ESP32-S3 `linker.lf` header gets one line saying the
RP2350 AMOLED build uses `NEUPOD_PSRAM_BSS` and why.

## R5. PSRAM bring-up hazards on the RP2350 (clock change, warm start)

**Found**:

- The SDK brings PSRAM up in `runtime_init` at the SDK's boot clock (150 MHz), then `hal_system_init()` calls
  `set_sys_clock_khz(NEUPOD_SYS_CLOCK_KHZ = 125 MHz)`. `psram_configure_params()` computes `MAX_SELECT` in units of 64
  system clocks: 18 at 150 MHz (8,000 ns / (64 x 6.67 ns)). Left at 18 at 125 MHz it is 18 x 64 x 8 ns = **9.2 us**, above
  the chip's 8 us maximum CS-low time (tCEM, needed for its self-refresh). The divisor (2) stays valid (62.5 MHz).
- `hardware_psram`'s size detection reads the ID in single-SPI mode. The bring-up probe (`testbed/amoled241/bringup/src/psram.c`)
  first sends "quad off" (0xF5, in quad) and a reset (0x66, 0x99) "in case an earlier run left the chip in quad mode (a
  picotool reboot does not reset it)". The SDK relies on the boot ROM's XIP exit sequence; whether that also leaves the
  PSRAM's QPI mode is not documented.

**Decision**: right after `set_sys_clock_khz()`, with interrupts off and before core 1 starts, call
`psram_configure_params(PICO_DEFAULT_PSRAM_MAX_FREQ, PICO_DEFAULT_PSRAM_MAX_SELECT, PICO_DEFAULT_PSRAM_MIN_DESELECT)` and
`psram_reinitialize()` (unless T003 finds `set_sys_clock_khz()` in SDK 2.3.1 already re-times PSRAM). Add a warm-start
reset: a `runtime_init` function ordered before "11080" that sends quad-off and reset on CS1, from our own probe driver
(MPL-2.0, written from the datasheets), so detection works after any reset. Bench test SC-007 (10 warm restarts) decides
whether the step can be dropped; it stays unless the test shows it is not needed.
**Rationale**: the timing violation would cause rare data corruption, not a crash: exactly the kind of bug that is found
months later. The reset step is a few lines already proven on this board.
**Alternatives**: `PICO_RUNTIME_SKIP_INIT_PSRAM=1` and bring PSRAM up ourselves after the clock change (kept as the
fallback if `psram_reinitialize()` misbehaves: one call site either way); run the system at 150 MHz (rejected: changes the
QSPI display and SD timing of a working board).

## R6. What happens with no PSRAM, or when PSRAM init fails?

| Board | Behaviour |
|---|---|
| ESP32-S3 | Unchanged from today: the IDF aborts start-up and prints why on the USB console. The board already cannot run without PSRAM (codec arena, LVGL pool, display shadow live there). |
| RP2350 AMOLED | New fatal check. `hal_system_init()` records whether `psram_is_available()` and `psram_check_address(__psram_end__ - 1)` both hold; only then does it zero the section. The runtime asks `hal_psram_ok()` (amoled241 only, `NEUPOD_PSRAM_SECTION` builds) at the start of boot, **before any service that touches a marked variable starts**. If false: the console prints `boot: psram FAILED: <reason>` every 2 s (the USB port may be opened late), the display shows the same line in the boot splash's text line once the display is up, and the boot stops there. No service starts, so nothing touches the missing memory (an access would bus-fault). |
| Pico 2 W, host, Y2 | No change; no check compiled in. |

**Decision**: stop with a clear message (spec FR-007). **Rationale**: there is no internal RAM to fall back on (R1, 11 KB),
the ESP32-S3 already behaves this way, and a dev board whose soldered PSRAM fails has a hardware fault that should be loud.
**Alternatives**: (a) degrade at run time to no cache and no playlists (rejected: every marked module would need a run-time
"have memory" branch, and the 160 px build cannot fit even one 51 KB slot internally); (b) panic through `neupod_panic()`
(rejected as the only path: it writes UART0, which this board does not bring out, so the owner would see nothing on USB).

## R7. RP2350 XIP cache sharing (performance risk)

**Found**: on the RP2350 the 16 KB XIP cache serves both flash (code and read-only data) and PSRAM. Cover Flow draws up to
five 50 KB thumbnails per frame from PSRAM through the CPU (LVGL reads them to render into the internal-RAM partial
buffers), which will evict cached code.

**Decision**: start with the cached window (0x11000000) and measure: full-frame time on Cover Flow steps, Now Playing redraw,
and the null sink's under-run counter (FR-009, SC-006). If a step's frame time exceeds 50 ms, or under-runs appear, give
LVGL the uncached alias of the pixel pointer (0x15000000 + offset) for the browse tiles, so pixel reads stream past the cache;
this is one address transform in the RP2350 build, not a UI change.
**Rationale**: measure before optimising; the fallback is cheap and local.
**Alternatives**: copy tiles into internal RAM before drawing (rejected: no internal RAM, R1).

## R8. LVGL pool on the RP2350 AMOLED board

**Found**: the amoled241 build keeps the 32-bit default pool of 72 KB in internal RAM (`app/os/memory_budget.h`). The ESP32-S3
build, with the same 600x450 profile, moved its pool to PSRAM at 192 KB because "72 KB ran out on the Music screens"
(`docs/esp32s3.md`). Cover Flow adds seven tile objects to the Albums screen.

**Decision**: measure the pool peak on the board through the library tour including Cover Flow (T032). If the peak exceeds
85% of 72 KB, move the pool to PSRAM at 192 KB on amoled241 too (LVGL's builtin pool `work_mem_int` takes
`LV_ATTRIBUTE_LARGE_RAM_ARRAY` from `app/ui/lv_conf.h`; set it to `NEUPOD_PSRAM_BSS` on amoled241), which frees 72 KB of internal RAM. Both outcomes are budgeted in plan.md.
**Rationale**: the pool in PSRAM has the same XIP-cache cost as R7 on every widget access; avoid it unless needed.
**Alternatives**: always move it (rejected for now: performance risk without evidence it is needed).

## R9. Has the combination "one compiled profile + `NEUPOD_ART_HIRES`" ever been built?

**Found**: no. `NEUPOD_ART_HIRES` has only ever been on together with `NEUPOD_UI_HIRES` (hosted builds carry all profiles and
choose at run time). The AMOLED builds compile one profile as a constant (`NEUPOD_UI_PROFILE_600`). Code under
`#ifdef NEUPOD_ART_HIRES` in `ui_library.c`, `ui_coverflow.c`, `ui_frame.c` may assume the run-time profile.

**Decision**: add a host configuration with exactly the boards' UI options (`-DNEUPOD_UI_HIRES=OFF -DNEUPOD_ART_HIRES=ON
-DNEUPOD_UI_PROFILE=600x450`) and run the library tours (including `lib-coverflow*`) and the art tests in it, locally and in
CI (T004, T005). If the host cannot build without `NEUPOD_UI_HIRES`, fix that first; it is the cheapest place to find
layout bugs before the boards.

## R10. Pico 2 W unchanged

**Found** (**measured**, `main` at 2d95476): `.text` 0xeb148 (962,888), `.rodata` 0x6b774 (440,180), `.data` 0x565c (22,108),
`.bss` 0x79fc0 (499,648), `.heap` 0x800, end of heap 0x2007ffc0: 64 B to the end of main SRAM.

**Decision**: every change is behind `NEUPOD_BOARD_AMOLED241`, `NEUPOD_PSRAM_SECTION` (amoled241 only) or the IDF build. The
macro is empty on the Pico 2 W, `hardware_psram` is not linked there, the board header is not used there, and the boot check
is compiled out. The PR compares `llvm-size -A` of the Pico 2 W `neupod.elf` before and after: every allocated section must
be the same size (T023).

## R11. Documentation and decisions to write (constitution VIII)

- **ADR**: amend [ADR-0013](../../docs/adr/0013-bluetooth-ram-psram.md) (its decision already names the 8 MB APS6404L PSRAM as
  where art goes on the final board) with "PSRAM in use: placement on each board, the failure policy, the budget". Not a new
  ADR: the decision is the one ADR-0013 anticipated.
- ADR-0022's "Known gap" paragraph: mark closed with the date and the PR.
- `docs/esp32s3.md` memory table, `docs/rp2350-amoled241.md` (PSRAM section; remove from "What is not done"), `docs/ui.md`
  "Thumbnails and cache (NP-5)" (which builds), `art.h` and the top-level `CMakeLists.txt` comments, `docs/backlog.md` (UI-25
  done, NP-5 row), `CHANGELOG.md`.
- `LICENSE.md` / Licences screen: no new third-party code (`hardware_psram` is part of the Pico SDK 2.3.1 row, already
  listed, BSD-3-Clause).
