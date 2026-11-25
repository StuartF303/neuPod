# Phone app: session handoff

_Last updated 2026-11-25. The project record is [PROGRESS.md](PROGRESS.md); the brief is [TEAM-BRIEF.md](TEAM-BRIEF.md).
This file is the "where we stopped" note for the next lead session. Replace it, don't append to it._

## Rules that outrank the plan

- [`docs/constitution.md`](../constitution.md) outranks any plan or spec. Check work against it.
- [ADR-0022](../adr/0022-one-player-ui.md): one player UI on every device and the app. A difference needs a hardware or
  platform reason, as a setting or build option; a UI change lands everywhere or is logged in the backlog.

## State of main

- **Phases 0 to 2 done; gate 1 and gate 2 closed.** Gate 1 (owner decision 18): direct touch is candidate 2 (wheel plus
  taps and drags), seek is by the wheel's seek mode only, the progress bar is not draggable. Candidate 1 and the drag
  switch are gone from the code. The owner ran the slice on an iPhone 15 Pro (iOS 27.2.1) and a Pixel 7 (Android 17).
- **Phase 3 (library), mostly done:** tags (FLAC, MP3, M4A, WAV), embedded art and the art cache, firmware collation,
  source-neutral tracks, background scan with progress, cancel, Android foreground service, incremental rescan, folder
  removal, generated test library, read-only guarantee, discs and compilations.
- **Phase 3 remaining** (all unticked in PROGRESS): Android shared music collection; iCloud files that are not downloaded
  (Download action, scan never hangs); ratings, play counts, last played and skip counts that survive a rescan (a
  user-owned table with a real migration and the guard test updated; add the confirmation noted on the folder item);
  unplayable files greyed with a reason; the 20,000-track scale test (`DriftLibraryView` loads every row per read, give it
  real queries first); the phase 3 device check.
- Merged since the last handoff: #141 to #150 (library), #154 (Spec Kit pilot spec), #155 (Custom mode design).

## Open owner decisions

From [Custom mode, Open questions](../design/2026-11-25-app-custom-mode/README.md#open-questions) (nothing built yet;
AP-1, progress in that folder's PROGRESS.md):

1. Custom look row always shown under Mode, or only while Mode is Custom.
2. Board pictures: which boards, rendered or photographed, by when (`boards/` is empty, so it cannot ship without one).
3. The starter looks' exact colours (Harbour, Sea glass, Clear case).
4. Six looks, as the no-scroll rule gives.
5. Largest text: looks list and a look's rows move with the wheel, or split over two screens.
6. Settings is at seven rows; group the rest (Playback, About)?
7. Most transparency: 85 % body, 60 % wheel and centre.
8. Wordmark badge on a see-through body, or no wordmark.
9. Android status bar ink on a mid-tone body.
10. Clashing with Modern's art-led accent.
11. Does a real neuPod ever need to know the look (companion, R3).

## Device builds

The owner's iPhone and Pixel are the targets. Team ID for iOS signing: the owner's personal team, set locally; never
commit it. From `mobile/`, one heavy build at a time through `~/bin/neupod-buildlock`:

1. `flutter build ios --config-only --no-codesign`
2. `xcodebuild` on `ios/Runner.xcworkspace` (scheme Runner, Release or Debug) with `DEVELOPMENT_TEAM=<team>` and
   `-allowProvisioningUpdates`.
3. `xcrun devicectl device install app --device <id> <Runner.app>`.

Android: `flutter build apk` and `adb install`. Delete `mobile/build` and `.dart_tool` afterwards.

## Spec Kit pilot

`specs/001-amoled-full-art` (UI-25, full album art on the AMOLED boards: spec, plan, tasks; `speckit-*` skills,
`.specify/`). It is firmware work, not the app. Tasks are all unticked. It is a pilot of the method; judge it after the
first feature is through.

## Known flaky things

- **Network.** DNS to github.com sometimes fails, which breaks the sqlite3 native-assets download during `flutter
  test` or a build. Retry; nothing is wrong with the code.
- **iOS CI timeout.** On #150 a slow macOS runner took 17 minutes to build and the 40-minute job was cancelled
  mid-tests. The `ios` job is now 60 minutes (the Integration tests step keeps its own 25). If it still cancels, read the
  step times before raising anything.
- Check that "green" means tests ran: confirm counts are real, not all skipped (146 skipped on the host are goldens,
  compared on the macOS job).

## How the lead works

- Merge only after `gh pr checks <n> --watch` shows every check passing on the reviewed head. Squash-merge once green
  (standing OK). Never force-push or rebase; never pipe `gh pr merge` through `tail` or `grep`.
- Every subagent brief says "do not run `gh pr merge`" and "never kill processes you didn't start".
- **Check what agents claim** (files exist, test counts real). Re-review fix commits of native, firmware-port and
  data-safety code.
- Models: Opus for native code, exact firmware ports, data safety, hard CI and their reviews; Sonnet for UI from exact
  specs, tests and docs; Haiku for mechanical chores.
- Goldens are regenerated only in CI: `gh workflow run app.yml --ref <branch> -f update_goldens=true`, then download
  the artifact (docs/app/ENVIRONMENT.md "Goldens"). Never locally.
- The build Mac has 8 GB RAM and a small disk: `~/bin/neupod-buildlock <command>`, delete `mobile/build` and
  `.dart_tool` after, remove merged worktrees.
- Branch protection on main: PR required (0 approvals), firmware jobs required, enforce admins, no force-push.
