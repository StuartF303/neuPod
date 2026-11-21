# Progress: neuPod phone app, R1

The checklist for [TEAM-BRIEF.md](TEAM-BRIEF.md). Tick an item when its PR merges and add the PR number and date:
`- [x] item (#123, 2026-12-01)`. Mark a blocked item `BLOCKED: reason` and leave it unticked.

Every item means: works on Android and iOS, covered by tests at the levels the specification names, analysis clean.

## Phase 0: set-up

- [ ] Environment report (Flutter, Xcode, CocoaPods, simulators, Android SDK, `gh`)
- [ ] Checklist reconciled with the specification
- [ ] `mobile/` scaffold, layering, ignore rules for keys and personal files
- [ ] CI: Android on Linux, iOS on a Mac runner, no signing
- [ ] Decision notes: audio engine, media session, tag reader, database, state management
- [ ] `LICENSE.md` row for `mobile/` (MPL-2.0)

## Phase 1: vertical slice

- [ ] Choose a folder and keep access to it, Android
- [ ] Choose a folder and keep access to it, iOS; app Documents folder visible in Files and Finder
- [ ] Minimal scan into the database
- [ ] Songs list
- [ ] Play a FLAC and an MP3
- [ ] Background playback with lock-screen and notification controls
- [ ] **Gate 1: stop and show the owner**

## Phase 2: look and feel

- [ ] Style and mode tokens (Classic and Modern, Light and Dark) from the UI styles design
- [ ] Classic Dark and Modern Light. BLOCKED: not designed (UI styles Open question 1)
- [ ] Montserrat and Lucide bundled; Licences screen lists them
- [ ] Components: list row (one and two lines), selection and pressed states, header bar, progress bar, panel, art frame
- [ ] Accent from album art, clamped to 4.5:1 with white text
- [ ] Modern backdrop from the art
- [ ] Design-gate screens: Home, a list, an album, Cover Flow, Now Playing, in portrait
- [ ] **Gate 2: stop and show the owner; record the choices here**

## Phase 3: library

- [ ] Tags: title, artist, album artist, album, track, disc, year, genre, compilation, duration, format, embedded art
- [ ] Folder-convention fallback (`Artist/Year Album/NN Title.ext`, `Folder.jpg`), `, The` handling
- [ ] Discs merged into one album with sections
- [ ] Compilations rule
- [ ] Background scan with progress, cancel, and survival across app restarts
- [ ] Incremental rescan; removing a folder
- [ ] Art cache and thumbnails
- [ ] Ratings, play counts, last played, skip counts that survive a rescan and a move
- [ ] Unplayable files listed greyed with a reason
- [ ] Scale test at 20,000 tracks; numbers recorded

## Phase 4: playback

- [ ] Queue: Play, Play Next, Add to Queue; reorder; clear
- [ ] Gapless, with the measured join test
- [ ] Shuffle and repeat (off, all, one)
- [ ] Seek and the three-second skip-back rule
- [ ] Interruptions: call, headphones unplugged, another app
- [ ] Headset and Bluetooth transport buttons
- [ ] Output route icon
- [ ] Resume queue, track and position
- [ ] Each format decodes: FLAC, ALAC, MP3, AAC, WAV, Opus, Ogg Vorbis; fixture checksums where possible
- [ ] Sample-rate passthrough: what each platform allows, measured and reported

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
- [ ] Now Playing, with rating
- [ ] Queue screen
- [ ] Row feedback: flash and tick; greyed row with reason; centred panel
- [ ] Golden tests: every screen, both styles, both modes, two sizes, largest text

## Phase 6: playlists and settings

- [ ] On-The-Go: add, reorder, clear, save as
- [ ] Named playlists: create, rename, reorder, delete
- [ ] `.m3u8` import and export with relative paths
- [ ] Settings: style and mode, folders, gapless, skip-back, reset statistics
- [ ] About and Licences

## Phase 7: finish

- [ ] Accessibility pass: screen reader, text size, contrast, touch targets
- [ ] Performance against the specification's targets
- [ ] An hour of continuous play on the iPhone
- [ ] An hour of continuous play on an Android phone. BLOCKED until a test phone is named (Open question 6)
- [ ] Full pass of every screen in every style and mode on each device
- [ ] `mobile/README.md`: build, run, install
- [ ] **Gate 3: final report**

## Notes

(One line per thing the next session must know. Newest first.)
