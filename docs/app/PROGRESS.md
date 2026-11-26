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
- [x] Device check: the slice on the owner's iPhone and Pixel, recorded with the date (owner ran it 2026-11-22/23 on an iPhone 15 Pro, iOS 27.2.1, and a Pixel 7, Android 17; see Gate 1)
- [x] **Gate 1: stop and show the owner** (closed 2026-11-25, #157)
  - 2026-11-22/23: the owner ran gate 1 on an iPhone 15 Pro (iOS 27.2.1) and a Pixel 7 (Android 17). Choices (spec owner decision 18): touch is **candidate 2** (the wheel plus taps and drags); seek is by the wheel's seek mode only, the progress bar is **not draggable**. On the iPhone: the slice survives a restart, wheel feel and haptics are OK, the wheel changes the volume. Cover Flow matches the device (decision 14). Apple team for device builds: the owner's personal team (set locally, never in the repo).
  - 2026-11-25: candidate 1 and the draggable bar are removed from the code, with their tests and gate switches (#157).

## Phase 2: look and feel

- [x] Style and mode tokens (Classic Light and Modern Dark) from the UI styles design; mode follows the phone by default (#118, #121, 2026-11-22)
- [ ] Classic Dark and Modern Light. BLOCKED: not designed (UI styles Open question 1)
- [x] Montserrat and Lucide bundled; Licences screen lists them (#118, #138, 2026-11-22)
- [x] Components: list row (one and two lines), selection and pressed states, header bar, progress bar, panel, art frame (#118, 2026-11-22)
- [x] Accent from album art, clamped to 4.5:1 with white text (#118, 2026-11-22)
- [x] Modern backdrop from the art (#118, 2026-11-22)
- [x] Device view: body, 4:3 screen at s = 0.88 W / 600, wordmark, both finishes, fit A and fit B, safe areas, minimum width, phone status bar shown or hidden, wordmark on or off ([device view design](../design/2026-11-21-app-device-view/README.md) sections 1 to 3, 10) (#111, #125, #127, #130, 2026-11-22)
- [x] Click wheel: drawing and states, hit zones, press-or-turn at 7.5 degrees, 15-degree detents, an exact port of `ui_accel_feed()` (`app/ui/ui_logic.c`), holds, system volume on Now Playing (design sections 4 and 5) (#106, #111, #112, #120, 2026-11-22)
- [x] Fast-scroll letter tile and rail, trigger and timings confirmed against `app/ui/ui_list.c` and `ui_rail.c` (design section 5) (#106, #121, 2026-11-22)
- [x] Haptics on both platforms with the design's event table, API names confirmed against current platform docs, 30 a second cap, obeying the system setting; optional click sound (design section 5) (#111, #112, #130, 2026-11-22)
- [x] Accessibility of the device view: rows as elements, the wheel as one adjustable element with five actions, keyboard and switch control (design section 9) (#111, 2026-11-22)
- [x] Direct touch candidates 1 (wheel only) and 2 (wheel plus taps and drags), switchable for the gate (design section 6) (#111, #121, 2026-11-22). Gate 1 chose candidate 2; candidate 1 is removed (#157, 2026-11-25)
- [x] Text tiers: 7, 6 and 5 rows by the phone's text size (design section 8) (#118, 2026-11-22)
- [x] Design-gate screens in the device view, Classic Light and Modern Dark, with real data: Home, Songs with the letter tile, an album with disc headings, Cover Flow, Now Playing at rest, with volume and in seek, a panel, Search typing with the wheel and in the results; fit A and B; touch 1 and 2; both body finishes; phone status bar shown and hidden; three text tiers (design sections 2, 3, 6, 7, 8 and 10) (#120, #121, #123, #126, 2026-11-22)
- [x] **Gate 2: stop and show the owner; record the choices here** (closed 2026-11-22; touch and drag-to-seek move to gate 1)
  - 2026-11-22: gate 2 shown (screenshots of every candidate, both styles, three iPhone sizes and three text tiers). Owner chose: fit A; body finish follows the mode, no manual setting; phone status bar shown; wordmark on; rating stars on page 2 only; Search kept at 24 characters (spec owner decision 13).
  - 2026-11-22: remaining choices (spec owner decision 13): text capped at 5 rows at the accessibility sizes (OQ3); holding Play/Pause does nothing extra (OQ4); own proportions (OQ6); speaker no icon, wired/USB jack plug, AirPlay and other wireless a new generic external-speaker icon, never Apple's glyph (OQ7); left-edge swipe is Menu on iOS (OQ9); click sound is a system sound following the ringer switch (OQ10); Search in the Music menu (OQ11) with up to three live results (OQ13); Classic Light adjusted to 4.5:1 plus a non-colour cue for Bluetooth saved vs active; Home idle pane shows a placeholder cover, "Nothing playing" and the song count; side-cover tap centres; preview pane for every Music-menu item; Settings = spec list + click sound + haptics switch. Touch candidate and drag-to-seek: judged at gate 1; until then candidate 2 by default (1 behind the gate switch), bar not draggable.
- [x] Gate 2 follow-ups: remove fit B and any manual body-finish code paths; Classic Light colours to 4.5:1 (selection bar upper stops under white text, rail letters, ink3 on grey) with a non-colour Bluetooth saved/active cue, goldens updated; generic external-speaker route icon for AirPlay and other wireless outputs, jack plug for USB/wired, no icon for the speaker; direct touch defaults to candidate 2 (1 behind the gate switch), progress bar not draggable until gate 1; check the rest of decision 13 against what is built and fix any gap (Home idle pane, side-cover tap centres, preview pane on every Music-menu item, up to three live results fewer at larger tiers, left-edge swipe as Menu, system click sound following the ringer switch). The click sound and haptics switches belong to the Settings item in phase 6 (#130, 2026-11-22)

## Phase 3: library

- [x] Tags: title, artist, album artist, album, track, disc, year, genre, compilation, duration, format, embedded art (#147, M4A and WAV #149, embedded art #148, 2026-11-23). Read from FLAC, MP3, M4A/MP4 and WAV, the formats the firmware parses (`tag_reader.dart`); an Ogg or Opus file gets the fallbacks of decision 15
- [x] Sort and collation match the firmware's `lib_collate_key` (leading "The " ignored, accents folded, symbols, digits, letters). "The ", "A " and "An " skipped, as in the code (owner decision 11) (`collation.dart` checked against the firmware's vectors #117, 2026-11-22; artist and album merging on the collation key #132, 2026-11-22)
- [x] Source-neutral track model (a track has a source and a location), so R2 is an addition (`sources` with a `kind` and `tracks` with `sourceId`, `relPath` and `uri`: #117, #131, 2026-11-22; decision 0005)
- [x] Android: the device's shared music collection offered alongside picked folders (#162, 2026-11-26; decision 0012, owner decision 21): MediaStore audio as one more root (`media_store` source), Settings > Library > Add Music on This Phone, the permission asked for only then, tags from the files' bytes, paths `RELATIVE_PATH` + `DISPLAY_NAME`, rescan by size and `DATE_MODIFIED`, music only (`IS_MUSIC`), a file in a picked folder listed once by the folder, turning it off keeps the ratings and play counts
- [x] iOS: files in iCloud Drive that are not downloaded are listed with a Download action (owner decision 7); the scan never hangs on them (detected from metadata only: `.icloud` placeholders, the downloading status key, the APFS dataless flag; never read; Select on the greyed `Not downloaded` row asks iOS to download, the row says `Downloading`, and the scan picks the file up when a metadata poll sees it local; decision 0004 "iCloud files not downloaded", #163, 2026-11-25). Real evicted files are checked at the phase 3 device check
- [x] Generated test library: the owner's folder convention, discs, compilations, awkward names; its expectations are the device's (owner decision 15, #150)
- [x] Read-only guarantee: a test shows no music file or folder is written, moved or deleted (#145, 2026-11-23)
- [x] Discs merged into one album with sections: by disc number tags, as the firmware (#147). "(Disc n)" in folder or album names is not merged, as on the device: settled by owner decision 15 (#150)
- [x] Compilations rule: COMPILATION / TCMP flag or album artist "Various Artists", as `index_file` (#147)
- [x] Background scan with progress, cancel, and survival across app restarts (progress and cancel #141, 2026-11-22; no resume after a cancel #143, 2026-11-22; Android survives the app being closed #146, 2026-11-23). On iOS a scan cannot outlive the app (owner decision 8): it restarts at the next launch and each return to the front, and unchanged files are skipped
- [x] Incremental rescan; removing a folder (unchanged size and modification time are not reopened #117, #141; Remove a folder #141, confirmation as the firmware #145, 2026-11-22/23)
- [x] Android: the scan survives the app being closed: a `dataSync` foreground service with the firmware's notification and Cancel keeps the app's engine, and the scan, alive after a swipe-away (decision 0009, #146, 2026-11-23). Owner check on the Pixel 7 at the next device session. Follow-up from #141
- [x] Removing a folder deletes its tracks at the next scan, with no question asked (Settings > Library > a folder > Remove). Add a confirmation before the ratings and play counts item below lands, since those would go with the tracks. Follow-up from #141 Done: the question is the firmware's own (#145, 2026-11-23)
- [x] Art cache and thumbnails: embedded art (FLAC PICTURE, ID3v2 APIC/PIC) and the folder's cover file, in the firmware's order; 192 and 600 px JPEGs in the app's support directory, 64 MB, off the UI isolate; screens, Modern accent and backdrop, lock screen and notification (decision 0010, 2026-11-23)
- [x] Ratings, play counts and last played that survive a rescan and a move: counted by the firmware's rules (vectors from the compiled C), with one known difference: a restart of the same queue entry (Previous after 3 s, repeat one) is not a new start yet, so it is not counted again until `neupod_audio` reports starts (see Notes); keyed by the device's path and size, carried over a move or a retag by a tag-free audio fingerprint, in their own `stats.sqlite` that iCloud and Google backups keep while the library database stays out (owner decision 20, decision 0011, #160, 2026-11-25). Skip counts were dropped from this item: the device has none (owner decision 20)
- [x] Unplayable files listed greyed with a reason, and never stop the queue (#159, 2026-11-25)
- [x] Scale test at 20,000 tracks; numbers recorded (2026-11-26, decision 0013): a generated library of real FLAC, MP3 and M4A files (1,500 album folders, 563 artists, compilations, two-disc albums, Unicode and "The" names) scanned by the app's scanner, `mobile/test/scale`. Host (M1, debug): first scan 18 s, rescan with nothing changed 0.8 to 1.1 s, rescan after 100 changes 0.9 s, database 17.2 MB, first list after a scan 0.43 s, every list once loaded under 6 ms, a Search keystroke 3.3 ms, the lists' memory 5 MB. Runs at 2,000 tracks on every PR; the full run is the app workflow's `scale_tracks` input. The library view keeps its one read and fold (no per-list SQL, no schema change; lead call in decision 0013)
- [ ] Device check: library phase on the iPhone and Pixel, recorded with the date. iCloud step (owner decision 7): pick an iCloud Drive folder, evict an album in Files (Remove Download), Update Library: its songs stay listed by title, greyed `Not downloaded`, and the scan finishes; Select one: `Downloading`, then a playable song with its art within a few seconds of iOS finishing; with flight mode on, Select gives `Not downloaded` back (or `Can't download`), and nothing hangs. Includes the scale numbers on each phone (decision 0013): copy a 20,000-track scale library (`dart run tool/gen_test_library/main.dart --out <dir> --scale 20000`, 314 MB) to the phone, add its folder, and time the first scan, a rescan with nothing changed and the first list after the scan (Artists), check Songs scrolls without dropped frames (Flutter performance overlay or a profile build), and note memory from Xcode's gauge and Android Studio's profiler; against the spec's targets (5,000 FLAC files in 5 minutes, a rescan in 10 s) and the lead calls (first list under 1 s, a list under 100 ms). Over 1 s for the first list means the stored-keys design in decision 0013

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
- [ ] Cover Flow: the Albums list's albums in its order, the device's caption `Artist · Year · n songs` in both styles (owner decision 14)
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

- 2026-11-26: Scale test and spec suites (`feat/app-scale-and-spec-suite`). `test/spec` now runs with no skips: every
  suite is wired to the app (wheel acceleration and touch, text tiers and screen scale, the library rules over the
  drift library view, the scanner over a generated folder through `test/support/host_library.dart`, a dart:io storage
  root that skips hidden names as both platforms' walks do). Where a spec test disagreed it was changed to the
  firmware or the owner's decisions: an untagged file's title is its file name and `(Disc n)` names stay two albums
  (decision 15), an album without track numbers is in title order (`alb_cmp`). `WheelInput.turnedPositions` was added
  for the touch suite. The generator's "untagged" ALAC file was really tagged (the fixture has tags); it now strips
  them with the new M4A tag writer. Scale test: decision 0013.
- 2026-11-26: iCloud follow-ups (lead calls under owner decisions 7 and 19, decision 0004). A queued song that iOS has evicted since it was queued is skipped, as an unplayable song is: neupod_audio's engine checks the file's download state from metadata (`LocalFile.swift`: placeholder, downloading status, dataless flag) before it opens the current or the pre-scheduled next item, and fails it as `Not downloaded`, so AVFoundation never fetches it; the lock screen's embedded art is read only from a file on the phone. neupod_audio's privacy manifest now declares the file timestamp API (`lstat`, 3B52.1 and C617.1). Search: its index was already rebuilt after a finished scan (`searchFunctionProvider` watches the library view); its rows now also read `Downloading` once asked for, as the lists do. `requestDownload` has a 30 s Dart timeout; the plugin's notes of old downloading errors are dropped once a request settles and capped at 256.

- 2026-11-25: iCloud files not downloaded (owner decision 7, decision 0004). Lead call, UI: the Download action is Select on the greyed `Not downloaded` row itself (the device has no row buttons and decision 19 puts the reason on the row); its reason then reads `Downloading` until the scan finds the file. No new screen, menu or control. The device has no iCloud, so this is a platform difference (constitution I), iOS only. Follow-ups done in the next PR (below). `LibraryLoader` now loads again when a finished scan rebuilds the library view, so an open list shows what the scan found.

- 2026-11-25: Android shared music collection (#162, decision 0012): `neupod_storage` `addSharedCollection()` and root kind `sharedCollection` (id `mediastore:audio`); the walk is one MediaStore query (`IS_MUSIC != 0`, all external volumes), entries open through `openFd` like SAF documents. No schema change (`sources.kind` `media_store`). The scanner leaves out a collection item inside a picked folder (tree URI to volume and path, `lib/data/scan/shared_collection.dart`), as owner decision 21 chose (26 Nov). Android integration test `media_store_test.dart`: the instrumentation rule in `MainActivityTest.java` grants the permission and inserts a WAV only when `neupodSeedMediaStore=true` (the script passes it for that file); CI step and job timeouts raised for the 7th file. The permission dialog itself is not driven in CI.
- 2026-11-25: Ratings and play counts (decision 0011, owner decision 20; library schema version 5 adds `tracks.audio_fingerprint`, scan version 6; the new `stats.sqlite` is schema version 1 and is in phone backups through neupod_storage `backedUpDataPath()` and the Android backup rules): the firmware's play tracker, rating and plays rules are ported and checked against vectors from the compiled C (`scripts/app/gen-plays-vectors.sh`, a CI step diffs them). Follow-ups: the audio engineer to report a track start from `neupod_audio` (a restart of the same queue entry, Previous after 3 s or repeat one, is not a new start yet, so not counted again); Most played, Top rated, Unplayed and Settings > Reset ratings and play counts (phase 6) read `track_stats`; the star colours differ from the device's (accent and line grey there; filled and outlined in the inks here), a UI styles question for the owner. `make-migrations` cannot read `_testVersion ?? n`: write the bare number while it runs (comment on `schemaVersion`).
- 2026-11-25: Unplayable files (#159, owner decision 19). A song is unplayable when the device would mark it damaged or the phone's player cannot decode it. Every stored read failure (wrong format, damaged, can't open, can't read) is the device's `LIB_PLAY_DAMAGED` (`index_file` and `play_from_meta`, lib_build.c), worded `Damaged file` (`lib_cant_play_str`); `Not downloaded` stays the app's own; on iOS, Ogg and Opus read `Ogg, can't play` / `Opus, can't play` until the Xiph decoders land (owner decision 5). Greyed rows are selectable by the wheel and touch, as on the device (`ui_list.c` skips no row; the earlier note that the wheel skips them no longer holds); Select opens nothing; Search results grey them too. Decision 19 settles the differences from today's firmware: reason on the row and no toasts (UI styles design), unplayable songs left out of the queue, Shuffle Songs never starts on one; the firmware's backlog items are the lead's.
- 2026-11-23: Album art (#148, schema version 4, scan version 5), known differences from the device: a `Folder.jpg` with the FAT hidden or system attribute (as Windows Media Player writes it) is skipped by the device (`art_service.c` line 380) but used by the app, which cannot see FAT attributes; the app reads at most 32 MB of a picture (decision 0010).
- 2026-11-23: Album art follows the firmware, not the spec's wording: cover files are `cover.jpg`, then `folder.jpg`, then `front.jpg` (any ASCII case; the spec says "`Folder.jpg` (or `cover.jpg`)"), only baseline JPEG is shown (no PNG or progressive JPEG, as on the device), and an embedded JPEG comes before the cover file (decision 0010).
- 2026-11-23: Owner decision 15 (#150, docs/app/README.md): untagged files match the device. The album is the folder's name, the artist "Unknown Artist", the title the file name (extension and all); there is no `Artist/Year Album/NN Title.ext` parsing and no `, The` handling. This replaces the folder-convention fallback item; `Folder.jpg` / `cover.jpg` art stays with the art cache item. Album names or folders that differ only by "(Disc n)" stay apart, as on the device. The generated test library's expectations and `test/spec/library_rules_spec_test.dart` follow it.
- 2026-11-23: Disc headings (lead decision, #147; the firmware draws none): an album with no disc numbers is one disc with no heading; where some songs have disc numbers, the songs without keep their firmware place (first, `alb_cmp`) under no heading and each disc number gets "Disc n" (never "Disc 0"). A file whose parse fails part-way keeps the tags read before the failure, with its reason, as the device lists it.
- 2026-11-23: Library tags and album artists (#147, schema version 3, scan version 3): FLAC and MP3 album artist, sort tags, genre, disc, year and compilation flag read as `meta.c` reads them (vectors from the compiled parser, `scripts/app/gen_tag_key_vectors.py`); the library view folds artists on the album artist, albums on (album artist, album), compilations on the flag or "Various Artists", genres, disc order, years, as `lib_build.c` does (fixtures from `lib_build()`, `scripts/app/library_entities.c`). Firmware findings for the owner: with COMPILATION=1 a "Back to Mine" is a **compilation** on the device (Compilations, no artist), not an album under its album artist; an artist's albums are in album-key order (the design drew year order, ui-styles Open question 6); the device's genre screen lists artists, the app's lists albums; the firmware has **no** `Artist/Year Album/NN Title` or `, The` folder fallback and no "(Disc n)" folder merging (untagged: title = file name, artist "Unknown Artist", album = folder), so the spec's folder-convention items and `test/spec/library_rules_spec_test.dart` wait for an owner decision. M4A tags and duration (`mp4.c`) and WAV durations (`wav.c`) follow in the stacked PR; AAC has no duration on the device (it cannot play it), the app takes the media header's.

- 2026-11-22: Test devices: iPhone 15 Pro on iOS 27.2.1 (393x852 pt) and Pixel 7 (Android version to be read at gate 1).
- 2026-11-22: Gate 1 needs Xcode 27 on the build Mac, which is now on macOS 27.2.1.
- 2026-11-22: Design-gate screens, second half (UI engineer B, `feat/app-gate-np-search`): Now Playing (rest, volume, seek, page 2), the centred panel layer (`panelProvider`, `PanelLayer`) and Search typed with the wheel are built as standalone widgets under `mobile/lib/ui/screens/` with goldens; the navigation shell plugs them in (`NowPlayingScreen`, `SearchScreen`, `PanelLayer`; `searchFunctionProvider` is to be overridden with the library view). Choices left to the owner and what the screens still lack are listed in the PR. Gate switches added: rating stars placement (page 2 only, or also under the format line) and a line showing whether the system volume has ever failed (owner decision 10: the fallback is not chosen).
- 2026-11-22: Library view on the scanned database (`feat/app-library-view-drift`): `data/library_view/drift_library_view.dart` is the `LibraryView` behind `libraryViewProvider`, sorted by `collateKey`; removed `sort_name.dart`, the manifest fallback, the `gate-sample:` mapping, the `assets/gate_library` bundle and `NEUPOD_GATE_LIBRARY`. The manifest adapter is now test-only (`mobile/test/support/`) for goldens and the gate-2 tool. Still gate-only: Settings stub, `StandInScreen`. The database has no album artist, year, disc, genre or compilation flag yet, so Genres and Compilations are empty and artist/album lists are by track artist and album until phase 3.
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
