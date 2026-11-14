# neuPod UI/UX review and proposal

Reviewed: 164 simulator screenshots (`320x240/`, `480x360/`, two contact sheets), firmware v0.1.0-dev. Target: LVGL 9, RGB565, partial redraws, Montserrat Medium 14/20/48 (320×240) and 20/28/48 (480×360), click wheel + Select + 4 buttons + Hold.

Mock-ups are in `mockups/`, named `<area>-<screen>-<WxH>.png`, rendered at native resolution for both profiles. They use Inter Medium/SemiBold as a stand-in for Montserrat Medium (same x-height class; expect Montserrat to run ~4% wider — all widths below leave that margin). Fixture names (Aurora Lane, Harbour Lights, Beacon) are kept so the engineer can compare like for like.

---

## 1. Summary — highest-impact changes, ranked

1. **Now Playing hierarchy** — title/artist/album stack, format and output as pills, progress with remaining time, volume as a quiet bottom strip; one layout for wired and Bluetooth (`np-nowplaying`, `np-nowplaying-bt`). Today the title is a file name, "Playing – 1 of 3" competes with the format, and the volume bar is as heavy as the progress bar.
2. **Two-line list rows with secondary text** for Songs and search results (`lib-songs`) — today artist is right-aligned on the same line and collides with long titles (`lib-songs.png`, `text-nowplaying.png`).
3. **Fast-scroll letter overlay + index rail** (`lib-songs-fastscroll`) — the wheel accelerates but gives no position feedback; the overlay already exists in Files (`files-many.png`, the "F" tile) and should be generalised and moved off the content.
4. **Consistent status bar**: left context slot (clock / track position / count / path), centre title, right Bluetooth + battery with a percentage when < 20 % (`home-home`). Today the left slot is always empty and the battery reads "--".
5. **Selection bar as the one accent**: 4 px radius, 3 px inset from the edges, only the focused row coloured; chevrons and secondary text drop to grey — the bar then "moves" between rows with an 80 ms slide instead of repainting two rows.
6. **State feedback without modal screens**: top toast strip for link-lost / can't-play / library busy (`bt-dropped`), bottom HUD strip for volume from any screen (`np-volume-hud`), and busy dots replaced by a determinate progress bar where a count is known (`lib-building`).
7. **Hold / Power feedback** (`hold-locked`, `hold-power-progress`) — a pill at the bottom and a 3 px progress line under the status bar; no full-screen dim.
8. **Text entry on the wheel** (`in-text`) — a horizontal letter ring with the current glyph at 48 px, mode chips (abc / ABC / 123 / #!? / space / ⌫) on Left/Right, field at the top. Today letters are evenly spaced small glyphs and there is no visible mode switch (`in-text.png`, `wifi-kb.png`).
9. **Dark theme via tokens** (`home-home-dark`, `np-nowplaying-dark`) — one token table, `Auto` switches to dark when the backlight dims; the product board's LCD benefits less than an OLED would, but night use and battery on a future panel both improve.
10. **Missing screens**: Cover-Flow-style album browser without 3D (`lib-coverflow`), Now Playing page 2 (`np-page2`), Playlists (`lib-playlists`), Licences (`settings-licences`), real empty states (`lib-empty`).

---

## 2. Design system

### 2.1 Colour tokens (RGB565-safe: R and B multiples of 8, G multiples of 4)

| Token | Light | Dark | Use |
| --- | --- | --- | --- |
| `bg` | `#F8F8F8` | `#101010` | screen background |
| `surface` | `#FFFFFF` | `#181818` | cards, input fields, right pane |
| `bar` | `#E8E8E8` | `#202020` | status bar fill |
| `line` | `#E0E0E0` | `#303030` | hairlines, progress track, scrollbar track |
| `ink` | `#181818` | `#F0F0F0` | primary text, battery fill |
| `ink2` | `#606060` | `#A0A0A0` | secondary text, chevrons, volume fill |
| `ink3` | `#A0A0A0` | `#686868` | tertiary text, disabled rows, hints |
| `accent` | `#1858C8` | `#2868D8` | selection bar, progress fill, focused value |
| `on_accent` | `#FFFFFF` | `#FFFFFF` | text on the selection bar |
| `accent_soft` | `#D8E4F8` | `#203050` | pills, info toast |
| `amber` | `#D08018` | `#E89828` | seek mode, warnings |
| `amber_soft` | `#F8E8C8` | `#483818` | warning toast background |
| `green` | `#209858` | `#30B870` | charging, streaming OK, signal full |
| `red` | `#D03028` | `#E84840` | battery < 15 %, errors |

Contrast: `ink2` on `surface` 5.9:1 light / 7.1:1 dark; `ink3` on `surface` 2.6:1 — use `ink3` only for text ≥ 14 px that is not the sole carrier of meaning (hints, disabled rows). `on_accent` on `accent` 7.3:1.

Theme switch: a `theme_t` struct of these 14 colours; every style reads from it; `lv_obj_report_style_change()` after swap. `Auto` = light while the backlight is at full, dark once it dims (and remembers the user's last manual choice).

### 2.2 Type scale (existing fonts only)

| Role | 320×240 | 480×360 | Weight | Where |
| --- | --- | --- | --- | --- |
| Title | 20 | 28 | Medium | Now Playing title, album header, empty-state heading |
| Body | 14 | 20 | Medium | list rows, status-bar title, settings values |
| Small | 12 (new) | 16 (new) | Medium | secondary list line, times, pills, hints, status-bar left slot |
| Big | 48 | 48 | Medium | seek time, letter overlay, clock, text-entry current glyph |

The only addition is **Small** (12 / 16 px). It is the single biggest readability gain in the proposal (secondary lines, times). Cost: one Montserrat Medium size per profile, ASCII + Latin-1 + punctuation ≈ 9–12 KB each. If flash is tight, ship 12 px only and let 480×360 use 14 px Body for secondary text with `ink2` (works, looser).

Truncation: always `LV_LABEL_LONG_DOT` with a single ellipsis; never clip glyphs (`text-nowplaying.png` currently clips "Have You Fed t…" at the title size but wraps nothing). Title in Now Playing may wrap to two lines on 480×360 only.

### 2.3 Layout metrics

| Metric | 320×240 | 480×360 |
| --- | --- | --- |
| Status bar height | 24 | 36 |
| Single-line row | 30 | 45 |
| Two-line row | 40 | 60 |
| Horizontal padding | 10 | 15 |
| Selection bar inset / radius | 3 / 4 | 4 / 6 |
| Split-pane divider (Home, Settings) | 56 % / 44 % | same |
| Art: Now Playing | 112 | 168 |
| Art: Home panel | panel width − 24 | same |
| Art: album header, NP page 2 | 64 | 96 |
| Art: Cover Flow centre / neighbours / outer | 96 / 64 / 48 | 144 / 96 / 72 |
| Progress bar height (normal / seek) | 4 / 6 | 6 / 9 |
| Toast strip height | 22 | 33 |
| HUD strip height | 26 | 39 |
| Pill height | 18 | 27 |
| Scrollbar width / margin | 2 / 1 | 3 / 2 |

Visible rows: 320×240 gives 7 single-line rows or 5 two-line rows; 480×360 gives the same counts (everything scales ×1.5). Keep row counts identical across profiles so wheel acceleration feels the same.

### 2.4 Album-art sizes to decode

Decode once per album to a **160×160 RGB565 thumbnail** (51 KB) on the PSRAM board and keep an LRU of ~24 (1.2 MB). Draw it scaled down for every slot above; nothing is scaled up, which is what makes today's 64 px → 2.6× art look soft (`art-cover.png`). On the Pico 2 W target keep 96×96 (18 KB) and an LRU of 6; Now Playing then shows art at 1:1 on 320×240 and 1.17× on 480×360 — acceptable.

### 2.5 Icon set

Use **Lucide** (ISC licence, https://lucide.dev) rendered to 1-bit-alpha bitmaps at 16 and 24 px (320) / 24 and 36 px (480). Lucide is stroke-based, consistent at small sizes, and its licence permits embedding without attribution in the UI (keep the licence text in the Licences screen). Required glyphs, 14 in total:

`chevron-right`, `play`, `pause`, `skip-back`, `skip-forward`, `shuffle`, `repeat`, `repeat-1`, `volume-2`, `bluetooth`, `wifi`, `lock`, `folder`, `file`. Battery, signal bars and the three busy dots are drawn primitives (rectangles), not glyphs.

Drop FontAwesome where these cover it; keep it only for glyphs you already ship and still need.

### 2.6 Motion (all small-area, no full-screen redraw)

| Motion | Duration | Easing | What moves |
| --- | --- | --- | --- |
| Selection bar to adjacent row | 80 ms | ease-out | only the bar (`lv_anim` on y); text repaints two rows |
| Selection bar jump > 3 rows / fast spin | 0 ms | — | no anim; letter overlay shows instead |
| Push screen (deeper) | 120 ms | ease-out-cubic | list area slides in 24 px from the right, fades from 0; status bar title swaps instantly |
| Pop screen (Menu) | 100 ms | ease-out-cubic | list slides 24 px to the right and fades out |
| Toast in / out | 150 / 200 ms | ease-out / ease-in | one-row (30 / 45 px) strip slides down from under the status bar |
| Volume HUD | 120 ms in, hold 1200 ms, 200 ms out | ease-out | 26 px bottom strip; bar fill animates 60 ms |
| Seek mode enter / leave | 0 ms | — | colour swap only; haptic tick |
| Progress bar | 1 Hz step | — | invalidate only the bar rectangle |
| Letter overlay | appears instantly, fades 250 ms after the wheel stops | ease-in | 44 px tile + rail |
| Cover Flow step | 90 ms | ease-out | five tiles re-blit at new x/size; no rotation |
| Power-hold progress | linear over 2 s | — | 3 px line under the status bar grows left to right |

Cap LVGL at 30 fps on the MCU (`LV_DEF_REFR_PERIOD 33`); 60 fps on the Y2 if the kernel gives a vsync'd fb.

---

## 3. Per-screen review and proposal

Button conventions used throughout: **Wheel** scroll/adjust · **Select** open/choose · **Menu** back · **Play/Pause** global · **◁ ▷** prev/next (hold = seek) · **Power** hold 2 s lock, 10 s off.

### 3.1 Home

**Current** (`home.png`, `home-clock.png`, `art-home.png`): split list + right panel; the panel shows a generic disc glyph, "Nothing playing", "--:--" and a debug line "image 2.0 MB". When playing it shows art, file name and "⏸ Paused" but no progress. Status bar left slot is empty; battery reads "--".

**Proposed** (`home-home`, `home-idle`, `home-home-dark`):

- Left 56 %: the seven items, chevrons on items that open a list, none on "Shuffle Songs". "Now Playing" greys out when nothing is queued instead of opening an empty screen.
- Right panel, playing: art at panel width, title (Body, semibold) and artist (Small) centred, a 3 px progress bar, elapsed/remaining in Small. Paused shows the pause glyph in front of the elapsed time.
- Right panel, idle: 48 px clock, date, one Small line "1,204 songs · 72 %". Remove "image 2.0 MB" from release builds (keep it behind a debug flag).
- Status bar: clock in the left slot on Home only.

**States:** idle / playing / paused / library-not-built (panel shows "Update Library" prompt, Select on Music still works).
**Transitions:** push on Select; the panel does not animate.
**Wheel:** scroll items; Select opens; Play/Pause toggles playback without leaving Home.

### 3.2 Now Playing

**Current** (`np-bt-badge.png`, `seek-scrub.png`, `np-bt-volume.png`, `art-cover.png`, `text-nowplaying.png`): file name as title; format badge in blue sits where the artist should; "Playing – 1 of 3" is body-weight; the volume slider is as prominent as progress; seek mode recolours progress amber and adds a third "Seek 0:08" line with yet another bar; on BT the label changes to "Headset 47 %". Long titles clip (`text-nowplaying.png`).

**Proposed** (`np-nowplaying`, `np-nowplaying-bt`, `np-seek`, `np-page2`, `np-volume-hud`):

- Status bar: left slot "3 of 12", centre "Now Playing", Bluetooth glyph in blue when streaming.
- Art 112 / 168 px at the left; to its right the stack Title (Title size) → Artist (Body, `ink2`) → Album · Year (Small, `ink3`). Fall back to the file name only when there is no title tag, and then show the folder name as the "artist".
- Pill row under the stack: format pill (`accent_soft`, e.g. "FLAC 16/44.1" or "AAC 44.1" when the BT codec is what's leaving the box) and output pill (outline: "Wired" or the headset's name). The output pill is where "Headset" belonged.
- Progress: 4 px, elapsed left, remaining right, a quiet `ink3` "repeat 1 / shuffle" word in the centre only when either is on.
- Volume: bottom strip, label "Volume −18 dB" or "Headset 62 %" in Small, bar in `ink2`, 45 % of the width. On a volume change from any other screen the same strip appears as a HUD for 1.2 s (`np-volume-hud`).
- Seek mode: the pill row is replaced by the seek time in Big amber; the progress bar grows to 6 px with a knob; hint line "Scroll: seek · Select: go · Menu: cancel". Leaving seek restores the pills. No third bar.
- Page 2 (Select on Now Playing when not seeking): small art + title, star rating under it (wheel adjusts when focused), then Shuffle / Repeat / Gapless / Add to playlist / Go to album / Go to artist.

**States:** playing / paused (pause glyph before elapsed time, art unchanged) / seeking / BT link lost (toast, progress bar goes `ink3`, "Paused" under the title — see `bt-dropped`) / no art (placeholder tile with a ring, not a music-note glyph) / unsupported codec (toast "Can't play: AAC — skipped", auto-skip after 1.5 s).
**Transitions:** push from a list = 120 ms slide; Menu returns to the list that queued the track.
**Wheel:** volume; Select → page 2; hold Select → seek mode (wheel seeks, Select commits, Menu cancels); ◁ ▷ tracks, hold to scan at 4× with the seek bar shown.

### 3.3 Library (Artists, Albums, Songs, Genres, Compilations, Playlists, Album, Update Library)

**Current** (`lib-songs.png`, `lib-album.png`, `lib-album-greyed.png`, `lib-artists.png`, `lib-building.png`, `lib-needs-writable.png`, `lib-cant-play.png`, `lib-update-done.png`, `music-empty.png`): single-line rows with artist right-aligned (collides with long titles); album screen is a bare track list with no header; greyed unplayable rows give no reason until you press Select, then a yellow bar appears bottom-left; busy and error states are rendered as fake list rows ("Building library…" with dots as the first row, "Select to cancel" as the third); "No artists" is a selected blue row.

**Proposed:**

- **Songs / search results** (`lib-songs`): two-line rows, title in Body and "Artist · Album" in Small `ink2`; 2 px scrollbar at the right edge; status-bar left slot shows the count.
- **Fast scroll** (`lib-songs-fastscroll`): when wheel velocity passes the acceleration threshold, show a 44 px dark tile with the first letter of the focused row at the top-right and a 26-letter rail along the right edge with the current letter in `ink` at Small size, the rest at 8 px `ink3` (every second letter on 320×240). The tile never covers the selection bar. Fade out 250 ms after the wheel stops. Sort keys ignore "The ", "A " and diacritics so the rail is useful.
- **Album** (`lib-album`): header with 64 px art, album title (Title), "Artist · Year" (Body `ink2`), "3 songs · 9:41 · FLAC" (Small `ink3`); hairline; tracks as "n   Title" with duration right. Unplayable tracks greyed with the codec in brackets ("Carrier Wave (AAC)") so the reason is visible before pressing.
- **Artists / Albums / Genres / Compilations**: single-line rows with chevrons; show counts on the right ("4 albums") only on 480×360 where there is room.
- **Albums as Cover Flow** (`lib-coverflow`): optional view toggled with ▷ on the Albums list. Five flat tiles: centre 96 px, neighbours 64 px, outer 48 px, no rotation or reflection; title and "Artist · Year · n songs" under the centre; status-bar left slot "7 of 31". Wheel moves one album per step; Play plays the album; Select opens it.
- **Playlists** (`lib-playlists`): smart lists first (On-The-Go, Recently added, Most played, Unplayed) then .m3u files from the card, counts on the right.
- **Update Library / busy** (`lib-building`): centred heading, determinate bar (songs found / total files from a first directory count), current folder in Small, hints "Menu: run in background · Select: cancel". While running in the background the status bar shows three busy dots in the left slot of every screen.
- **Empty and error states** (`lib-empty`): heading in Title, one or two Small lines that say what to do, a single accent button. "Library needs a writable card" becomes a toast on the library root plus this empty state, never a selected list row.
- **Can't play**: toast at the top in `amber_soft` with the codec and "skipped"; the track is skipped automatically after 1.5 s in a queue, or the row stays focused in a list.

**Transitions:** push/pop slides; Cover Flow step 90 ms.
**Wheel:** as lists; in Cover Flow one album per detent with acceleration to 3 per detent.

### 3.4 Bluetooth

**Current** (`bt-found.png`, `bt-pairing.png`, `bt-dropped.png`, `bt-forget-asks.png`, `settings-bt-streaming.png`, `bt-key-next.png`): the settings page mixes status rows (Status, Address) with actions (Scan, Connect, Forget) in one list; the scan list shows a "Cancel pairing" row at the top and three-colour signal bars; "Headset disconnected" is a yellow band with "Reconnecting…" dots inline; "Forget" turns into "Select again" as a confirmation, which is easy to trigger by accident; the right pane in Settings dumps "SBC bitpool 53 · 328 kbit/s · 0 underruns" (fine for a dev build, not for users).

**Proposed** (`bt-devices`, `bt-dropped`):

- One **Devices** screen: known devices first with their state on the right ("Connected" in `on_accent` on the selection bar, "Saved" in `ink2`), then found devices with signal bars (three bars, monochrome `ink`/`line`, no traffic-light colours), phones greyed and labelled "(phone)" because they cannot be sinks, then "Scan again". Scanning progress as four dots at the bottom in `ink3`.
- Select on a saved device: Connect / Forget / Rename in a small list pushed from the right; Forget asks "Forget AirPods Pro?" with Select = yes, Menu = no — never a self-mutating row.
- Pairing: the row's right text becomes "Pairing…" with three dots; Menu cancels (no "Cancel pairing" row).
- Link lost: `bt-dropped` — toast "AirPods Pro disconnected · reconnecting…", playback paused, progress bar goes `ink3`, status-bar glyph grey. On success the toast becomes "AirPods Pro connected" in `accent_soft` for 1.2 s; on give-up, "Couldn't reconnect — playing on wired" or "— paused" if nothing is plugged in.
- Codec detail stays in About › Diagnostics, not in the Settings right pane.
- Status, Address and Audio output move to a short **Bluetooth** settings page: Bluetooth On/Off, Audio output Auto/Wired/Bluetooth, Devices ›, Absolute volume On/Off.

**States:** off / idle / scanning / pairing / connected / streaming / reconnecting / failed.
**Wheel:** list; Select on a device = connect (found) or options (saved).

### 3.5 Settings

**Current** (`settings.png`, `settings-bt.png`, `haptics`, `display`): two-pane with values on the left rows and a centred prose summary on the right ("Resume playing / At power-up / On / Plays on power-up if it was playing") that reads as a paragraph; About shows "6 of 8 checks ok" and "nogit".

**Proposed** (`settings-display`):

- Left list: Display, Click, Haptics, Gapless, Shuffle, Repeat, Resume, Bluetooth, About — values on the row where the value is a word, chevrons where it opens a page. Rows that toggle (Gapless, Resume) toggle on Select without leaving the list; a 60 ms value flip, haptic tick.
- Right pane: a key/value summary left-aligned (Small), heading in Body semibold, one line of help in `ink3`. No centred prose.
- Display page: Theme Light/Dark/Auto, Brightness, Dim after, Off after.
- About: version, build, storage used/free, battery health, then Diagnostics › (checks, codec detail, underruns) and Licences › (`settings-licences`).

### 3.6 Files (Extras › Files)

**Current** (`files.png`, `files-many.png`, `files-many-end.png`, `text-folder.png`): fine functionally; the "… more (first 256 shown)" row is a blue selected row; the "F" letter tile sits over the list centre.

**Proposed** (`files-browser`): path in the status-bar left slot, folder/file glyphs from the icon set, size right-aligned in `ink2`, scrollbar. Folders with more than 256 entries show "256+" on the folder row and page in the list with "Load more" as the last row (not selected by default). Letter overlay as in the library (top-right tile).

### 3.7 Input widgets (text, password, number, slider, choice, date/time, search)

**Current** (`in-text.png`, `wifi-kb.png`, `in-slider.png`, `in-choice.png`, `in-datetime.png`): letters in an even row of five with the current one blue; hints as a sentence; no visible way to switch case/digits/symbols; slider is a bare bar; date/time fields underline the active field.

**Proposed** (`in-text`): field at the top in a `surface` box with a caret; a horizontal ring of nine glyphs with the current one in Big `accent` and widths proportional to glyph size; mode chips below (abc / ABC / 123 / #!? / space / ⌫) switched with ◁ ▷; hint line at the bottom. Password shows the typed glyph for 800 ms then a dot. Number/slider: value in Big centred, bar below, min/max in Small at the ends. Choice: plain list. Date/time: keep the field underline but make the active field `accent` and add the field name under it in Small.

**Wheel:** letters (acceleration jumps 3); Select types; Menu deletes (hold = clear); Play = done; ◁ ▷ = mode (text) or field (date/time).

### 3.8 Hold, lock and power

**Current:** nothing visible in the tours beyond the brief's description.

**Proposed** (`hold-locked`, `hold-power-progress`): while locked, any key press wakes the screen for 3 s and shows a dark pill at the bottom "Locked · hold Power 2 s to unlock"; the content behind is unchanged (no dim, so no full-screen blend). While Power is held, a 3 px `accent` line grows under the status bar over 2 s with a pill "Keep holding to lock · 10 s = off"; past 2 s the pill reads "Locked", the line resets and refills to 10 s in `red` with "Release to cancel · shutting down in 6"; haptic tick at 2 s and each second after 7 s.

On the product board the physical Hold switch drives the same pill without the progress line.

### 3.9 Splash and About

**Current** (`splash.png`): wordmark, a thin progress line, a row of eight boxed status glyphs with red crosses, a red "[1/2] battery: gauge: no response" line and the version.

**Proposed:** keep the wordmark and the progress line; move the self-test row to a debug splash (shown only when a check fails or when Menu is held at boot) and show it as a short list ("Battery gauge — no response") rather than crossed-out icons. Release splash: wordmark, progress line, version in `ink3`, 600 ms minimum.

> **Owner amendment, 2026-11-13:** after testing on the Y2 the owner asked for the icon row back ("the device check screen has lost the icons and replaced with text - I want the icons back, that loading screen was good"). The release splash is now wordmark, progress line, **icon row** (Lucide icons, pulse / solid / red cross), version; the text list stays on the debug splash only.

### 3.10 Status bar (global)

Left slot by screen: Home = clock · Now Playing = "n of m" · lists = count · Files = path · Album = album/artist name · Cover Flow = "n of m". Centre: screen title. Right: Bluetooth glyph (`ink3` saved, `ink` connected, `accent` streaming), battery outline with fill (`green` while charging, `red` below 15 %) and the percentage as text only below 20 % or while charging. Three busy dots replace the left slot when the library is building in the background.

---

## 4. Mock-ups

All in `mockups/`, light theme unless named `-dark`, both `320x240` and `480x360`:

| File | Section |
| --- | --- |
| `home-home`, `home-idle`, `home-home-dark` | 3.1 |
| `np-nowplaying`, `np-nowplaying-bt`, `np-nowplaying-dark`, `np-seek`, `np-page2`, `np-volume-hud` | 3.2 |
| `lib-songs`, `lib-songs-fastscroll`, `lib-album`, `lib-coverflow`, `lib-playlists`, `lib-building`, `lib-empty` | 3.3 |
| `bt-devices`, `bt-dropped` | 3.4 |
| `settings-display`, `settings-licences` | 3.5 |
| `files-browser` | 3.6 |
| `in-text` | 3.7 |
| `hold-locked`, `hold-power-progress` | 3.8 |

`render.py` regenerates them; every length comes from the metric table in §2.3, so it doubles as a layout reference.

---

## 5. Implementation notes (cost and cheaper alternatives)

| Item | Cost | Cheaper alternative |
| --- | --- | --- |
| New Small font size (12 / 16 px) | ~10 KB flash per profile | Ship 12 px only; 480×360 uses 14 px Body in `ink2` for secondary text |
| Lucide icons, 14 glyphs × 2 sizes × 2 profiles, 1-bit alpha | ~6 KB | Render at 16 px only and let LVGL scale ×1.5 on 480×360 (slightly soft) |
| 160×160 art thumbnails, LRU 24 | 1.2 MB PSRAM | 96×96, LRU 6 (18 KB each) on the Pico 2 W; Now Playing at 1:1 |
| Selection-bar slide (80 ms) | invalidates two rows per frame | Disable below 30 fps; the bar simply jumps |
| Push/pop slide + fade | list area redrawn ~4 frames | Slide only (no alpha); or title-swap only on the Pico 2 W |
| Toast / HUD strips | one 22–26 px strip | none needed — already minimal |
| Letter overlay + rail | 44 px tile + 26 small labels, drawn ~once per 100 ms | Rail every second letter on 320×240 (as mocked) |
| Cover Flow, five flat tiles | five `lv_image` re-blits per step; no rotation | Three tiles on the Pico 2 W |
| Dark theme | zero runtime cost; one token struct | — |
| Power-hold progress line | 3 px × width invalidate at 10 Hz | — |
| Determinate library progress | needs a first pass to count files (fast: directory entries only) | Indeterminate bar that sweeps, still better than row-dots |

Avoid: full-screen dim behind the lock pill (alpha blend of the whole frame), blur, gradients on backgrounds, animating the progress bar more often than 1 Hz, and per-frame redraw of the right pane on Home.

---

## 6. Backlog

| ID | Item | Area | Effort | Impact | Depends on |
| --- | --- | --- | --- | --- | --- |
| DS-1 | Colour token table + light/dark structs, `Auto` on dim | System | S | 3 | — |
| DS-2 | Add Small font size (12/16) to both profiles | System | S | 3 | — |
| DS-3 | Status bar: left slot, battery % rules, BT glyph states | System | S | 3 | DS-1 |
| DS-4 | Selection bar restyle (inset, radius, 80 ms slide) and grey chevrons | System | S | 2 | DS-1 |
| DS-5 | Toast strip component (top) and HUD strip (bottom) | System | M | 3 | DS-1, DS-2 |
| DS-6 | Lucide icon set, replace FontAwesome where covered | System | S | 2 | — |
| DS-7 | Push/pop slide transition, 30 fps cap | System | M | 2 | DS-4 |
| NP-1 | Now Playing layout: tag stack, pills, progress, volume strip | Now Playing | M | 3 | DS-2, DS-3 |
| NP-2 | Seek mode: Big amber time, knob, hints, no third bar | Now Playing | S | 2 | NP-1 |
| NP-3 | Now Playing page 2 (rating, shuffle, repeat, go-to) | Now Playing | M | 2 | NP-1 |
| NP-4 | Volume HUD from any screen | Now Playing | S | 2 | DS-5 |
| NP-5 | Art pipeline: 160² thumbnails + LRU (96² on Pico 2 W) | Now Playing | M | 2 | — |
| LB-1 | Two-line song rows (title / artist · album) | Library | S | 3 | DS-2 |
| LB-2 | Fast-scroll letter overlay + index rail, sort keys ignoring articles/diacritics | Library | M | 3 | DS-2 |
| LB-3 | Album header (art, title, artist · year, totals) | Library | S | 2 | NP-5 |
| LB-4 | Empty / error / busy states as full-pane layouts, determinate progress | Library | M | 3 | DS-5 |
| LB-5 | Unplayable rows show codec; auto-skip with toast | Library | S | 2 | DS-5 |
| LB-6 | Cover Flow (flat five-tile) view on Albums | Library | M | 2 | NP-5 |
| LB-7 | Playlists screen (smart lists + .m3u) | Library | L | 2 | — |
| BT-1 | Devices screen (known / found / scan), signal bars monochrome | Bluetooth | M | 3 | DS-6 |
| BT-2 | Forget / pairing / reconnect flows with toasts; no self-mutating rows | Bluetooth | M | 3 | DS-5, BT-1 |
| BT-3 | Bluetooth settings page; codec detail moves to Diagnostics | Bluetooth | S | 1 | BT-1 |
| ST-1 | Settings right pane as key/value summary; toggles in place | Settings | S | 2 | DS-2 |
| ST-2 | Display page (theme, brightness, timeouts) | Settings | S | 2 | DS-1 |
| ST-3 | About › Diagnostics, Licences screen | Settings | S | 1 | — |
| IN-1 | Text entry: letter ring, mode chips, field box | Input | M | 2 | DS-2 |
| IN-2 | Number/slider/date-time polish | Input | S | 1 | IN-1 |
| HL-1 | Lock pill and Power-hold progress line | Hold/Power | S | 2 | DS-5 |
| FL-1 | Files: path in status bar, icons, paged "Load more" | Files | S | 1 | DS-3, DS-6 |
| SP-1 | Release splash; self-test to debug splash | Splash | S | 1 | — |

Suggested order: DS-1 → DS-2 → DS-3 → DS-4 → DS-5 (foundation, about a week), then NP-1 and LB-1/LB-2 (the two things users see most), then BT-1/BT-2, then the rest by impact.
