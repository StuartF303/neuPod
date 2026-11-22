---
name: neupod
description: neuPod firmware, bench and phone-app work. Use for the Flutter phone app in mobile/ (building, testing, CI, leading the app team), and for building, flashing, deploying or driving any neuPod target (host, Pico 2 W, Waveshare RP2350/ESP32-S3 AMOLED dev boards, Innioasis Y2 Linux testbed), reading their consoles, sending keys, taking screenshots, or checking which boards are attached. Also covers the safety rules that apply to each device.
---

# neuPod: hardware and software

neuPod is an open-hardware iPod-style player. C firmware (LVGL, FreeRTOS, FatFs), one app layer (`app/`), and a HAL
(`hal/include/*.h`) implemented per platform (`platforms/*`, `hal/host`, `hal/common`). Docs live in `docs/`. ADRs in `docs/adr/`.

Start with `scripts/bench-status.sh` (in this skill folder). It shows which boards are attached and how to reach each one.

## Targets

| Target | Build | Put it on the device | Console / keys | Notes |
|---|---|---|---|---|
| host (SDL sim + tests) | `scripts/build.sh host` | n/a | stdin; `NEUPOD_TOUCH=1` for mouse-as-touch | `ctest --test-dir build/host --output-on-failure`. Tours run in ctest. |
| Pico 2 W rig (`rp2350`) | `scripts/build.sh rp2350` | `scripts/flash.sh` (OpenOCD, Debug Probe `2e8a:000c`) | `scripts/console.sh`, UART on the probe, `/dev/ttyACM0` | **Frozen.** ~100 B static RAM spare: report size deltas for any change. |
| RP2350-Touch-AMOLED-2.41 (`rp2350-amoled241`) | `scripts/build.sh rp2350-amoled241` | `picotool load -f -x build/rp2350-amoled241/platforms/rp2350/neupod.uf2` | USB CDC `/dev/serial/by-id/usb-Raspberry_Pi_Pico_*` | Dev board, flashing approved. `docs/rp2350-amoled241.md`. |
| ESP32-S3-Touch-AMOLED-2.41 (`esp32s3`) | see [workflows](reference/workflows.md#esp32-s3) (IDF v5.3.2 in docker) | `esptool ... write-flash 0x10000 build/esp32s3/neupod.bin` (app only) | `/dev/serial/by-id/usb-Espressif*` (`303a:1001`) | Dev board. Factory backup at `~/esp32amoled-backup/factory-flash-16MB.bin`. `docs/esp32s3.md`. |
| Innioasis Y2 (`y2`, Linux testbed) | `testbed/y2/scripts/deploy-fw.sh` builds + installs | same (ssh copy, no flashing) | `ssh root@192.168.7.2`; keys via `/run/neupod.keys` | USB `18d1:4ee8`. Never ships. Rules below. |

Step-by-step commands for each: [reference/workflows.md](reference/workflows.md). Board facts (pins, chips, USB IDs,
quirks): [reference/boards.md](reference/boards.md).

## Rules (all targets)

- **Ask the owner** before anything irreversible (overwriting firmware that isn't ours to overwrite, repartitioning, dropping
  data, force-push), anything that changes security posture, or an architectural call. Routine calls: make them and say which.
- Code changes go implement → verify → review (subagents). Tooling, docs and bench work don't.
- Git: branch + PR, **squash-merge once CI is green** (standing OK). Never force-push or rebase; merge main into branches.
- Never `pkill -f <pattern>` (it matches the calling shell). Kill by PID.
- Licences: keep `LICENSE.md`, the on-device Licences screen and the `licences` ctest current when adding third-party code.
  Don't copy vendor demo code (Waveshare, MTK) into our drivers.
- Keep the Pico 2 W image within its RAM. Report flash/RAM deltas for every RP2350 build.
- Don't send the owner's personal details to external services.

## Rules (Y2 only)

The Y2 runs Buildroot in place of its stock Android. The stock backup is at `~/y2-backup/2026-11-10` and on the NAS.

- **Never** touch `/dev/mmcblk0*` (the internal eMMC). The firmware only opens the removable microSD.
- **Never** write NVRAM (`/data/nvram`, `/data/BT_Addr`) and never run `nvram_daemon`. `wmt_loader` runs once per boot.
- Only BOOTIMG, RECOVERY, USRDATA and LOGO may ever be written, and **only by the owner** running `testbed/y2/scripts/flash-*.sh`.
  `deploy-fw.sh` (an ssh file copy) is fine to run.
- No Wi-Fi joins (recon only). MTK blobs, BT addresses and link keys are never committed.
- Audio caps: speaker volume ≤ 9, headphones ≤ 11.
- `testbed/y2/docs/` is git-ignored and private (the repo is public). Read it locally; never commit or quote it in PRs.
- Suspending the Y2 drops the USB link; it needs a physical key press to wake. Only do it with the owner at the bench.

## Dev boards

Both Waveshare AMOLED boards are the owner's dev boards ("dev only so we can test capabilities"). Flashing them is approved.
The ESP32 radio is scan-only (no joins). Always keep a full flash backup before replacing a factory image.

## Phone app (`mobile/`)

Flutter app that reads a neuPod's music in place. Spec `docs/app/README.md`, checklist `docs/app/PROGRESS.md`, how the
team works `docs/app/TEAM-BRIEF.md`, **where the last lead stopped `docs/app/HANDOFF.md` (read first)**.

- Build and test from `mobile/`: `flutter test --timeout 120s`, `dart analyze`. Integration: iOS via XCTest
  (`FLTIntegrationTestRunner`), Android via `connectedDebugAndroidTest`. CI is `.github/workflows/app.yml`.
- Firmware behaviour is the rule: wheel acceleration, letter ring, letter jump, sort keys and tag text are exact ports
  of `app/ui/*.c` and `app/audio/meta.c`, verified against the compiled C with vectors. Never approximate; where the
  firmware is silent, ask the owner.
- Gates in PROGRESS.md stop the work until the owner answers. Never settle a design Open question yourself.
- Subagents: set the model explicitly; tell every agent not to run `gh pr merge`; reviewers read via `gh pr diff` only.
- Small machine: one heavy build at a time, delete `mobile/build` and `.dart_tool` after, remove merged worktrees.

## Finishing work

- Run `ctest` on host for any app/HAL change; build every platform the change touches (CI builds host, rp2350,
  rp2350-amoled241, esp32s3, y2).
- Commit trailers come from the session's attribution reminder. PR bodies end with the Claude Code line.
- Bench results that matter (verified on hardware, open issues) go in the project memory `bench-status.md` and the feature's doc,
  with the date.
