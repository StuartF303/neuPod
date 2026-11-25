---

description: "Task list for UI-25: full album art on the AMOLED dev boards"
---

# Tasks: Full album art on the AMOLED dev boards

**Input**: Design documents from `specs/001-amoled-full-art/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/console-and-build.md, quickstart.md

**Tests**: the spec requires measurable checks (SC-001 to SC-007) and the constitution (VII) requires tests that really run.
Tests here are the existing host suites run in a new configuration, size comparisons, and bench checks; no new unit test
framework.

**Organization**: one compile-time option switches on all three user stories at once, so most of the code work is
foundational (Phase 2). The story phases verify and tune each story on the two boards.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: US1 Now Playing art, US2 Cover Flow and browse art, US3 playlists and the rest of the big-RAM player

---

## Phase 1: Setup

**Purpose**: baselines and the host configuration that finds layout bugs before any bench time

- [ ] T001 Record the baseline: `llvm-size -A` and `llvm-nm -S --size-sort` of `neupod.elf` for `rp2350` and `rp2350-amoled241` from the merge base's CI artifacts, and the ESP32-S3 `idf.py size` summary from a local build (`~/bin/neupod-buildlock`); keep them in the PR description draft (research R10, plan section 5)
- [ ] T002 [P] Check the ESP32-S3 app partition headroom against `platforms/esp32s3/partitions.csv` and the baseline `neupod.bin` size (plan "Flash"); stop and ask the owner if the estimate (+100 KB) does not fit (constitution III: no repartitioning without the owner)
- [ ] T003 [P] Read Pico SDK 2.3.1 in the CI image: does `set_sys_clock_khz()` re-time PSRAM; where `sections_psram.incl` sits relative to `.bss` in `memmap_default.incl`; how `runtime_init_setup_psram` orders against a custom `runtime_init` function. Write the answers into research.md R4/R5
- [ ] T004 Add a host configuration with the boards' UI options (`-DNEUPOD_UI_HIRES=OFF -DNEUPOD_ART_HIRES=ON -DNEUPOD_UI_PROFILE=600x450`) as `host-amoled` in `scripts/build.sh`; fix whatever stops the host from building or running without `NEUPOD_UI_HIRES` (research R9)
- [ ] T005 Run the art tests and the library tours (incl. `lib-coverflow*`) in `build/host-amoled`; fix layout or compile issues in `app/ui/ui_library.c`, `app/ui/ui_coverflow.c`, `app/ui/ui_frame.c` that only show with one compiled profile; then add the `host-amoled` configuration to `.github/workflows/firmware.yml` with test counts checked (constitution VII)

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: the option, the PSRAM bring-up and the placement. After this phase both boards build with the big-RAM player.

**⚠️ CRITICAL**: no story verification can begin until this phase is complete

### Build option

- [ ] T006 Default `NEUPOD_ART_HIRES` ON for `NEUPOD_BOARD=amoled241` in `CMakeLists.txt`, and rewrite its comment (AMOLED boards, PSRAM) (spec FR-001, FR-002, FR-010; contracts section 1)
- [ ] T007 [P] Add the `NEUPOD_ART_HIRES` cache option (default ON) to `platforms/esp32s3/CMakeLists.txt` and pass `NEUPOD_ART_HIRES=1` to the component in `platforms/esp32s3/main/CMakeLists.txt` (spec FR-001, FR-010; contracts section 1)

### ESP32-S3 placement

- [ ] T008 [P] Add `art_service`, `playlist_service`, `plays_store`, `play_queue` as `extram_bss` entries in `platforms/esp32s3/main/linker.lf`, update its header comment (and note that the RP2350 AMOLED build uses `NEUPOD_PSRAM_BSS` instead, research R3/R4) (spec FR-004)

### RP2350 AMOLED PSRAM bring-up

- [ ] T009 Define `PICO_PSRAM_CS_PIN 47`, `PICO_PSRAM_SIZE_BYTES (8 * 1024 * 1024)` with its `pico_cmake_set_default` line, and `PICO_AUTO_DETECT_PSRAM_SIZE 1` in `platforms/rp2350/boards/amoled241/waveshare_rp2350_touch_amoled_2.41.h` (spec FR-006, research R4)
- [ ] T010 In `platforms/rp2350/CMakeLists.txt`, for amoled241 only: link `hardware_psram`, define `NEUPOD_PSRAM_SECTION=1` for the app and HAL targets, add `boards/amoled241/psram_amoled.c` to `hal_rp2350_core` (contracts section 1)
- [ ] T011 Write `platforms/rp2350/boards/amoled241/psram_amoled.c`: the warm-start reset (`runtime_init` function before the SDK's PSRAM init: quad-off, reset-enable, reset on CS1, taken from `testbed/amoled241/bringup/src/psram.c`), `psram_amoled_after_clock()` (re-time with `psram_configure_params()` + `psram_reinitialize()` with interrupts off; skip if T003 found the SDK does it), the check (`psram_is_available()`, `psram_check_address(__psram_end__ - 1)`), the zeroing of `__psram_start__`..`__psram_end__`, `hal_psram_ok()` / `hal_psram_reason()`, and the `psram:` console line (spec FR-006, FR-008; data-model.md "PSRAM state"; contracts sections 2 and 3). The `runtime_init` function must live in this same object as `hal_psram_ok()`: the file is linked from a static library, and an object with no referenced symbol is dropped, which would silently drop the reset step
- [ ] T012 Call `psram_amoled_after_clock()` from `hal_system_init()` right after `set_sys_clock_khz()` in `platforms/rp2350/hal/hal_system.c`, inside `#ifdef NEUPOD_BOARD_AMOLED241` (research R5)
- [ ] T013 Declare `hal_psram_ok()` and `hal_psram_reason()` in `hal/include/hal_system.h` under `#ifdef NEUPOD_PSRAM_SECTION` (contracts section 2)

### Placement macro and marked buffers

- [ ] T014 Add `NEUPOD_PSRAM_BSS` to `app/os/memory_budget.h`: `__attribute__((section(".psram_uninitialised.neupod")))` when `NEUPOD_PSRAM_SECTION` is defined, empty otherwise, with a comment pointing at research R4 (spec FR-004)
- [ ] T015 [P] Mark `g_slot` with `NEUPOD_PSRAM_BSS` in `app/runtime/art_service.c` (data-model.md "Album-art cache slot")
- [ ] T016 [P] Mark the playlist pool, path, name, row, offset and state arrays in `app/runtime/playlist_service.c` (data-model.md "Other moved buffers")
- [ ] T017 [P] Mark the plays table and inbox in `app/settings/plays_store.c` (data-model.md "Other moved buffers")
- [ ] T018 [P] Mark the play-queue name pool in `app/runtime/play_queue.c` (data-model.md "Other moved buffers")

### Failure path

- [ ] T019 In the runtime boot (`app/runtime/boot.c` or `app/runtime/runtime.c`, wherever services are first started), under `#ifdef NEUPOD_PSRAM_SECTION`: if `!hal_psram_ok()`, print `boot: psram FAILED: <reason>` every 2 s and show it in the boot splash's text line, and start no service (spec FR-007, research R6). Confirm no marked variable is touched earlier (grep the init order)
- [ ] T020 Add a debug-only `NEUPOD_PSRAM_FORCE_FAIL` CMake option (amoled241) that makes the check report "forced", for the failure-path test in quickstart section 4, in `platforms/rp2350/CMakeLists.txt` and `psram_amoled.c`
- [ ] T021 [P] ESP32-S3: print the same `psram:` summary line from `esp_psram_get_size()` and the `extram_bss` bounds in `platforms/esp32s3/main/hal/board.c` (contracts section 3, FR-008)

### Build and size gate

- [ ] T022 Build all five targets (`host`, `host-amoled`, `rp2350`, `rp2350-amoled241`, `esp32s3`) through `~/bin/neupod-buildlock`; run the full host `ctest` in both host configurations with counts checked; the core-1 closure and app-heap checks must pass on both RP2350 images
- [ ] T023 Compare the Pico 2 W `neupod.elf` section sizes with T001: every allocated section identical, or stop and fix (spec FR-005, SC-004)
- [ ] T024 Record the amoled241 and ESP32-S3 linked figures (internal static, `.psram_noload` / `.ext_ram.bss`, flash) against the plan's budget tables; any internal growth beyond the gates blocks the bench work (spec FR-011)

**Checkpoint**: both boards build with the big-RAM player, all buffers placed, Pico 2 W unchanged.

---

## Phase 3: User Story 1 - Sharp album art on Now Playing (Priority: P1) 🎯 MVP

**Goal**: Now Playing on both boards shows the 160 px art as the simulator does.

**Independent Test**: play a track with art on each board, compare with the simulator's 600x450 screenshot (SC-001).

- [ ] T025 [US1] Flash the RP2350 AMOLED board (approved dev board); check the boot log for the `psram:` line, no `FAILED`, and record the PSRAM zeroing time (plan constraint: +50 ms at most)
- [ ] T026 [US1] RP2350 board: 10 warm restarts (`picotool reboot`, reset button, `picotool load -f -x`) and 2 cold power cycles; PSRAM found every time (SC-007). If any fails, fix the reset step in `psram_amoled.c` before going on
- [ ] T027 [US1] Run the quickstart failure-path test with `NEUPOD_PSRAM_FORCE_FAIL=ON` on the RP2350 board: message on console and screen within 2 s, no service started (SC-007)
- [ ] T028 [P] [US1] Flash the ESP32-S3 board (app only, `write-flash 0x10000`); check the boot log and the `psram:` line
- [ ] T029 [US1] On each board: play tracks with embedded art, cover-file art, PNG, progressive JPEG, a JPEG wider than 5,120 px, no art; screenshots against the simulator at 600x450, same picture chosen (SC-001, FR-003, acceptance scenarios 1 and 2); replay a track of an album seen before and confirm no new decode on the console (SC-003)

**Checkpoint**: US1 verified on both boards.

---

## Phase 4: User Story 2 - Cover Flow and browse art (Priority: P2)

**Goal**: album header art and Cover Flow on both boards, responsive, no under-runs.

**Independent Test**: the `lib-coverflow*` steps driven over the console on each board match the simulator; tiles fill within 2 s (SC-002).

- [ ] T030 [US2] On each board: drive the Cover Flow tour steps over the console with the test card (10+ albums); screenshots against `build/host-amoled`; time from the last step to all tiles filled (SC-002)
- [ ] T031 [US2] On each board: frame time of a Cover Flow step and Now Playing redraw (the UI statistics in the monitor line); RP2350: if a step exceeds 50 ms or under-runs appear, give the browse tiles the uncached PSRAM alias in the RP2350 build (research R7) and measure again
- [ ] T032 [US2] On each board: LVGL pool peak through the whole library tour with Cover Flow; RP2350: if above 85% of 72 KB, apply the R8 contingency (`LV_ATTRIBUTE_LARGE_RAM_ARRAY` = `NEUPOD_PSRAM_BSS` in `app/ui/lv_conf.h` for amoled241, pool 192 KB) and re-run T024's figures
- [ ] T033 [US2] On each board: five minutes of Cover Flow and Now Playing with a track playing: 0 under-runs, FreeRTOS heap minimum within the gate, internal free within the gate (SC-005, SC-006, FR-009)

**Checkpoint**: US1 and US2 verified on both boards.

---

## Phase 5: User Story 3 - The rest of the big-RAM player (Priority: P3)

**Goal**: Playlists, play counts and 128 ratings work on both boards as on the simulator.

**Independent Test**: Music > Playlists on each board; Recently Played and On-The-Go across a restart.

- [ ] T034 [US3] On each board: play three tracks, check Recently Played (newest first); add two tracks to On-The-Go, restart, confirm they remain (acceptance scenario 1)
- [ ] T035 [US3] On each board, with a write-protected card: rate tracks, confirm they are kept in RAM and written once the card allows (acceptance scenario 2)

**Checkpoint**: all three stories verified.

---

## Phase 6: Polish & cross-cutting

- [ ] T036 [P] Amend `docs/adr/0013-bluetooth-ram-psram.md` with "PSRAM in use (UI-25)": placement per board, the failure policy, the budget with linked figures (research R11, constitution VIII)
- [ ] T037 [P] Update `docs/rp2350-amoled241.md` (PSRAM section with pin, size, clock, placement, failure behaviour, bench results with the date; remove it from "What is not done"; say `NEUPOD_ART_HIRES` is on by default and how to turn it off, FR-010) and `docs/esp32s3.md` (memory table rows for the new PSRAM buffers, the option and its override, bench results with the date)
- [ ] T038 [P] Update `docs/ui.md` "Thumbnails and cache (NP-5)" and the Cover Flow note (which builds have it), and the comments in `app/include/art.h`, `app/include/playlist_service.h`, `app/include/plays_store.h`, `app/include/ratings_store.h` that say "hosted, Y2, PSRAM boards"
- [ ] T039 [P] Mark the gap closed in `docs/adr/0022-one-player-ui.md` ("Known gap"), move UI-25 to done and update the NP-5 row in `docs/backlog.md`, add the entry to `CHANGELOG.md`
- [ ] T040 Bench results to the project memory `bench-status.md` (verified on hardware, open issues)
- [ ] T041 Run quickstart.md end to end; the PR description carries the size table (Pico 2 W zero deltas, both boards' budgets with linked figures) and names the reviewer's checklist: constitution I, IV, VII
- [ ] T042 Independent review (a subagent that did not write the code) against spec.md, plan.md and the constitution; reviewers read via `gh pr diff` only; no `gh pr merge` by agents

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (Phase 1)**: T001 to T003 first (T002, T003 in parallel); T004 then T005.
- **Foundational (Phase 2)**: needs T003 (it decides part of T011) and T005 (the host config catches UI bugs first).
  Inside: T006/T007/T008 independent; T009 → T010 → T011 → T012; T013 and T014 before T015 to T019; T019 → T020; T022 needs
  all of Phase 2; T023 and T024 need T022.
- **US1 (Phase 3)**: needs Phase 2. T025 → T026 → T027 on the RP2350 board; T028 in parallel on the ESP32-S3; T029 last.
- **US2 (Phase 4)**: needs US1's boot checks (T025, T028); T032 may change T024's figures.
- **US3 (Phase 5)**: needs Phase 2 only; can run alongside US2 on the other board.
- **Polish (Phase 6)**: after the stories; T036 to T039 in parallel; T041 → T042.

### User story dependencies

- US1: none beyond Phase 2.
- US2: uses the art cache that US1 verifies; independently testable on the tour.
- US3: independent of US1/US2 (same build, different screens).

## Parallel examples

```text
# Phase 2, once T013 and T014 are in:
T015 art_service.c   T016 playlist_service.c   T017 plays_store.c   T018 play_queue.c
# Bench, two boards at once:
T026/T027 on the RP2350 board   while   T028/T029 on the ESP32-S3 board
```

## Implementation strategy

### MVP first

Phases 1 and 2, then US1 on both boards: sharp art on Now Playing is the visible gap. Stop and show the lead the size table
and screenshots before Cover Flow tuning.

### Incremental delivery

One PR for Phases 1 to 3 (it must contain the whole option, since the option switches on all stories; US2/US3 are already
built and host-tested by then). Follow-up PRs only if US2 tuning (R7 uncached alias, R8 LVGL pool) needs code changes.

## Notes

- Heavy builds only through `~/bin/neupod-buildlock`; one at a time.
- Never force-push or rebase; merge main into the branch.
- Kill only processes you started, by PID.
