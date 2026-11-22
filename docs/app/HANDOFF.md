# Phone app: session handoff

_Last updated 2026-11-22 by the lead agent. The project record is [PROGRESS.md](PROGRESS.md); the brief is
[TEAM-BRIEF.md](TEAM-BRIEF.md). This file is the "where we stopped" note for the next lead session. Replace it, don't
append to it._

## State of main

- **Phase 0 done.** Scaffold, CI (`.github/workflows/app.yml`: `check`, `android`, `android-integration`, `ios`), decision
  notes 0001–0007.
- **Phase 1 code complete:** playback (`neupod_audio`), folder access (`neupod_storage`), minimal scan into drift with
  firmware collation, plain Songs list, background and lock-screen controls (#113, #114, #117, #124). Its device check
  is gate 1, still to do on the owner's phones.
- **Phase 2:** styles, components, device view (fit A/B, finishes), click wheel (exact `ui_accel_feed` port), haptics and
  system volume, letter ring, every gate-2 screen, the shell, Search by wheel (#106, #111, #112, #116, #118, #120, #121,
  #123, #125). Gate-2 screenshot tool: `mobile/tool/gate2_shots/` (#126).

## Gate 2 (in progress)

The owner has the screenshot page. Decided so far (spec owner decision 13, PR #128): fit A; body finish follows the mode,
no manual setting; phone status bar shown; wordmark on; rating stars on Now Playing page 2 only; Search capped at 24
characters.

Still to ask: touch candidate 1 or 2 and drag-to-seek (to be judged by feel at gate 1); Classic Light contrast (keep the
designer's exact values or adjust to 4.5:1); Home's right pane when nothing plays; tapping a side cover in Cover Flow;
the Music menu's preview pane; Settings contents; device view design Open questions 3, 4, 6, 7, 9, 10, 11, 13. Ask in
small batches (AskUserQuestion, recommended option first), record each answer in README owner decision 13 and the gate-2
line of PROGRESS.

Once gate 2 closes: remove the candidates not chosen (fit B, manual finish code paths if any) and continue phase 3.

## Open PRs

| PR | What | Next step |
|---|---|---|
| #128 | Gate-2 choices, this handoff, app section in the `neupod` skill (docs only) | Merge once checks are green. |
| #127 | Device view keeps its state across layout changes (review must-fix on #125); gate-2 tool tidy (personal path in a comment, temp-file cleanup, README) | Opened by an agent that was stopped mid-way: check it contains the reparent fix, the state-survives-resize test and the scaled-path hit test; let CI finish; Sonnet review; merge. |
| #122 | Android integration job via native instrumentation (`connectedDebugAndroidTest`) | Main merged in at 3a4a558. Needs three consecutive green `android-integration` runs (rerun the same run twice), a PR body that records the deliberate-failure proof (run 45005491672), a review, then merge. |

## Next work, in order

1. Close gate 2 (above).
2. Finish #127, #122, #128. Then the `app-gate` required check (see the working rules below).
3. Swap the interim sort in `mobile/lib/domain/library_view/sort_name.dart` to `collateKey`, and the gate manifest adapter
   to the drift database (needs #117, now merged).
4. **Phase 3, first item:** replace the drop-and-recreate `onUpgrade` in `mobile/lib/data/db/library_database.dart` with
   real migrations **before** any user-owned table (ratings, play counts, playlists) lands, and add a test that fails if
   tables other than `sources` and `tracks` exist until then. Also: treat `StorageException` as transient in the
   scanner's `_parse`; tests for an unavailable folder or unreadable subfolder coming back.
5. Follow-ups logged on merged PRs: audio (run `ErrorRecoveryTest` in CI, three-track test, `windowOf` test, skip in the
   background only); phase 5 lazy-list items (#123); iOS CI (per-file derived data, upload xcresult on failure).
6. Gate 1 batch ask to the owner when phase 1–2 are ready on devices: install Xcode 27, plug in the iPhone 15 Pro (iOS 27)
   and Pixel 7, trust the developer certificate. Device checks include iOS volume via the hidden MPVolumeView, wheel feel
   and haptics.

## How the lead works (learned the hard way)

- Merge only after `gh pr checks <n> --watch` exits 0, then confirm `gh pr view <n> --json state`. Never pipe
  `gh pr merge` through `tail`/`grep`.
- Agents share the owner's `gh` login and can merge: every brief says "do not run `gh pr merge`". (#125 and #126 were
  merged by an agent before their reviews finished; the owner did not merge them.)
- **Branch protection on main (owner, 2026-11-22):** changes only through a PR (no approvals required, since every agent
  is the same account); the firmware jobs `host`, `rp2350`, `rp2350-amoled241`, `y2`, `esp32s3` must pass; it applies to
  admins too; no force-push or deletion. App jobs are not required yet because `app.yml` is path-filtered (a required
  check that never runs would block docs-only PRs). To do: add an always-running `app-gate` job that passes when
  `mobile/` is untouched and otherwise needs check/android/android-integration/ios, then make it required.
- Set the model on every subagent: Opus for native code, exact firmware ports, hard CI and reviews of native code;
  Sonnet for UI from exact specs, tests and doc reviews; Haiku for mechanical checks.
- Reviewers read with `gh pr diff` / `git show <ref>:<path>`; they never check out in the shared tree.
- Before reusing a stopped agent's worktree, compare it with the remote (`git status -sb`, `git range-diff`). Never
  force-push; merge main into branches.
- The build Mac has 8 GB RAM and a small disk: serialise heavy builds (the lead's local build-lock wrapper kills anything
  over 45 minutes), give `flutter test` a `--timeout`, delete `mobile/build` and `.dart_tool` after, and remove merged
  worktrees promptly.
