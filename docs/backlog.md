# Backlog

Open work, mostly UI, that is not covered by an ADR or a PR yet. One line per item, newest
requests at the top of each section; delete a line when its PR merges (the git history keeps it).
Larger subsystems (library, Bluetooth stages, final board) live in their ADRs and are only
pointed at here.

Columns: **Source** is where the item comes from (spec = [UI mock-ups](ui.md), req = Stuart's
request with the date, bench = seen on the testbed). **Needs** is what blocks it, if anything.

Platform note (12 Nov 2026, [ADR-0001 amendment](adr/0001-rp2350-phase1-platform.md#amendment-12-november-2026-the-pico-2-w-is-frozen-the-product-target-is-the-rp2350b-with-psram)):
the Pico 2 W build is frozen as a compile check; items may assume the RP2350B with PSRAM and be compiled out of the Pico 2 W build.

## UI — ready to do (no blocker)

| ID | Item | Source | Needs | Notes |
| --- | --- | --- | --- | --- |
| UI-25 | **Full album art on the AMOLED dev boards** (ADR-0022 gap): **built** (25 Nov 2026, [specs/001-amoled-full-art](../specs/001-amoled-full-art/spec.md)): both AMOLED builds have the big-RAM player (`NEUPOD_ART_HIRES`) with its buffers in PSRAM. Left: the bench checks (boot line, art, Cover Flow speed, LVGL pool peak, warm restarts) on both boards. | owner 24 Nov 2026 | boards on the bench | Delete this line once the bench checks pass. |
| UI-22 | **The queue line shows library track names**: Now Playing's `n of N` line (and the queue it describes) names the track from the library's tags, not the file name, when the queue came from a library list. | req 12 Nov 2026 | — | The library reader has the title; the range queue only knows the song index today. |
| UI-21 | **Resume restores the album or shuffle queue**, not just the one track: the resume record keeps the track and its place; it should also bring back the queue it was chosen from (album, artist, genre, all songs, a folder; shuffle with its key) so Next / Previous work after a restart. | req 12 Nov 2026 | — | [ADR-0017](adr/0017-music-library-index.md) range queues are stateless, so the record needs the range and the shuffle key, not a list. |
| UI-20 | **Genre → Albums**: from a genre, a list of albums (as Artists has), not only All Songs and artists. | req 12 Nov 2026 | — | Needs a per-genre album grouping in the index (a table like `by_genre.u32`, ADR-0017 L2). |
| UI-19 | **Playlists**: the Music > Playlists list (M3U / M3U8 on the card, in Files order), a playlist as a queue. | spec | — | Was UI-9's last piece. Parse on the storage task, entries resolved through the library or by path. |
| UI-4 | **Fast-scroll letter overlay** on long lists (first letter of the row under the bar while scrolling fast). | spec (Library) | — | Scroll bar exists (`ui_list.c update_scrollbar`). Only useful once lists are long: Files on a big folder today, library later. |
| UI-8 | **Wi-Fi status-bar icon**: today one Wi-Fi glyph coloured by the signal class of the joined network (hidden when the station is off); remainder: draw the signal as bars in the status bar like the lists do, and a distinct "joining" look. | spec (common frame) | — | The Bluetooth glyph (remembered / connected / streaming) is done. Keep to one glyph; the detail lives in Settings. |
| UI-7 | **Seek remainder**: FLAC without a SEEKTABLE (needs a decode-from-start fallback, seconds of silence, or a frame scan) and AAC (not playable yet); **audible fast-forward** while Right / Left is held (today the hold moves a target and applies it on release); a Fraunhofer VBRI byte table is read as if its first stretch includes the VBRI frame (not checked against a real file). | spec (Now playing) | — | MP3 and ALAC seek are built (UI-7, 12 Nov 2026); see [Audio](audio.md#gapless-playback-and-the-play-queue). |

| UI-24 | **Shuffle Songs can start on a track that cannot play** (the library's AAC file: "Can't play: unsupported codec (AAC)", Now Playing shows the error). Skip unplayable tracks when picking the first one, or leave it? It made the library tour wait forever (found 14 Nov 2026 as a CI failure; the library tours' Shuffle step now uses `wait:pause-or-err`). | CI 2026-11-14 | decision | The library already knows which tracks are playable (`16 tracks (15 playable)`). |
| TS-1 | **Library tours are still partly clock-driven**: the `x` steps are 300 ms of the UI clock and the script takes ~70 s, so `--run-ms` (240 s) is a cap that a slow runner can still reach. Fixed 14 Nov 2026: the pause wait and the touch contacts no longer race (see UI-24 and the touch tour). Remaining: make the other fixed steps wait on events (screen pushed, track started) instead of counting steps. | CI 2026-11-12 | — | |
## UI — checks and loose ends from the Hold / screen-off work (12 Nov 2026)

| ID | Item | Source | Needs |
| --- | --- | --- | --- |
| UI-18 | **Hold on the device**: run the Y2 steps in [bench.md](bench.md#hold-y2) (including the Lock hold steps, 2026-11-19: Settings > Display > Lock hold, default Off) (ring, lock, swallowed keys, countdown, power-off with exit 77, screen off and wake, haptic click, headset keys while locked); **assign the final board's Hold-switch GPIO** (`hal_input_hold_switch()` is `TODO(hw)` on the RP2350 HAL); decide whether a button held down when the screen goes off should be released (a fast-forward in progress ends by its own 30 s timer today). | req 12 Nov 2026 | Y2 on the bench; iPod 6G donor board |
| UI-6 | **Rapid presses (bench check)**: the queue is 64 deep, the UI wakes on input and drops are counted (`input: WARNING n events dropped`); send 20 UART keys in one burst on the Pico rig and confirm none are lost. | bench 2026-11-03 | wheel / UART on the bench |
| UI-23 | **Licences screen, remainder**: the on-device list shows a licence name and a short notice; the full texts (SIL OFL, CC BY 4.0, Apache-2.0, ChaN's licence) are in the source tree only; BTstack's commercial terms (a final board that is not a Raspberry Pi product, and the Y2) are an open legal question ([LICENSE.md](https://github.com/StuartF303/neuPod/blob/main/LICENSE.md)). | req 12 Nov 2026 | decision on shipping full texts; BlueKitchen answer |

## Phone app 2026-11-21

A Flutter music player for iOS and Android in the neuPod styles, later a companion to the device: [specification](app/README.md). Release R1 (files on the phone) is tracked in its own [progress list](app/PROGRESS.md), not here. R3 (companion) depends on the device HTTP API, step W2 in [Wi-Fi](wifi.md).

| ID | Item | Source | Needs | Notes |
| --- | --- | --- | --- | --- |
| AP-1 | **Custom mode for the device view**: Mode becomes Light, Dark, Auto, Custom. Custom sets the colour and transparency of the body, the wheel and the centre button with a colour wheel, carries its own screen choice, and shows a neuPod board picture through a see-through part. Up to six saved looks; three starters ship. | req 25 Nov 2026 | [design](design/2026-11-25-app-custom-mode/README.md) written, awaiting the owner on its Open questions; **real board pictures** (KiCad renders or photos of neuPod's own boards; `boards/` is empty); settings storage in the app (app progress list, phase 6) | Build from the design's [brief](design/2026-11-25-app-custom-mode/IMPLEMENTATION-BRIEF.md), tracked in its [progress list](design/2026-11-25-app-custom-mode/PROGRESS.md). Paint only: no geometry or hit zone changes. |

## Device and phone together 2026-11-24

The phone has no card slot, so the device is the full offline library and the phone carries part of it. Three ways to play: the device plays, the phone plays from the device's card, or either plays to Bluetooth or network speakers. Owner: all of it is wanted eventually; the order is open. Working split: Bluetooth for control, Wi-Fi (or the cable) for the music itself, because no Bluetooth mode in today's radios carries CD-quality FLAC (about 1 Mbit/s, peaks to 1.4) to a phone with margin: see CN-7.

| ID | Item | Source | Needs |
| --- | --- | --- | --- |
| CN-1 | **The app as a remote for the device**: find the device, transport, volume, now-playing, and wake its Wi-Fi on request, over Bluetooth LE. | req 24 Nov 2026 | BLE in the firmware (BTstack is built Classic only today; RAM per [ADR-0013](adr/0013-bluetooth-ram-psram.md)); app R3 |
| CN-2 | **The phone plays from the device's card**: the device serves its files and library index over HTTP, the app browses and streams them. | req 24 Nov 2026 | W2 in [Wi-Fi](wifi.md); Wi-Fi is off during playback by default (one radio shared with Bluetooth); app R3 |
| CN-3 | **Offline pinning**: the app copies chosen albums from the device to the phone's storage, so they play with the device away. | req 24 Nov 2026 | CN-2 |
| CN-4 | **The device as its own access point** for use away from a home network; the app asks the phone to join it (iOS shows a prompt, and the phone's Wi-Fi is taken while joined). | req 24 Nov 2026 | CN-2; access-point mode in `hal_wifi` |
| CN-5 | **By cable**: plugged into the phone with USB-C the device's card is a drive, and the app opens it as a library source. No radio. | req 24 Nov 2026 | USB mass storage on the final board ([requirements](requirements.md)); app: a folder on an external drive as a source |
| CN-6 | **Network speakers** (AirPlay, Cast, DLNA) are driven from the phone app, with the device only serving files; the device itself sends to Bluetooth speakers as now. Differs from Wi-Fi stage W5 ([requirements](requirements.md#wi-fi-feature-stages-4-nov-2026)), where the device itself is the DLNA control point; which of the two drives network speakers is open. | req 24 Nov 2026 | CN-2; the app's "No Cast in R1" decision revisited |
| CN-7 | **Investigate other Bluetooth modes for lossless.** (a) Measure LE 2M PHY throughput device to iPhone and to Android over an L2CAP channel: about 1.4 Mbit/s chip to chip, less to a phone, so at best a slow download, not a live stream. (b) Classic serial port (RFCOMM) to Android; iOS does not allow it without MFi. (c) LDAC as a second A2DP source codec for headsets that take it (lossy, 990 kbit/s, open encoder; not AirPods). (d) Bluetooth 7.0 High Data Throughput (adopted Nov 2026, up to 7.5 Mbit/s): needs new radios at both ends, so keep the radio replaceable behind `hal_radio.h` and look again when modules and phones ship. | req 24 Nov 2026 | bench time; (a) needs CN-1's BLE |
| CN-8 | **Power cost of each mode**: BLE remote, Wi-Fi serving (continuous and in bursts with the radio asleep between tracks), access point, cable. | req 24 Nov 2026 | CN-1, CN-2 on the bench |

## UI styles 2026-11-21

Classic and Modern styles, the status bar, the three feedback places and the Music screens: [design](design/2026-11-21-ui-styles/README.md). The work is tracked item by item in its own [progress list](design/2026-11-21-ui-styles/PROGRESS.md), not here. It overrides parts of the 12 November review (the toast strip, the volume HUD, the Now Playing pills and volume strip, the status-bar clock); the design's README lists them.

## Design review 2026-11-12

From the owner-commissioned [design review](design/2026-11-12-ui-review/README.md) ([report](design/2026-11-12-ui-review/REPORT.md), mock-ups in `design/2026-11-12-ui-review/mockups/`). Owner decisions that override the report are in the README there. Order: phase 1 foundation, phase 2 Now Playing and library rows, phase 3 Bluetooth, phase 4 the rest. Effort (S/M/L) and impact (1 to 3) are the review's.

| ID | Item | Area | Effort | Impact | Depends on | Phase | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| DS-1 | Colour token table + light/dark structs, Theme setting (Light / Dark / Auto on dim) | System | S | 3 | — | 1 | built (phase 1) |
| DS-2 | Add Small font size (12/16) to both profiles | System | S | 3 | — | 1 | built (phase 1) |
| DS-3 | Status bar: left slot, battery % rules, BT glyph states | System | S | 3 | DS-1 | 1 | built (phase 1) |
| DS-4 | Selection bar restyle (inset, radius, 80 ms slide) and grey chevrons | System | S | 2 | DS-1 | 1 | built (phase 1) |
| DS-5 | Toast strip component (top) and HUD strip (bottom) | System | M | 3 | DS-1, DS-2 | 1 | built (phase 1) |
| DS-6 | Lucide icon set, replace FontAwesome where covered | System | S | 2 | — | 1 | built (phase 1) |
| DS-7 | Push/pop slide transition, 30 fps cap | System | M | 2 | DS-4 | 1 | built (phase 1) |
| HL-1 | Lock pill and Power-hold progress line | Hold/Power | S | 2 | DS-5 | 1 | built (phase 1) |
| NP-1 | Now Playing layout: tag stack, pills, progress, volume strip | Now Playing | M | 3 | DS-2, DS-3 | 2 | built |
| NP-2 | Seek mode: Big amber time, knob, hints, no third bar; entered by hold Select | Now Playing | S | 2 | NP-1 | 2 | built |
| NP-3 | Now Playing page 2 (rating, shuffle, repeat, go-to); Select opens it | Now Playing | M | 2 | NP-1 | 2 | built (ratings on the card; Add to playlist greyed until LB-7) |
| NP-4 | Volume HUD from any screen | Now Playing | S | 2 | DS-5 | 2 | built |
| NP-5 | Art pipeline: 160² thumbnails + LRU (PSRAM board and Y2 only) | Now Playing | M | 2 | — | 2 | built (host, Y2, and the RP2350 / ESP32-S3 AMOLED boards with the cache in PSRAM, UI-25) |
| LB-1 | Two-line song rows (title / artist · album) | Library | S | 3 | DS-2 | 2 | built |
| LB-2 | Fast-scroll letter overlay + index rail, sort keys ignoring articles/diacritics (UI-4) | Library | M | 3 | DS-2 | 2 | built ("The " and accents ignored; "A " / "An " still sort under A: see LB-2b) |
| LB-3 | Album header (art, title, artist · year, totals) | Library | S | 2 | NP-5 | 2 | built |
| LB-2b | Sort keys also ignore "A " and "An " (changes the index order: bump the index version, rebuild) | Library | S | 1 | LB-2 | 4 | built (index format 3; older indexes rebuild automatically) |
| NP-6 | Ratings in the library: show them in lists, a Rated / Top rated smart list (the stars are saved in `/.neupod/ratings.bin` by path and size) | Now Playing | S | 1 | NP-3, LB-7 | 4 | built (phase 4b): the Top rated smart list (four stars or more, from the plays store); ratings are not shown in song rows yet |
| LB-4 | Empty / error / busy states as full-pane layouts, determinate progress | Library | M | 3 | DS-5 | 4 | built (full-pane states, counted progress, run in background with status-bar dots) |
| LB-5 | Unplayable rows show codec; auto-skip with toast | Library | S | 2 | DS-5 | 4 | built (codec on greyed rows, skip toast in queues) |
| LB-6 | Cover Flow (flat five-tile) view on Albums (UI-11) | Library | M | 2 | NP-5 | 4 | built (phase 4c; not on the frozen Pico 2 W): Right on Albums, flat five tiles, 90 ms step, `art_browse_want_set()` with 5 handles. Open: the year is the first song's only; Compilations has no Cover Flow
| LB-7 | Playlists screen (smart lists + .m3u) (UI-19) | Library | L | 2 | — | 4 | built (phase 4b; not on the frozen Pico 2 W). Open: long-press Select on a song row to add it to On-The-Go (only Now Playing page 2 adds), a count shown for Unplayed beyond the first 128, reordering On-The-Go, writing .m3u files |
| BT-1 | Devices screen (known / found / scan), signal bars monochrome | Bluetooth | M | 3 | DS-6 | 3 | built (phase 3) |
| BT-2 | Forget / pairing / reconnect flows with toasts; no self-mutating rows; link lost ends "Couldn't reconnect — paused" (never switches output) | Bluetooth | M | 3 | DS-5, BT-1 | 3 | built (phase 3) |
| BT-3 | Bluetooth settings page; codec detail moves to Diagnostics | Bluetooth | S | 1 | BT-1 | 3 | built (phase 3): Audio output Auto / Wired / Bluetooth, Devices, Absolute volume (read-only). Open: Bluetooth On / Off (no clean radio power-down in the HAL), an Absolute volume switch (no AVRCP code to ignore it), Rename a headset |
| ST-1 | Settings right pane as key/value summary; toggles in place | Settings | S | 2 | DS-2 | 4 | built (key/value pane, toggles in place with tick and 60 ms flip; list order unchanged: About is still first) |
| ST-2 | Display page (theme, brightness, timeouts); the Theme row itself is built with DS-1 | Settings | S | 2 | DS-1 | 4 | built (Theme, Brightness, Dim after incl. Off, Off after, Auto-lock) |
| ST-3 | About › Diagnostics, Licences screen | Settings | S | 1 | — | 4 | built (Licences, UI-23; Diagnostics with the boot checks, audio and Bluetooth link counters, phase 3) |
| IN-1 | Text entry: letter ring, mode chips, field box | Input | M | 2 | DS-2 | 4 | built (phase 4c): ring of nine glyphs, mode chips, field box with caret, password reveal 800 ms, Menu hold clears; the text no longer has a movable cursor (Left / Right are the chips)
| IN-2 | Number/slider/date-time polish | Input | S | 1 | IN-1 | 4 | built (phase 4c): Big value, bar and min / max for number and slider, field names under date / time, choice stays a plain list
| FL-1 | Files: path in status bar, icons, paged "Load more" | Files | S | 1 | DS-3, DS-6 | 4 | built (phase 4c): Load more / Earlier entries paging (`storage_list_request_page`), `256+` on a folder row once it has been opened (folders are not counted in advance); icons and the path in the status slot were already there
| HM-1 | Home panel: playing (art, title, artist, progress, times) and idle (48 px clock, date, "N songs . NN %"), Now Playing greyed when nothing is queued, library-not-built prompt, card line behind `NEUPOD_UI_DEBUG` | Home | M | 3 | DS-3 | 4 | built |
| SP-1 | Release splash; self-test to debug splash | Splash | S | 1 | — | 4 | built (phase 4c): release splash (wordmark, progress line, version, 600 ms minimum; the check **icon row was reinstated at the owner's request on 2026-11-13** as Lucide icons, after the first cut replaced it with text); debug splash with the failed-check list when a check fails or Menu is held at start

## UI — blocked on other work

| ID | Item | Source | Needs |
| --- | --- | --- | --- |
| UI-10 | **Podcasts** list and per-episode resume. | spec | library |
| UI-11 | **Album browser (Cover Flow), the 3D version**: full-screen art carousel with reflection (the flat five-tile view is built: LB-6). | spec | RP2350B + PSRAM board ([ADR-0001 amendment](adr/0001-rp2350-phase1-platform.md#amendment-12-november-2026-the-pico-2-w-is-frozen-the-product-target-is-the-rp2350b-with-psram), [ADR-0013](adr/0013-bluetooth-ram-psram.md)); art at more than 64 px |
| UI-13 | **Settings → Output** (headphone / line / optical / USB) when jack detect cannot decide. | spec | final board jack detect |
| UI-14 | **Click: Piezo** actually clicks (setting exists, shows `Piezo not fitted yet`). | #23 | piezo driver + part on the testbed |
| UI-15 | **Photos** browser. | spec | RP2350B + PSRAM, JPEG decode at screen size |
| UI-16 | **Now Playing: rating / shuffle / repeat page** on a further Select (spec says "not drawn"). | spec | (superseded by NP-2 / NP-3: Select opens page 2, hold Select seeks) |

## Non-UI, tracked elsewhere

- Music library index: [ADR-0017](adr/0017-music-library-index.md); the lists are built (L2), the gaps are UI-19 to UI-22 above.
- Y2 internal speaker and Line out (owner decisions 2026-11-14): built on the host and in the Y2 firmware, bench steps in [Y2](y2.md#wired-audio-bench-steps); open: listen on the device, choose the speaker's default level (9 is the cap), decide whether the speaker may also follow a Bluetooth drop.
- Bluetooth: stages 2a to 2c and the reconnect policy are built, what is left is in [bluetooth.md](bluetooth.md).
- Bluetooth reconnect-on-play (bench 2026-11-19: no reconnect after an idle hang-up, Play after a reconnect did not start the stream, Play with the headset gone stalled silently): **built on the host, bench pending** ([Reconnecting](bluetooth.md#reconnecting-stage-3-built), steps in [bench.md](bench.md#bluetooth-reconnect-on-play-pico-2-w--airpods)). Play with the remembered headset away waits and connects (two attempts), "Couldn't connect to <name> - paused" if it does not come; a stream the headset will not start is closed and reopened once. Open: the root cause of the unstarted stream on the headset side is unconfirmed until the bench log shows the new `a2dp command rejected` line or the reopen fixing it.
- Hold, the Power key and the screen-off stage: [ADR-0020](adr/0020-hold.md).
- Bench tests still to run: [bench.md](bench.md).
- Final board hardware: [requirements.md](requirements.md), [design.md](design.md).
