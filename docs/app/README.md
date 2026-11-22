# neuPod phone app: specification

A music player for iOS and Android that looks and behaves like a neuPod, and later talks to one.

Status: specification, 21 November 2026. Nothing is built. The working instructions for building it are in
[TEAM-BRIEF.md](TEAM-BRIEF.md) and the checklist is [PROGRESS.md](PROGRESS.md).

## Owner decisions (21 November 2026)

1. **The app is both** a full player on the phone and, later, a companion to a neuPod device.
2. **Music comes from three places, in this order of delivery:** files on the phone first; then the owner's NAS; then
   the card in a neuPod.
3. **Built in Flutter**, one codebase for both platforms.
4. **iOS is built and tested for real**: the owner has a Mac and an Apple developer account.

**Owner decisions in phase 0 (21 November 2026)**, after the platform research in [decisions/](decisions/):

5. **Ogg Vorbis and Opus on iOS** are played through bundled Xiph decoders (libogg, libvorbis, libopus, BSD-3), because
   iOS has no decoder for them that apps can use.
6. **No Cast in R1.** Casting from Android needs the app's own network connection. The output icon covers the phone
   speaker, wired, Bluetooth and, on iOS, AirPlay (the system does AirPlay's networking).
7. **iCloud files that are not downloaded** are listed with a Download action. The app never fetches them itself; a tap
   asks iOS to download the file.
8. **Platform limits, accepted:** on iOS a scan cannot keep running after the app is closed, so it resumes at the next
   launch (Android keeps going). Tracks from Android's shared music collection get embedded art only, because folder
   art would need a restricted image permission. Android 11+ cannot pick the storage root or `Download`, so the folder
   screen says so. Sample-rate passthrough is best effort and reported, never claimed (Android 14+ USB DACs where the
   phone supports it, an iOS preferred-rate hint, never over Bluetooth). The active output on Android 10 to 12 is
   inferred, because there is no public API before Android 13. MP3 and AAC files without encoder-delay metadata cannot
   be gapless on any engine.

9. **The app is the device (owner, 21 November 2026).** The full-screen view is the front of a neuPod: the 4:3 screen
   above a touch click wheel with the four click buttons (Menu, previous, next, play/pause) and the centre button, in
   the device's proportions, scaled to the phone's width. Turning the wheel scrolls with haptic detents, and pressing a
   button or the centre gives a haptic click. It is drawn fresh, never copied from Apple's artwork, with no Apple logo
   and no "iPod"; the store-review risk stays with the owner at submission (Open question 1). This settles Open
   question 2 and most of Open question 3.
10. **The wheel sets the volume on Now Playing (owner, 21 November 2026)**, as on the device, on both platforms. It
    sets the phone's own system volume (no separate app volume): Android through `AudioManager`, iOS through a hidden
    `MPVolumeView`, the common workaround, since iOS has no public API for it. The device's volume bar replaces the
    progress bar while the wheel turns. If Apple breaks the workaround, the fallback is to be asked for, not guessed.
11. **Sort rule: the app matches the firmware's code (owner, 21 November 2026)**, `lib_collate_key()` in
    `app/library/lib_collate.c`, and the rule is written down under [The library](#the-library). The owner may change
    it later, in the firmware and the app together.
12. **Search is typed with the wheel (owner, 22 November 2026)**, as text is on the device. It never opens the phone's
    keyboard. It uses the firmware's text entry in search mode (`docs/ui-input.md`, model in
    `app/include/wheel_input.h`): the letter ring and mode chips, with live results as the user types. The chips are the firmware's six: `abc`,
    `ABC`, `123`, `#!?`, `space`, `del` (owner, 22 November 2026; the design drew five, without `#!?`). The screen is in
    the [device view design](../design/2026-11-21-app-device-view/README.md), "Search, by the wheel".
13. **Gate 2, first choices (owner, 22 November 2026)**, from the screenshots of the device view: **fit A** (the body
    fills the phone; device view design Open question 1), and the **body finish follows the mode** (silver in Light,
    black in Dark) with no separate Silver/Black setting (Open question 5). Also: the phone's own status bar is
    **shown** above the device; the **neuPod wordmark stays on** the body (Open question 8); **rating stars live on
    Now Playing's page 2 only**, as a Rating row set with the wheel; Search keeps its **24-character cap**.

    **Gate 2, remaining choices (owner, 22 November 2026).** Device view Open questions: the accessibility text sizes
    stay **capped at 5 rows** (3); **holding Play/Pause does nothing** extra (4); **our own proportions** stay (6);
    routes: the phone speaker shows **no icon**, USB and other wired outputs show the **jack plug**, and AirPlay and
    other wireless outputs show a **new generic external-speaker icon** in the device's style (never Apple's AirPlay
    glyph) (7); a **left-edge swipe is Menu** on iOS (9); the click sound is a **system sound that follows the
    ringer switch** (10); Search stays **in the Music menu** (11); Search shows **up to three live results** (two,
    then one, at the larger tiers) (13). UI styles: Classic Light is **adjusted to 4.5:1** where the designer's exact
    values fall short (the selection bar's upper stops under white text, rail letters, ink3 on grey surfaces), and
    Bluetooth saved versus active gets a cue besides colour; Home's right pane with nothing playing shows a
    **placeholder cover, "Nothing playing" and the song count**; tapping a side cover in Cover Flow **centres it**
    (a second tap or Select opens it); the Music menu's **preview pane shows for every item**, not only Albums.
    Settings: the list under [Settings](#settings) plus the **click sound** switch (off by default) and a **haptics**
    switch. **Direct touch** (Open question 2) and **drag-to-seek** are judged by feel on the owner's phones at
    gate 1: until then the build defaults to **candidate 2** (wheel plus screen, candidate 1 kept behind the gate
    switch) and the progress bar is **not draggable**.

Everything else in this document is the designer's proposal until the owner confirms it. Proposals that most need a
decision are collected under [Open questions](#open-questions).

## What it is for

The owner is re-ripping about 300 CDs to FLAC as the library for the neuPod. The app gives the same library, the same
menus and the same look on the phone he already carries, and becomes the easy way to manage a neuPod once the device
has a network interface.

**Goals**

- Play a large lossless library from the phone, gaplessly, in the background, with the controls people expect on the
  lock screen and on headsets.
- Feel like a neuPod: the same menu structure, vocabulary and visual styles
  ([Classic and Modern](../design/2026-11-21-ui-styles/README.md)), adapted to touch and to a portrait screen.
- Work fully offline with no account.

**Not goals**

- Streaming services, a store, social features, recommendations, lyrics, podcasts (podcasts are a later device feature).
- Video.
- Editing tags or files. The app reads the library; it never changes the owner's music files.
- Tablets and desktop layouts in the first release.

**It has failed if**

- it ever alters, moves or deletes a music file without being asked;
- playback gaps between tracks of a continuous album, or stops when the screen locks;
- it sends anything about the library off the phone;
- it looks like a generic player with a neuPod logo on it.

## Releases

| Release | What it adds | Depends on |
| --- | --- | --- |
| **R1: local player** | Everything in this document's "R1" sections: files on the phone, library, playback, both styles | Nothing. **This is the work to do now.** |
| **R2: NAS** | Browse, stream and download from a home server | An owner decision on the server (see Open questions); R1 |
| **R3: companion** | Find a neuPod on the network, browse and play its card, send music and playlists to it, remote control, device settings | The device's HTTP API, which does not exist yet (it is step W2 in [Wi-Fi](../wifi.md)); R1 |

R2 and R3 are outlined at the end so R1 is built with room for them. They are not to be started without the owner.

## R1: the local player

### Getting music onto the phone

- **Android:** the user picks one or more folders with the system folder picker; the app keeps read access to them. It
  also offers the device's shared music collection.
- **iOS:** the app has its own Documents folder, visible in the Files app and in Finder when the phone is plugged into
  a Mac, so a library is copied in by dragging folders. The user can also pick a folder elsewhere in Files (for example
  on iCloud Drive or an attached drive) and the app keeps access to it.
- The app never copies music it can read in place, and never writes into a music folder.

### Formats

Must play: FLAC (up to 24-bit / 192 kHz), ALAC, MP3, AAC (M4A), WAV, Opus and Ogg Vorbis. These match the device's
required codecs. A file the platform cannot decode is listed greyed with the reason at the right (`Opus, can't play`),
exactly as the device does; it is never hidden and never stops the queue.

### The library

- Built by scanning the chosen folders and reading tags: title, artist, album artist, album, track and disc number,
  year, genre, compilation flag, duration, sample rate and bit depth, embedded art.
- **Where tags are missing, fall back to the folder convention** the owner's library uses:
  `Artist/Year Album/NN Title.ext`, with `Folder.jpg` (or `cover.jpg`) as the album art. A trailing `, The` on a
  folder name is displayed as a leading `The` and sorted without it.
- Discs of one album (`(Disc 1)`, `(Disc 2)` in tags or folder names) are one album with disc sections.
- A track whose album artist is `Various Artists`, or whose compilation flag is set, belongs to Compilations and is not
  listed under each contributing artist's albums.
- The scan runs in the background, shows progress, can be cancelled, and survives the app being closed (on iOS it resumes at the next launch; owner decision 8). A rescan only
  reads files that are new or changed. Removing a folder removes its tracks from the library and nothing else.
- Sizing target: 20,000 tracks. First scan of 5,000 FLAC files in under five minutes on a mid-range phone; a rescan
  with no changes in under ten seconds. (Targets are proposals; measure and report.)
- **Sort rule (owner decision 11):** names sort by a collation key built as the firmware's `lib_collate_key()` does.
  A sort tag is used when present, otherwise the name. One leading article is skipped, in any case, when something
  follows it: `The `, `A ` or `An ` ("The Beatles" under B, "A Perfect Circle" under P). Letters compare without
  case and with accents folded. Order: symbols first, then digits, then letters. The fast-scroll letter is the
  first letter of the key, `#` for a digit or symbol. `docs/ui.md` still says "A "/"An " are not skipped; the code
  is the reference until the owner changes both.
- Ratings (0 to 5), play counts, last played and skip counts are kept per track in the app's own database, keyed so
  that they survive a rescan and a file move within the library.

### Browsing

The same structure as the device:

- **Home:** Music, Playlists, Settings, Shuffle Songs, Now Playing (only while something is loaded).
- **Music:** Cover Flow, Playlists, Artists, Albums, Songs, Genres, Compilations.
- **An artist:** All Songs, then the albums with cover, year and song count.
- **An album:** header with the cover, artist, year, song count and total time; numbered tracks, with disc headings.
- **Songs:** two lines per row, title over artist and album.
- **Cover Flow:** albums by artist then year; the centred album opens on tap.
- **Fast scroll:** a letter index on long lists.
- **Search**, typed with the wheel (owner decision 12): the field, the letter ring turned by the wheel with the mode
  chips under it, and the best matches live as the user types; Play/Pause goes to the full results, grouped as
  artists, albums and songs, and Menu returns to typing with the text kept. It never opens the phone's keyboard.

Long names end in an ellipsis in lists and scroll as a ticker where the device scrolls them (the focused title on Now
Playing).

### Playback

- A play queue with Play, Play Next and Add to Queue from any track, album, artist, genre or playlist. The queue is
  visible and can be reordered and cleared.
- **Gapless** between consecutive tracks of the same format; verified by test, not by ear alone.
- Shuffle (off, on) and repeat (off, all, one), with the same glyphs as the device's status bar.
- Seek by dragging the progress bar; skip back restarts the track after three seconds, otherwise goes to the previous one.
- **Background playback** with lock-screen and notification controls, artwork and scrubbing; headset and Bluetooth
  transport buttons; correct behaviour on interruptions (a call pauses; unplugging headphones pauses; another app
  taking audio pauses).
- **Resume:** reopening the app restores the queue, the track and the position.
- Volume is the phone's own volume. The app has no volume of its own; on Now Playing the wheel sets the system
  volume and shows the device's volume bar (owner decision 10).
- The output in use (phone speaker, wired, Bluetooth or AirPlay) is shown by an icon, as on the device. No Cast in R1
  (owner decision 6).
- No resampling or processing in the app. Where the platform lets the app pass the file's sample rate through to an
  external DAC, do so and say what was achieved in the format line; do not claim bit-perfect output that has not been
  measured.

### Playlists

- **On-The-Go:** add from any row; reorder; clear; save as a named playlist.
- Named playlists: create, rename, reorder, delete.
- Import and export `.m3u8`, with paths relative to the library folder, so playlists can move between the phone and a
  neuPod's card.

### Now Playing

Art, title, artist, album and year, the small format line (`FLAC 16/44.1`), progress with elapsed and remaining, shuffle
and repeat, rating stars, and a route to the queue. In Modern the backdrop and the accent come from the album art, by
the same rule as the device: the dominant colour, clamped until white text on it reaches 4.5:1.

### Look and feel

- Two styles, **Classic** and **Modern**, each in Light and Dark, with the colours and skins in the
  [UI styles design](../design/2026-11-21-ui-styles/README.md). Mode follows the phone's setting by default; style is
  chosen in Settings.
- **The device view (owner decision 9).** The phone shows the device's front: the 4:3 screen, running the device's
  screens with the UI styles design's values (drawn at 600x450, so they carry over), above the click wheel. The wheel
  behaves as on the device: turning scrolls, with acceleration and the fast-scroll letter tile on long lists; the
  centre is Select; Menu goes back; previous, next and play/pause work as on the device, including press-and-hold to
  scan. Each wheel detent is a light haptic tick, and each button press is a firmer haptic click. An optional click
  sound, like the original's clicker, can be turned on in Settings.
- **The layout and the wheel's numbers are in the [device view design](../design/2026-11-21-app-device-view/README.md)**: geometry as ratios of the
  phone's width, the fit on tall and short phones, body finishes, hit zones, press-or-turn, detents and acceleration
  (a port of the firmware's `ui_accel_feed()`), holds, haptics, direct touch, the three text tiers, accessibility and
  the phone's status bar. Its recommendations (the fit, the body finish, direct touch, the phone's status bar shown,
  the large-text tiers, the wordmark) are proposals the owner picks at gate 2; where it offers candidates, both are
  built and switchable. Its Open questions are the owner's.
- Search is typed with the wheel's letter ring, never the phone's keyboard (owner decision 12). Its placement is device
  view design Open question 11; it sits in the Music menu meanwhile.
- Type is Montserrat (SIL OFL), icons are Lucide (ISC), as on the device. Every third-party asset and package is listed
  on an in-app Licences screen.
- Feedback follows the device's three places where they translate: a row confirms with a brief flash and a tick; a
  greyed row carries its reason; something lost or about to be lost gets a centred panel. No toasts for routine things.

### Settings

Style and mode; library folders (add, remove, rescan); gapless on or off; skip-back behaviour; click sound (off by
default); haptics (on by default, and always off when the phone's own setting is off); reset ratings and play counts
(with a panel to confirm); About; Licences (owner decision 13).

### Accessibility

The wheel and buttons have screen-reader equivalents (the list is navigable row by row, and the wheel is an
adjustable control with actions for Select, Menu, previous, next and play/pause). Every control has a screen-reader label; the app honours the system text size up to the largest standard setting
without clipping; text contrast is at least 4.5:1 in every style and mode; nothing depends on colour alone; touch
targets are at least 44 points.

### Privacy and safety

- R1 makes **no network connection at all**. No analytics, no crash reporting service, no accounts. iOS downloading an iCloud file
  after the user taps Download (owner decision 7), and AirPlay (decision 6), are the system's networking, not the app's.
- The app requests only the permissions it needs to read the chosen folders, play in the background and show media
  controls.
- Signing keys, provisioning profiles and store credentials are never committed. The repository is public.

## How it is built

- **Flutter**, current stable, one codebase in `mobile/` at the repository root. Licence MPL-2.0, like the firmware.
- Layers, each testable without the ones above it: a pure-Dart **domain** (library model, queue, shuffle and repeat
  rules, sorting and naming rules); **data** (database, scanner, tag reader, art cache); **audio** (the player and the
  platform media session); **UI**.
- Packages (audio engine, media session, tag reading, database, state management) are the team's choice. Each choice is
  recorded in a short decision note with the alternatives considered. A package must be actively maintained, licensed
  compatibly with MPL-2.0 distribution on both stores, and checked against its current documentation, not chosen from
  memory.
- The domain rules that the device also implements (sort order, `, The` handling, compilation rules, queue behaviour)
  follow the firmware's behaviour as documented in [UI](../ui.md) and the ADRs, so a library looks the same in both.

## How it is tested

| Level | What | Where |
| --- | --- | --- |
| Unit | Domain rules, scanner, tag fallbacks, playlist import and export, the accent-colour rule | Every PR |
| Widget and golden | Every screen in Classic and Modern, Light and Dark, at a small and a large phone size and at the largest text size | Every PR |
| Integration | Pick a folder, scan, browse, play, background, resume; on an Android emulator and an iOS simulator | Every PR (CI: Linux for Android, a Mac runner for iOS) |
| Audio | Decode each fixture in `tests/assets/audio` and compare with its checksum where the platform allows; gapless join measured on a generated two-track fixture | CI where possible, otherwise the bench |
| Device | The owner's iPhone and an Android phone: real library, lock screen, headset buttons, Bluetooth, interruptions, an hour of continuous play | End of each phase, recorded with the date |
| Scale | A generated 20,000-track library: scan time, memory, scroll smoothness | End of the library phase |

The repository's audio fixtures (`tests/assets/audio`) are reused; a generated library with the owner's folder
convention and awkward names is added for scanner tests. The owner's own music is never committed.

## R2 outline: NAS

Browse and play from a home server; download albums for offline use; show clearly what is on the phone and what is
remote. R1 keeps the library model source-neutral (a track has a source and a location) so this is an addition, not a
rewrite. The server type is an open question.

## R3 outline: companion

Discover a neuPod on the local network; show its battery, storage and what it is playing; remote control; browse and
play its card; send albums and playlists to it and remove them; exchange ratings and play counts; set its settings;
later, firmware update. All of this needs the device's HTTP API. That API is to be specified in an ADR written with the
firmware, not invented by the app. [Wi-Fi](../wifi.md) currently describes that step as "device HTTP API and a C#
companion app for library sync"; whether a separate desktop companion is still wanted is an open question.

## Open questions

Not decided. Do not settle these by guessing.

| # | Question | Proposal |
| --- | --- | --- |
| 1 | **App name and store listing.** A music player whose name contains "Pod" may meet trademark objections at store review. Not legal advice; the owner should take a view before submitting | Build as "neuPod"; nothing is submitted to a store without the owner |
| 2 | **An on-screen click wheel.** Apps that reproduce the original wheel have been removed from the App Store: Rewound, a click-wheel iPod skin for the iPhone, was pulled in December 2019 ([MacRumors](https://www.macrumors.com/2019/12/16/rewound-app-pulled-from-app-store)), and the App Review Guidelines bar apps "confusingly similar to an existing Apple product, interface … or advertising theme" (cited as 5.2.5). Store review is the owner's risk at submission (Open questions 1 and 9) | **Decided 2026-11-21 (owner decision 9):** a touch click wheel with haptics, in a drawn-fresh device view |
| 3 | **Portrait layout**: the split Home with the art pane, the status bar, where Now Playing lives (a full screen, or a mini-player above a tab bar) | Mostly settled by owner decision 9: the screen is the device's 4:3 screen. How the device fits tall and short phones is decided at the design gate |
| 4 | **Bundle and application ID** | **Decided 2026-11-21:** `net.stuartfraser.neupod` |
| 5 | **Minimum OS versions** | **Decided 2026-11-21:** iOS 18 and later; Android 10 (API 29) and later |
| 6 | **Which Android phone** is the test device | **Decided 2026-11-21:** the owner's Pixel (model and version to be recorded) |
| 7 | **NAS type** for R2: a Jellyfin-style server, plain SMB, or WebDAV | Ask when R1 is done |
| 8 | **Desktop C# companion** in the Wi-Fi plan: still wanted alongside the phone app? | Ask when R3 is planned |
| 9 | **Distribution**: TestFlight and an internal Android track only, or public store releases | TestFlight and sideloaded APK until the owner decides |
| 10 | **Sync of ratings and play counts** between phone and device | R3 |
