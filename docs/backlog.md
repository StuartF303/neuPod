# Backlog

Open work, mostly UI, that is not covered by an ADR or a PR yet. One line per item, newest
requests at the top of each section; delete a line when its PR merges (the git history keeps it).
Larger subsystems (library, Bluetooth stages, final board) live in their ADRs and are only
pointed at here.

Columns: **Source** is where the item comes from (spec = [UI mock-ups](ui.md), req = Stuart's
request with the date, bench = seen on the testbed). **Needs** is what blocks it, if anything.

## UI — in progress

| ID | Item | Source | Needs | Status |
| --- | --- | --- | --- | --- |
| UI-1 | Home right pane shows the playing track's art, title and artist (play/pause + `n of N` on the third line) instead of the static placeholder; clock and card summary only when idle. | spec, req 2026-11-04 | — | `feat/home-now-playing` |

## UI — ready to do (no blocker)

| ID | Item | Source | Needs | Notes |
| --- | --- | --- | --- | --- |
| UI-17 | **Latin-1 glyphs in the UI fonts** (é, ü, ñ, ¿ …) and a display-text filter for anything still outside the font (’ ‘ “ ” – — … to ASCII) on every drawn string: file rows, Now Playing, library lists, not only Bluetooth names. Today such characters draw as an empty box; the test card has `¿` in 22 names (a ripper's stand-in for `?`) and Bluetooth names carry `’`. | req 2026-11-12 (next bug-fix round) | — | Measure the RP2350 flash cost (est. 20 to 40 KB for Montserrat 14/20/28 with U+00A0..U+00FF and a few punctuation code points; 48 pt can stay ASCII). `ui_display_name()` already maps the punctuation for Bluetooth names. |
| UI-3 | **Shuffle Songs** plays the whole card shuffled (walk `/Music` for playable files) instead of the placeholder. | spec | — | Interim until the library exists; the queue already has shuffle (#22). Cap the queue size; say how many were found. |
| UI-4 | **Fast-scroll letter overlay** on long lists (first letter of the row under the bar while scrolling fast). | spec (Library) | — | Scroll bar exists (`ui_list.c update_scrollbar`). Only useful once lists are long: Files on a big folder today, library later. |
| UI-5 | **Backlight auto-dim and off** after inactivity (dim within seconds, off within ~15 s; any key wakes), Display setting for the timeout. | requirements (power) | dim half done | Dim half done: Settings > Display > Auto-dim (default On) and Dim after (10 s to 5 min, default 30 s) drop the backlight to 10 % (never above the set level); any input restores it at once and is delivered to the screen as normal. 10 % is still quite readable on the panel (bench, 6 Nov). Still open, for the power-management work: a second stage that turns the backlight fully off after a further configurable period (default 3 min after the dim), perhaps depending on the hold switch state. There the waking key must not be delivered to the screen. This replaces the ~15 s off in the requirements power budget, so recheck that budget when it is built. |
| UI-6 | **Rapid repeated button presses are dropped** (seen with Menu from Now Playing over UART; later keys land in a different screen). Decide whether the input queue is too shallow or the screen pop is slow, and fix or document. | bench 2026-11-03 | wheel fitted to confirm | Memory note: send one key per step on the bench until fixed. |
| UI-7 | **Seek for MP3 and ALAC** (Now Playing seek mode, scrub and hold-to-fast-forward exist for WAV and FLAC with a seek table; other tracks answer `Seek not available`). | spec (Now playing) | MP3 frame index | ALAC already lands on the exact frame through `track_feed_seek()` (resume) and could be switched on with `track_info_t.seekable`; MP3 lands within a frame (CBR) to about 1 % (VBR), which a scrub bar would show as jumps. FLAC without a SEEKTABLE needs a decode-from-start fallback (seconds of silence) or a frame scan. Hold = fast-forward currently moves a target and applies it on release (no audible fast-forward). |
| UI-8 | **Status-bar icons** for Bluetooth link state and Wi-Fi joined, next to the battery. | spec (common frame) | signal thresholds from #27 | Keep to one glyph each; the detail lives in Settings. |

## UI — blocked on other work

| ID | Item | Source | Needs |
| --- | --- | --- | --- |
| UI-9 | **Library screens**: Artists / Albums / Songs / Genres / Playlists lists, artist → albums → tracks, Right on an artist = All Albums. | spec | [ADR-0017](adr/0017-music-library-index.md) L1–L4 (index build, idle-only, fingerprint) |
| UI-10 | **Podcasts** list and per-episode resume. | spec | library |
| UI-11 | **Album browser (Cover Flow)**: full-screen art carousel with reflection. | spec | library + PSRAM (final board, [ADR-0013](adr/0013-bluetooth-ram-psram.md)); art at more than 64 px |
| UI-12 | **Now Playing badge shows the Bluetooth codec** (SBC / AAC) when the output is BT. | spec | BT stage 2b A2DP ([bluetooth.md](bluetooth.md)) |
| UI-13 | **Settings → Output** (headphone / line / optical / USB) when jack detect cannot decide. | spec | final board jack detect |
| UI-14 | **Click: Piezo** actually clicks (setting exists, shows `Piezo not fitted yet`). | #23 | piezo driver + part on the testbed |
| UI-15 | **Photos** browser. | spec | PSRAM, JPEG decode at screen size |
| UI-16 | **Now Playing: rating / shuffle / repeat page** on a further Select (spec says "not drawn"). | spec | UI-7 (Select now toggles volume / seek; pause is the Play button) |

## Non-UI, tracked elsewhere

- Music library index: [ADR-0017](adr/0017-music-library-index.md), stages L1–L4.
- Bluetooth: stage 2a pairing (branch `feat/bt-pairing`, not yet merged), 2b A2DP, 2c AVRCP — [bluetooth.md](bluetooth.md).
- Bench tests still to run: [bench.md](bench.md).
- Final board hardware: [requirements.md](requirements.md), [design.md](design.md).
