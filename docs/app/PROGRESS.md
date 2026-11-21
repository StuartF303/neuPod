# Progress: neuPod phone app, R1

The checklist for [TEAM-BRIEF.md](TEAM-BRIEF.md). Tick an item when its PR merges and add the PR number and date:
`- [x] item (#123, 2026-12-01)`. Mark a blocked item `BLOCKED: reason` and leave it unticked.

Every item means: works on Android and iOS, covered by tests at the levels the specification names, analysis clean.

## Phase 0: set-up

- [ ] Environment report (Flutter, Xcode, CocoaPods, simulators, Android SDK, `gh`)
- [ ] Checklist reconciled with the specification
- [ ] `mobile/` scaffold, layering, ignore rules for keys and personal files
- [ ] CI: Android on Linux, iOS on a Mac runner, no signing
- [ ] Decision notes: audio engine, media session, tag reader, folder access, database, state management
- [ ] Owner answers recorded: bundle ID, minimum OS, Android test phone (spec Open questions 4 to 6)
- [ ] `LICENSE.md` row for `mobile/` (MPL-2.0)

## Phase 1: vertical slice

- [ ] Choose a folder and keep access to it, Android
- [ ] Choose a folder and keep access to it, iOS; app Documents folder visible in Files and Finder
- [ ] Minimal scan into the database
- [ ] Songs list
- [ ] Play a FLAC and an MP3
- [ ] Background playback with lock-screen and notification controls, artwork and scrubbing
- [ ] Device check: the slice on the owner's iPhone and Pixel, recorded with the date (needs owner: Xcode 27 installed, phones attached)
- [ ] **Gate 1: stop and show the owner**

## Phase 2: look and feel

- [ ] Style and mode tokens (Classic Light and Modern Dark) from the UI styles design; mode follows the phone by default
- [ ] Classic Dark and Modern Light. BLOCKED: not designed (UI styles Open question 1)
- [ ] Montserrat and Lucide bundled; Licences screen lists them
- [ ] Components: list row (one and two lines), selection and pressed states, header bar, progress bar, panel, art frame
- [ ] Accent from album art, clamped to 4.5:1 with white text
- [ ] Modern backdrop from the art
- [ ] Device view: the body and the 4:3 screen, scaled to the phone's width, drawn fresh (no Apple logo or artwork)
- [ ] Click wheel: rotation to scroll with acceleration, centre Select, Menu, previous, next, play/pause, press-and-hold; haptic detents and clicks on both platforms; optional click sound
- [ ] Wheel and buttons accessible: row-by-row screen reader navigation, adjustable control with actions
- [ ] Design-gate screens: Home, a list, an album, Cover Flow, Now Playing in the device view, with the open choices (fit on tall and short phones, body finish, direct taps on rows, largest text) built as candidates
- [ ] **Gate 2: stop and show the owner; record the choices here**

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
- [ ] Search
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
- [ ] Settings: style and mode, folders, gapless, skip-back, reset statistics
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
- [ ] **Gate 3: final report**

## Notes

(One line per thing the next session must know. Newest first.)

- 2026-11-21: The owner has asked the designer for a device-view layout framework (`docs/design/<date>-app-device-view/`): device geometry, fit on tall and short phones, wheel hit zones and behaviour in numbers, haptics, direct touch, largest text, accessibility. Phase 2's device-view, click-wheel and design-gate items build from it; phases 0 and 1 do not wait for it.

- 2026-11-21: Owner decision 9: the app is the device, a drawn-fresh front view with a touch click wheel, buttons and haptics. Phase 2 and the design gate are built around it; the device's 600x450 designs apply to the screen.

- 2026-11-21: The owner's iPhone runs iOS 27, and Xcode 26.5 cannot deploy to it. Xcode 27 must be installed (owner, Apple ID) before the gate 1 device check. The iOS 18.6 simulator runtime was installed for testing the minimum.
- 2026-11-21: Owner decisions 5 to 8 in the spec: bundled Xiph decoders for Ogg on iOS, no Cast in R1, a Download action for iCloud files, and the accepted platform limits. The audio engine and media session are an in-repo native plugin (decisions 0001, 0002): no pub.dev audio package met the spec.
- 2026-11-21: Owner decision 11: sorting follows `lib_collate.c` (skips The/A/An); `docs/ui.md` is out of date on this and is the firmware owner's to fix. Owner decision 10: the wheel sets system volume on Now Playing (iOS through a hidden MPVolumeView).

- 2026-11-21: Owner answered spec Open questions 4 to 6: ID `net.stuartfraser.neupod`; iOS 18+, Android 10 (API 29)+; Android test phone is the owner's Pixel (model and version to record when it is plugged in). The build Mac has 8 GB RAM: one emulator or simulator and one build at a time. See [ENVIRONMENT.md](ENVIRONMENT.md).
- 2026-11-21: Checklist reconciled with the specification; added items are the spec's requirements that had no line, plus a device check per phase (TEAM-BRIEF.md, The loop).
