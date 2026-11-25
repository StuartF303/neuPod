# Phone app: session handoff

_Last updated 2026-11-22 (evening) by the lead agent, stopping cleanly for an OS update on the build Mac. The project
record is [PROGRESS.md](PROGRESS.md); the brief is [TEAM-BRIEF.md](TEAM-BRIEF.md). This file is the "where we stopped"
note for the next lead session. Replace it, don't append to it._

## State of main

- **Phase 0 done.** **Phase 1 code complete** (playback, folder access, minimal scan into drift with firmware collation,
  Songs list, background and lock-screen controls); its device check is gate 1.
- **Phase 2 done up to gate 2, and gate 2 is closed** (spec owner decision 13, #129). The gate 2 follow-ups are merged
  (#130): fit A only, body finish follows the mode, Classic Light at 4.5:1 with a contrast test over 31 pairs, route
  icons (speaker none, wired jack plug, AirPlay and other wireless a generic Lucide speaker), Bluetooth connected shown
  by shape, side-cover tap centres, system click sound when switched on. Direct touch (candidate 2 by default) and
  drag-to-seek (off) are judged at gate 1.
- **Phase 3 started:** stepwise drift migrations in one transaction, a guard test allowing only `sources` and `tracks`,
  storage errors classified by code (#131, decision note 0008).
- Also merged today: #127 (device view keeps its state across layout changes), #128 (handoff, skill).

## Open PRs

| PR | What | Next step |
|---|---|---|
| #132 `feat/app-library-view-drift` | Screens read the scanned drift library (`DriftLibraryView`) with firmware collation; the gate manifest is test-only now | Head fa8a693 is a **WIP commit whose tests have not run**. It ports the firmware's entity merging: artists fold on the whole collation key (artist text cut to 63 bytes), albums on (merged artist, album key), the Albums list ties in artist order, song rows keep the track's own artist text. `scripts/app/library_entities.c` runs the firmware's `lib_build()` on given tags to produce expected lists. Next: an Opus engineer runs `flutter analyze` and the library-view, provider, empty-library and app tests (most likely to fail: the failed-load test's `ALTER TABLE`, the provider test's `invalidateSelf`), fixes, makes a proper commit, updates the PR body. Then a fresh **Opus** review (firmware port), CI green, merge. |
| #122 `ci/app-android-integration-robust` | Android integration through native instrumentation; folder-access test run by `scripts/app/verify-folder-access-android.sh` on the CI emulator | Head 6eaa858. Review fixes and both deliberate-failure proofs are done (instrumentation 45005491672, folder access 45028505607). **Not mergeable yet:** about 4 in 10 runs hit a first-frame hang. **Root cause (proven): a deadlock in the API 29 emulator's graphics stack** (RenderThread stuck in `QemuPipeStream::commitBufferAndReadFully` ← `rcOpenColorBuffer2_enc` ← `gralloc.ranchu`; Flutter's raster thread waits on the gralloc lock; main waits in `FlutterJNI.onSurfaceCreated`). The app and PlaybackService are not the cause (without the service it still hangs, 2/75 vs 7/75; experiment run 45037873868). Real phones are unaffected. An experiment run, **45039175170** (branch `ci/exp-api29-render`, 13 jobs × 25 cold launches), compares API 29 variants: default image control, google_apis image, `-gpu guest`, `-feature RefCountPipe`, an older emulator build. Next: read its `RESULT` lines, pick a reliable API 29 setup (the minimum is API 29, so CI must keep it; don't change the app's render mode for CI), apply it, get **three consecutive green** android-integration attempts on the final head, correct the PR body (it still says "likely app-side"), delete `ci/exp-api29-render`, Opus review, merge. If no API 29 setup is reliable, ask the owner about the trade-off (add a newer API, or a retry gated on the gralloc signature) before doing either. |

## Next work, in order

1. Finish #132 and #122 (above).
2. Docs: tick the phase 1 and phase 2 items in PROGRESS with their PR numbers (most merged items are unticked).
3. The `app-gate` required check: an always-running job that passes when `mobile/` is untouched and otherwise needs
   check/android/android-integration/ios; then the owner makes it required in branch protection.
4. Phase 3 on: tags and fallbacks, the folder convention, discs and compilations (the drift schema needs album artist,
   year, disc, genre, compilation; Genres and Compilations are empty until then), incremental rescan, art cache, then
   ratings and play counts (each user-owned table with a real migration and the guard test updated), the scale test
   (`DriftLibraryView` loads every row per read; give it real queries before the 20k test).
5. Follow-ups logged in PROGRESS notes and on merged PRs (audio: run `ErrorRecoveryTest` in CI, three-track test,
   `windowOf` test; phase 5 lazy-list items; iOS CI per-file derived data and xcresult upload; `capture_sim.sh` should
   shut the simulator down on failure).
6. **Gate 1**, once the owner has updated macOS to 26.6 or later and installed Xcode 27 (the iPhone 15 Pro runs iOS
   27.2.1; Xcode 26.5 can't deploy to it): plug in the iPhone and the Pixel 7, trust the developer certificate. Checks
   include iOS volume through the hidden MPVolumeView, wheel feel and haptics, direct touch candidate 1 or 2, and
   drag-to-seek.

## How the lead works (learned the hard way)

- Merge only after `gh pr checks <n> --watch` (or a polling loop) shows every check passing on the reviewed head, then
  confirm `gh pr view <n> --json state`. Never pipe `gh pr merge` through `tail`/`grep`.
- Every brief says "do not run `gh pr merge`" and "never kill processes you didn't start": one agent's `pkill -f "gh pr
  checks"` killed the lead's watchers.
- **Check what agents claim.** One reported a contrast test that was never written. Before merging, confirm claimed
  files exist (`git ls-tree`) and test counts are real ("green" once meant "every test skipped").
- **Re-review fix commits** of native, firmware-port and data-safety code: the #131 review caught migrations not run in
  a transaction (drift 2.35 doesn't wrap `onUpgrade`); the #132 re-review caught entity merging that didn't match the
  firmware.
- Model per subagent: Opus for native code, exact firmware ports, data safety, hard CI and their reviews; Sonnet for UI
  from exact specs, tests and doc reviews; Haiku for mechanical chores. A cheaper model that gets a second must-fix
  round on an item is replaced by Opus.
- Reviewers read with `gh pr diff` / `git show <ref>:<path>` only.
- Agents misjudge elapsed time: check run `createdAt` before calling CI stuck.
- `verify-folder-access-android.sh` run locally fails with `cp: Operation not permitted` under the agent sandbox; it
  works with the sandbox off.
- Branch protection on main: PR required (0 approvals), firmware jobs required, enforce admins, no force-push.
- The build Mac has 8 GB RAM and a small disk: one heavy build at a time through `~/bin/neupod-buildlock`, `--timeout`
  on `flutter test`, delete `mobile/build` and `.dart_tool` after, remove merged worktrees promptly. After Xcode 27 is
  installed, the iOS 26.5 simulator runtime (8.5 GB) can go.
