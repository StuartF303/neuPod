# Progress: neuPod phone app, R1

The checklist for [TEAM-BRIEF.md](TEAM-BRIEF.md). Tick an item when its PR merges and add the PR number and date:
`- [x] item (#123, 2026-12-01)`. Mark a blocked item `BLOCKED: reason` and leave it unticked.

Every item means: works on Android and iOS, covered by tests at the levels the specification names, analysis clean.

## Phase 0: set-up

- [x] Environment report (Flutter, Xcode, CocoaPods, simulators, Android SDK, `gh`) (#101, 2026-11-21)
- [x] Checklist reconciled with the specification (#101, 2026-11-21)
- [x] `mobile/` scaffold, layering, ignore rules for keys and personal files (#102, 2026-11-21)
- [x] CI: Android on Linux, iOS on a Mac runner, no signing (#104, 2026-11-22)
- [x] Decision notes: audio engine, media session, tag reader, folder access, database, state management (#101, 2026-11-21)
- [x] Owner answers recorded: bundle ID, minimum OS, Android test phone (spec Open questions 4 to 6) (#101, 2026-11-21)
- [x] `LICENSE.md` row for `mobile/` (MPL-2.0) (#102, 2026-11-21)

## Phase 1: vertical slice

- [x] Choose a folder and keep access to it, Android (#114, 2026-11-22)
- [x] Choose a folder and keep access to it, iOS; app Documents folder visible in Files and Finder (#114, 2026-11-22)
- [x] Minimal scan into the database (#117, #131, 2026-11-22)
- [x] Songs list (#117, 2026-11-22)
- [x] Play a FLAC and an MP3 (#113, #124, 2026-11-22)
- [x] Background playback with lock-screen and notification controls, artwork and scrubbing (#113, 2026-11-22)
- [ ] Device check: the slice on the owner's iPhone and Pixel, recorded with the date (needs owner: macOS 26.6 or later, then Xcode 27, phones attached)
- [ ] **Gate 1: stop and show the owner**

## Phase 2: look and feel

- [x] Style and mode tokens (Classic Light and Modern Dark) from the UI styles design; mode follows the phone by default (#118, #121, 2026-11-22)
- [ ] Classic Dark and Modern Light. BLOCKED: not designed (UI styles Open question 1)
- [ ] Montserrat and Lucide bundled; Licences screen lists them
- [x] Components: list row (one and two lines), selection and pressed states, header bar, progress bar, panel, art frame (#118, 2026-11-22)
- [x] Accent from album art, clamped to 4.5:1 with white text (#118, 2026-11-22)
- [x] Modern backdrop from the art (#118, 2026-11-22)
- [x] Device view: body, 4:3 screen at s = 0.88 W / 600, wordmark, both finishes, fit A and fit B, safe areas, minimum width, phone status bar shown or hidden, wordmark on or off ([device view design](../design/2026-11-21-app-device-view/README.md) sections 1 to 3, 10) (#111, #125, #127, #130, 2026-11-22)
- [x] Click wheel: drawing and states, hit zones, press-or-turn at 7.5 degrees, 15-degree detents, an exact port of `ui_accel_feed()` (`app/ui/ui_logic.c`), holds, system volume on Now Playing (design sections 4 and 5) (#106, #111, #112, #120, 2026-11-22)
- [x] Fast-scroll letter tile and rail, trigger and timings confirmed against `app/ui/ui_list.c` and `ui_rail.c` (design section 5) (#106, #121, 2026-11-22)
- [x] Haptics on both platforms with the design's event table, API names confirmed against current platform docs, 30 a second cap, obeying the system setting; optional click sound (design section 5) (#111, #112, #130, 2026-11-22)
- [x] Accessibility of the device view: rows as elements, the wheel as one adjustable element with five actions, keyboard and switch control (design section 9) (#111, 2026-11-22)
- [x] Direct touch candidates 1 (wheel only) and 2 (wheel plus taps and drags), switchable for the gate (design section 6) (#111, #121, 2026-11-22)
- [x] Text tiers: 7, 6 and 5 rows by the phone's text size (design section 8) (#118, 2026-11-22)
- [x] Design-gate screens in the device view, Classic Light and Modern Dark, with real data: Home, Songs with the letter tile, an album with disc headings, Cover Flow, Now Playing at rest, with volume and in seek, a panel, Search typing with the wheel and in the results; fit A and B; touch 1 and 2; both body finishes; phone status bar shown and hidden; three text tiers (design sections 2, 3, 6, 7, 8 and 10) (#120, #121, #123, #126, 2026-11-22)
- [x] **Gate 2: stop and show the owner; record the choices here** (closed 2026-11-22; touch and drag-to-seek move to gate 1)
  - 2026-11-22: gate 2 shown (screenshots of every candidate, both styles, three iPhone sizes and three text tiers). Owner chose: fit A; body finish follows the mode, no manual setting; phone status bar shown; wordmark on; rating stars on page 2 only; Search kept at 24 characters (spec owner decision 13).
  - 2026-11-22: remaining choices (spec owner decision 13): text capped at 5 rows at the accessibility sizes (OQ3); holding Play/Pause does nothing extra (OQ4); own proportions (OQ6); speaker no icon, wired/USB jack plug, AirPlay and other wireless a new generic external-speaker icon, never Apple's glyph (OQ7); left-edge swipe is Menu on iOS (OQ9); click sound is a system sound following the ringer switch (OQ10); Search in the Music menu (OQ11) with up to three live results (OQ13); Classic Light adjusted to 4.5:1 plus a non-colour cue for Bluetooth saved vs active; Home idle pane shows a placeholder cover, "Nothing playing" and the song count; side-cover tap centres; preview pane for every Music-menu item; Settings = spec list + click sound + haptics switch. Touch candidate and drag-to-seek: judged at gate 1; until then candidate 2 by default (1 behind the gate switch), bar not draggable.
- [x] Gate 2 follow-ups: remove fit B and any manual body-finish code paths; Classic Light colours to 4.5:1 (selection bar upper stops under white text, rail letters, ink3 on grey) with a non-colour Bluetooth saved/active cue, goldens updated; generic external-speaker route icon for AirPlay and other wireless outputs, jack plug for USB/wired, no icon for the speaker; direct touch defaults to candidate 2 (1 behind the gate switch), progress bar not draggable until gate 1; check the rest of decision 13 against what is built and fix any gap (Home idle pane, side-cover tap centres, preview pane on every Music-menu item, up to three live results fewer at larger tiers, left-edge swipe as Menu, system click sound following the ringer switch). The click sound and haptics switches belong to the Settings item in phase 6 (#130, 2026-11-22)

## Phase 3: library

- [ ] Tags: title, artist, album artist, album, track, disc, year, genre, compilation, duration, format, embedded art
- [ ] Folder-convention fallback (`Artist/Year Album/NN Title.ext`, `Folder.jpg`), `, The` handling
- [ ] Sort and collation match the firmware's `lib_collate_key` (leading "The " ignored, accents folded, symbols, digits, letters). "The ", "A " and "An " skipped, as in the code (owner decision 11)
- [ ] Source-neutral track model (a track has a source and a location), so R2 is an addition
- [ ] Android: the device's shared music collection offered alongside picked folders
- [ ] iOS: files in iCloud Drive that are not downloaded are listed with a Download action (owner decision 7); the scan never hangs on them
- [ ] Generated test library: the owner's folder convention, discs, compilations, awkward names
- [ ] Read-only guarantee: a test shows no music file or folder is written, moved or deleted
- [ ] Discs merged into one album with sections
- [ ] Compilations rule
- [ ] Background scan with progress, cancel, and survival across app restarts
- [ ] Incremental rescan; removing a folder
- [ ] Art cache and thumbnails
- [ ] Ratings, play counts, last played, skip counts that survive a rescan and a move
- [ ] Unplayable files listed greyed with a reason, and never stop the queue
- [ ] Scale test at 20,000 tracks; numbers recorded
- [ ] Device check: library phase on the iPhone and Pixel, recorded with the date

## Phase 4: playback

- [ ] Queue: Play, Play Next, Add to Queue; reorder; clear
- [ ] Gapless, with the measured join test
- [ ] Shuffle and repeat (off, all, one)
- [ ] Seek and the three-second skip-back rule
- [ ] Interruptions: call, headphones unplugged, another app
- [ ] Headset and Bluetooth transport buttons
- [ ] Output route icon
- [ ] Resume queue, track and position
- [ ] Each format decodes: FLAC, ALAC, MP3, AAC, WAV, Opus, Ogg Vorbis; fixture checksums where possible (iOS Ogg through bundled Xiph decoders; Android ALAC through Apple's Apache-2.0 decoder; Android 24/192 FLAC checked, libflac extension if needed)
- [ ] No resampling or processing in the app; sample-rate passthrough: what each platform allows, measured and reported
- [ ] Device check: lock screen, headset buttons, Bluetooth, interruptions on the iPhone and Pixel, recorded with the date

## Phase 5: screens

- [ ] Home
- [ ] Music menu
- [ ] Artists, an artist
- [ ] Albums, an album with disc headings
- [ ] Songs
- [ ] Genres, a genre
- [ ] Compilations
- [ ] Cover Flow
- [ ] Fast-scroll letter index
- [ ] Letter ring model: a port of the firmware's text entry in search mode (`app/include/wheel_input.h`, `app/ui/input/wheel_input.c`, `docs/ui-input.md`), not a reimplementation, with tests mirroring `tests/test_wheel_input.c`
- [ ] Search, typed with the wheel (owner decision 12; device view design, "Search, by the wheel"): the field, the letter ring and the firmware's six mode chips (`abc`, `ABC`, `123`, `#!?`, `space`, `del`), the wheel turning the ring one glyph a detent, the centre typing the glyph (a space on `space`, a delete on `del`), Previous and Next moving along the chips, the live results (up to three, fewer at larger tiers; owner decision 13), the count in the status bar, Menu deleting one character, held 0.6 s clearing, on an empty field leaving, Play/Pause to the results list where the wheel scrolls and the centre opens a row, Menu back to typing with the text kept; never the phone's keyboard. The design's chip table shows five chips; it is superseded on that point by the owner's choice of six
- [ ] Now Playing, with rating and the format line (`FLAC 16/44.1`)
- [ ] Queue screen
- [ ] Wheel behaviour matches the device on every screen, including fast scroll, and Now Playing seek and system volume with the device's volume bar (owner decision 10)
- [ ] Integration test: pick a folder, scan, browse, play, background, resume, on the Android emulator and the iOS simulator
- [ ] Row feedback: flash and tick; greyed row with reason; centred panel
- [ ] Long names: ellipsis in lists; ticker for the focused title on Now Playing
- [ ] Golden tests: every screen, both styles, both modes, two sizes, largest text
- [ ] Device check: every screen on the iPhone and Pixel, recorded with the date

## Phase 6: playlists and settings

- [ ] On-The-Go: add, reorder, clear, save as
- [ ] Named playlists: create, rename, reorder, delete
- [ ] `.m3u8` import and export with relative paths
- [ ] Settings: style and mode, folders, gapless, skip-back, click sound, haptics, reset statistics (owner decision 13)
- [ ] About and Licences
- [ ] Device check: playlists moved between the phone and a neuPod card, recorded with the date

## Phase 7: finish

- [ ] Accessibility pass: screen reader, text size, contrast, touch targets
- [ ] Performance against the specification's targets
- [ ] An hour of continuous play on the iPhone
- [ ] An hour of continuous play on the owner's Pixel
- [ ] Full pass of every screen in every style and mode on each device
- [ ] Privacy check: no network connection (no `INTERNET` permission in the release Android manifest, no networking package), only the permissions needed
- [ ] Naming and marks check: no "iPod", no Apple marks or artwork in the app or its listing
- [ ] `LICENSE.md` and the in-app Licences screen list every package and asset
- [ ] `mobile/README.md`: build, run, install
- [ ] Release signing from an uncommitted `key.properties` (falls back to debug when absent; CI needs no secrets), and the app's own launcher icons in place of Flutter's default
- [ ] **Gate 3: final report**

## Notes

(One line per thing the next session must know. Newest first.)

- 2026-11-22: Test devices: iPhone 15 Pro on iOS 27.2.1 (393x852 pt) and Pixel 7 (Android version to be read at gate 1).
- 2026-11-22: Gate 1 needs Xcode 27 on the build Mac, which is now on macOS 27.2.1.
- 2026-11-22: Design-gate screens, second half (UI engineer B, `feat/app-gate-np-search`): Now Playing (rest, volume, seek, page 2), the centred panel layer (`panelProvider`, `PanelLayer`) and Search typed with the wheel are built as standalone widgets under `mobile/lib/ui/screens/` with goldens; the navigation shell plugs them in (`NowPlayingScreen`, `SearchScreen`, `PanelLayer`; `searchFunctionProvider` is to be overridden with the library view). Choices left to the owner and what the screens still lack are listed in the PR. Gate switches added: rating stars placement (page 2 only, or also under the format line) and a line showing whether the system volume has ever failed (owner decision 10: the fallback is not chosen).
- 2026-11-22: When #117's drift adapter lands, remove the gate-only code of PR #121: `data/gate_library`, the `libraryViewProvider` manifest fallback, `queueMapperProvider`'s `gate-sample:` test-tone mapping, the made-up durations, the "Damaged file" reason, `domain/library_view/sort_name.dart` (use `collateKey`), the Settings stub (style/mode only) and the Not-built-yet stand-ins (`StandInScreen`) once their screens exist.
- 2026-11-22: Design gate, browsing half (UI engineer A): shell, Home, Music, Songs, Artists/artist, Albums/album, Cover Flow, Genres/genre, Compilations, Settings (style, mode), over a `LibraryView` port fed by the test library's manifest (`mobile/lib/ui/screens/README.md`). Interim sort (`domain/library_view/sort_name.dart`) until #117's `collateKey` is on main; gate durations are made up (the manifest has none). Greyed rows are skipped by the wheel.

- 2026-11-22: Phase 0 done. App CI (`.github/workflows/app.yml`, #104): `check`, `android` (with the no-`INTERNET` release-manifest check), `android-integration` (API 29 emulator) and `ios` (`macos-26`, Xcode 26.5, iPhone 17 simulator, one visible retry if the simulator hangs). Follow-ups: bump the pinned Xcode 26.5 and the `iOS-26-5` runtime when the runner image drops them; consider Dependabot for the SHA-pinned actions; do not make the app jobs required checks without a gate job (they are path-filtered). Every run on main completes; PR runs cancel superseded ones.
- 2026-11-22: Lead practice: merge only after `gh pr checks <n> --watch` exits 0 (an early script merged #101 and #102 with firmware jobs pending; main was green afterwards). Every subagent is given an explicit model for its task. Reviewers read PRs through `gh pr diff` / `git show`, never `git checkout` in the shared tree. Heavy builds go through `~/bin/neupod-buildlock` (8 GB RAM).

- 2026-11-22: Owner decision 12: Search is typed with the wheel (#107). The owner chose the firmware's six mode chips, keeping `#!?` (the design drew five). Still to settle in code, not by the team's choice: `docs/ui-input.md` gives the letter-jump tile as 6 steps within 400 ms, hidden 700 ms later, where the device view design says the 2x multiplier and a fade after 250 ms at rest; the code (`ui_list.c`, `ui_rail.c`) decides.

- 2026-11-22: The device view design is in [`docs/design/2026-11-21-app-device-view/`](../design/2026-11-21-app-device-view/README.md) (#103). Its Open questions (thirteen as of #107) are the owner's; none is settled by the team (answered at gate 2: owner decision 13). Where its mock-ups and the specification differ, the build follows the specification and the README text, and the difference is shown at gate 2: the mock-ups' Home is the firmware's: it adds Podcasts (a spec Not goal), Photos and Extras and lacks Playlists; some show a Wi-Fi icon that the README says R1 omits; Now Playing has no rating stars (the spec requires them); the album header shows the disc count instead of the song count and total time; the spec's "seek by dragging the progress bar" conflicts with the design's "the progress bar is not draggable (seek mode is the way to seek)", so both are built switchable for the owner (decision 13: not draggable until judged at gate 1). (The earlier note that Search's wheel was not drawn disabled no longer applies: Search is typed with the wheel.)

- 2026-11-21: Owner decision 9: the app is the device, a drawn-fresh front view with a touch click wheel, buttons and haptics. Phase 2 and the design gate are built around it; the device's 600x450 designs apply to the screen.

- 2026-11-21: The owner's iPhone runs iOS 27, and Xcode 26.5 cannot deploy to it. Xcode 27 must be installed (owner, Apple ID) before the gate 1 device check. The iOS 18.6 simulator runtime was installed for testing the minimum.
- 2026-11-22: Correction: the iPhone 15 Pro runs iOS 27.2.1 (a release). Xcode 27 is released but needs macOS Tahoe 26.6 or later (Xcode 27.1 release notes); the build Mac ran 26.5.1, so the App Store offered only Xcode 26.5. The owner is updating macOS, then Xcode 27. The iOS 18.6 simulator runtime was removed for disk space (CI still tests the iOS 18 minimum); reinstall it if a local check at the minimum is needed.
- 2026-11-22: Follow-ups logged: Android 10's folder picker hides internal storage until "Show internal storage" (add `android.content.extra.SHOW_ADVANCED` in `NeupodStoragePlugin.pickFolder`); before release, a handled "library from a newer version" state (a downgrade is refused, and every query then errors); a separate reason for files the platform can never open (stored as "damaged" today); `library_database.dart` imports Flutter only for `visibleForTesting` (use `package:meta`).
- 2026-11-21: Owner decisions 5 to 8 in the spec: bundled Xiph decoders for Ogg on iOS, no Cast in R1, a Download action for iCloud files, and the accepted platform limits. The audio engine and media session are an in-repo native plugin (decisions 0001, 0002): no pub.dev audio package met the spec.
- 2026-11-21: Owner decision 11: sorting follows `lib_collate.c` (skips The/A/An); `docs/ui.md` is out of date on this and is the firmware owner's to fix. Owner decision 10: the wheel sets system volume on Now Playing (iOS through a hidden MPVolumeView).

- 2026-11-21: Owner answered spec Open questions 4 to 6: ID `net.stuartfraser.neupod`; iOS 18+, Android 10 (API 29)+; Android test phone is the owner's Pixel (model and version to record when it is plugged in). The build Mac has 8 GB RAM: one emulator or simulator and one build at a time. See [ENVIRONMENT.md](ENVIRONMENT.md).
- 2026-11-21: Checklist reconciled with the specification; added items are the spec's requirements that had no line, plus a device check per phase (TEAM-BRIEF.md, The loop).
