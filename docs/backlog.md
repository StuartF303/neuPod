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
| UI-22 | **The queue line shows library track names**: Now Playing's `n of N` line (and the queue it describes) names the track from the library's tags, not the file name, when the queue came from a library list. | req 12 Nov 2026 | — | The library reader has the title; the range queue only knows the song index today. |
| UI-21 | **Resume restores the album or shuffle queue**, not just the one track: the resume record keeps the track and its place; it should also bring back the queue it was chosen from (album, artist, genre, all songs, a folder; shuffle with its key) so Next / Previous work after a restart. | req 12 Nov 2026 | — | [ADR-0017](adr/0017-music-library-index.md) range queues are stateless, so the record needs the range and the shuffle key, not a list. |
| UI-20 | **Genre → Albums**: from a genre, a list of albums (as Artists has), not only All Songs and artists. | req 12 Nov 2026 | — | Needs a per-genre album grouping in the index (a table like `by_genre.u32`, ADR-0017 L2). |
| UI-19 | **Playlists**: the Music > Playlists list (M3U / M3U8 on the card, in Files order), a playlist as a queue. | spec | — | Was UI-9's last piece. Parse on the storage task, entries resolved through the library or by path. |
| UI-4 | **Fast-scroll letter overlay** on long lists (first letter of the row under the bar while scrolling fast). | spec (Library) | — | Scroll bar exists (`ui_list.c update_scrollbar`). Only useful once lists are long: Files on a big folder today, library later. |
| UI-8 | **Wi-Fi status-bar icon**: today one Wi-Fi glyph coloured by the signal class of the joined network (hidden when the station is off); remainder: draw the signal as bars in the status bar like the lists do, and a distinct "joining" look. | spec (common frame) | — | The Bluetooth glyph (remembered / connected / streaming) is done. Keep to one glyph; the detail lives in Settings. |
| UI-7 | **Seek remainder**: FLAC without a SEEKTABLE (needs a decode-from-start fallback, seconds of silence, or a frame scan) and AAC (not playable yet); **audible fast-forward** while Right / Left is held (today the hold moves a target and applies it on release); a Fraunhofer VBRI byte table is read as if its first stretch includes the VBRI frame (not checked against a real file). | spec (Now playing) | — | MP3 and ALAC seek are built (UI-7, 12 Nov 2026); see [Audio](audio.md#gapless-playback-and-the-play-queue). |

| TS-1 | **Library tours are clock-driven** (key script timed in wall-clock ms), so they flake on a loaded CI runner (failed at 60 s and 80 s run limits; now 140 s). Make the tour scripts wait for events (log line / screen state) instead of fixed times. | CI 2026-11-12 | — | |
## UI — checks and loose ends from the Hold / screen-off work (12 Nov 2026)

| ID | Item | Source | Needs |
| --- | --- | --- | --- |
| UI-18 | **Hold on the device**: run the Y2 steps in [bench.md](bench.md#hold-y2) (ring, lock, swallowed keys, countdown, power-off with exit 77, screen off and wake, haptic click, headset keys while locked); **assign the final board's Hold-switch GPIO** (`hal_input_hold_switch()` is `TODO(hw)` on the RP2350 HAL); decide whether a button held down when the screen goes off should be released (a fast-forward in progress ends by its own 30 s timer today). | req 12 Nov 2026 | Y2 on the bench; iPod 6G donor board |
| UI-6 | **Rapid presses (bench check)**: the queue is 64 deep, the UI wakes on input and drops are counted (`input: WARNING n events dropped`); send 20 UART keys in one burst on the Pico rig and confirm none are lost. | bench 2026-11-03 | wheel / UART on the bench |
| UI-23 | **Licences screen, remainder**: the on-device list shows a licence name and a short notice; the full texts (SIL OFL, CC BY 4.0, Apache-2.0, ChaN's licence) are in the source tree only; BTstack's commercial terms (a final board that is not a Raspberry Pi product, and the Y2) are an open legal question ([LICENSE.md](https://github.com/StuartF303/neuPod/blob/main/LICENSE.md)). | req 12 Nov 2026 | decision on shipping full texts; BlueKitchen answer |

## UI — blocked on other work

| ID | Item | Source | Needs |
| --- | --- | --- | --- |
| UI-10 | **Podcasts** list and per-episode resume. | spec | library |
| UI-11 | **Album browser (Cover Flow)**: full-screen art carousel with reflection. | spec | RP2350B + PSRAM board ([ADR-0001 amendment](adr/0001-rp2350-phase1-platform.md#amendment-12-november-2026-the-pico-2-w-is-frozen-the-product-target-is-the-rp2350b-with-psram), [ADR-0013](adr/0013-bluetooth-ram-psram.md)); art at more than 64 px |
| UI-13 | **Settings → Output** (headphone / line / optical / USB) when jack detect cannot decide. | spec | final board jack detect |
| UI-14 | **Click: Piezo** actually clicks (setting exists, shows `Piezo not fitted yet`). | #23 | piezo driver + part on the testbed |
| UI-15 | **Photos** browser. | spec | RP2350B + PSRAM, JPEG decode at screen size |
| UI-16 | **Now Playing: rating / shuffle / repeat page** on a further Select (spec says "not drawn"). | spec | (Select now toggles volume / seek; pause is the Play button) |

## Non-UI, tracked elsewhere

- Music library index: [ADR-0017](adr/0017-music-library-index.md); the lists are built (L2), the gaps are UI-19 to UI-22 above.
- Bluetooth: stages 2a to 2c and the reconnect policy are built, what is left is in [bluetooth.md](bluetooth.md).
- Hold, the Power key and the screen-off stage: [ADR-0020](adr/0020-hold.md).
- Bench tests still to run: [bench.md](bench.md).
- Final board hardware: [requirements.md](requirements.md), [design.md](design.md).
