# UI mock-ups

Text, number, level, choice and date entry with the click wheel is one library: [Wheel input widgets](ui-input.md). Wi-Fi screens: [Wi-Fi](wifi.md).

Design review of 12 November 2026 (colour tokens, type scale, new screens, owner decisions): [design review](design/2026-11-12-ui-review/README.md); its backlog is in [Backlog](backlog.md#design-review-2026-11-12).

Target: the Phase 1 panel, **320×240 landscape**. Each screen is an original drawing at the
panel's native size (SVG, `viewBox="0 0 320 240"`); the look follows the classic click-wheel
player layout — split menu with artwork, blue selection bar — lightly modernised. These are
design intent, not screenshots of any existing firmware.

Click-wheel vocabulary used below: **Menu** (top button, goes back), **Select** (centre),
**Play/Pause** (bottom), **Left** / **Right** (previous / next), **scroll** (finger on the ring).

## Main menu

![Main menu](assets/ui/main-menu.svg)

- Scroll moves the selection bar through Music, Podcasts, Photos, Extras, Settings, Shuffle
  Songs, Now Playing. Scroll acceleration follows the Rockbox constants (4 counts per step,
  velocity-based acceleration).
- **Select** opens the highlighted item. **Menu** does nothing at the top level.
- **Play/Pause** toggles playback without leaving the menu; the right pane shows the current
  track's artwork and title, or a clock when nothing is playing.
- **Left** / **Right** skip tracks in the background.

## Library — artists

![Artists list](assets/ui/library-artists.svg)

- Full-width list; scroll bar on the right shows position in long lists. Fast scrolling shows a
  letter overlay so a 100k-track library is navigable.
- **Select** descends (artist → albums → tracks); **Menu** ascends one level.
- **Right** on an artist jumps to All Albums for that artist; **Left** returns to the previous
  level without animation. (Design intent: Right / Left skip tracks in the built lists, as everywhere else.)
- The same template serves Albums, Songs, Genres, Composers, Playlists and Podcasts. Built: see [Music lists](#music-lists).

## Music lists

The Music screens read the library index on the card ([ADR-0017](adr/0017-music-library-index.md), [how it is stored and built](storage.md#music-library-index)). Every list is the
same screen: a full-width list with the usual scroll acceleration; the first line of a row is the name, the right-hand text is the second fact (an album's artist, a song's
artist, the length of a track in an album, the number of songs in an artist's album); a chevron marks a row that opens another list.

| List | Rows | Select |
| --- | --- | --- |
| Artists | album artists, collated (a leading "The " ignored, accents folded, symbols, then digits, then letters) | the artist: **All Songs**, then its albums |
| Albums | every album by name, the album artist on the right | the album's songs in track order (disc, track, title) |
| Songs | every song in title order | plays it |
| Genres | genre names | the genre: **All Songs**, then its artists |
| Compilations | albums flagged as compilations (or by "Various Artists") | the album's songs |
| artist / genre | **All Songs** first | **All Songs** lists every song of the artist (album by album) or of the genre (album order) |

**Select on a song plays it and queues the whole list it was chosen from**: an album in track order, an artist's songs, a genre, or all songs, starting at that song, with the earlier
songs kept for Previous. Gapless playback, repeat, shuffle (Settings) and resume work as for a folder; Now Playing shows `n of N`. A file the player cannot play (AAC,
more than two channels, damaged) is listed greyed; Select shows why (`Can't play: unsupported codec (AAC)`) and auto-advance skips it. **Home > Shuffle Songs** plays the whole library in a
random order (every song once; the Shuffle setting is not changed). Playlists still say `None yet`.

**Two-line rows and the album header (LB-1, LB-3).** Lists of songs (Songs, an artist's All Songs, a genre's All Songs) show **two lines per row**, 40 / 60 px, five rows: the title in Body and `Artist . Album` in Small ink2 below (the artist no longer sits at the right, where it collided with long titles); the focused row's two lines are tickers. The status bar shows the count with a
thousands comma (`1,204`). An **album** opens with a header (art 64 / 96 px, the album in the large font, `Artist . 2019`, `3 songs . 9:41 . FLAC`, a hairline) over the track rows `3   Title` with the length at the right; a song the player cannot play is greyed with its codec in brackets (`Carrier Wave (AAC)`), so the reason is visible before pressing. The status bar's left slot holds the album's name.
The header's facts come with the album's info request (`lvw_info_t`: artist, first year, summed length, common codec; read from the first 100 songs); its art is decoded by the art service (see Now Playing art).

**Fast scroll (LB-2).** While the wheel spins a long list past the acceleration threshold, a dark **tile** with the letter of the focused row appears at the top right and an **index rail** runs down the right edge (A to Z, every second letter in ink3, the current letter in ink; the letters next to it make room). It stays while the wheel moves and fades out in 150 ms once the wheel has rested for 250 ms. It never covers the selection bar's row text beyond the right 14 px of the first lines. The letter is the first letter of the row's **collation key** (`lib_collate_key()`: a leading "The " is ignored, accents are folded, a number or symbol is `#`), so the rail agrees with the sort. The sort ignores "The " and accents already; "A " / "An " are not ignored yet (see Backlog). The same component (`ui_rail.c`, `ui_letter_overlay_*`) is used by Files, which sorts by plain name.

A long list never holds more than a few screens of rows in memory: the storage task fills a window of 16 rows around the selection, and a row that has not arrived yet shows `...` for the
moment it takes. Both resolution profiles use the same screens (the 480x360 layout audit runs on every snapshot of the `library_tour`).

**No index yet.** The first Artists / Albums / Songs / Genres / Compilations / Shuffle Songs opens the card's `/Music` and builds the index (a one-off; **Music > Update Library**
rebuilds it later, for example after adding albums). Progress is a screen of three lines: `Building library...` with busy dots, `n songs found` and `Select to cancel` (Cancel or Menu
leaves the old library untouched). It runs only with nothing playing: while music plays it says `Stop playback to update the library`. Other messages: `Library needs a writable
card` (the card is write-protected, or the card refused the write), `Card is full`, `Card read error`, `No card`; each offers `Select to try again`. **Update Library** ends with
`Library updated` and the counts.

## Now playing

![Now playing](assets/ui/now-playing.svg)

Built in phase 2 of the [design review](design/2026-11-12-ui-review/README.md) (NP-1 to NP-5; mock-ups `np-*` there). Page 1:

- Art (112 / 168 px) at the left; to its right **Title** (large), **Artist** (Body, ink2) and **Album . Year** (Small, ink3). A file with no title tag shows its file name as the title and its folder as the artist. Title, artist and album are
  tickers: wider than the column, they scroll (the owner's rule above). While something other than plain playing is going on the third line says it instead: `Opening...`, the error text (red), `Finished`, the MD5 check and its verdict.
- Under the text the **pill row**: a format pill (accent_soft, `FLAC 16/44.1`; on a Bluetooth output the codec that leaves the box: `SBC 44.1`, `SBC-XQ`, `AAC`) and an outline pill with the output (`Wired`, or the headset's name, cut with dots when long). A test tone shows `TEST SIGNAL`.
- The **progress bar** is 4 px, elapsed on the left (a pause glyph before it while paused), remaining on the right (`-~` for an estimated MP3 length), and a quiet ink3 word in the middle while either is on: `shuffle`, `repeat`, `repeat 1`, `shuffle . repeat`.
- The **volume strip** at the bottom: `Volume -18 dB` (or `Headset 62%` on a headset with absolute volume, same 0..127 scale) and a 4 px ink2 bar over the right 55 %. A headset left at 0 % while playing reads `Headset 0% . silent` in amber (the old "headset volume 0 %" hint).
- Status bar: `n of m` at the left, `Now Playing` in the middle.
- Scroll adjusts **volume**. **Select**: a short press opens **page 2**; **hold Select** (about 0.5 s, with a haptic tick) is **seek mode**. **Play/Pause** toggles; **Left** / **Right** previous / next track; hold Left / Right scans the track at 10x
  and applies on release (the old behaviour). **Menu** returns to the list the track was chosen from (in seek mode it cancels the seek first).

**Seek mode (NP-2).** The pills, the album line and the volume strip give way to the seek time in the Big font (amber), the progress bar becomes 6 px amber (soft amber track) with a knob, and the hint `Scroll: seek . Select: go . Menu: cancel` takes the bottom line.
The wheel moves the target (about 1/60 of the track per step, 1 to 30 s); **Select applies it** (one restart of the track at that frame) and leaves seek mode; **Menu drops it**. Nothing is applied by the wheel resting, and a seek mode left alone for 10 s is cancelled (so a forgotten seek never keeps the wheel; the old 3 s fallback is gone).
A track that cannot seek (a FLAC without a seek table, the MD5 check) answers the hold with the toast `Seek not available` and stays on volume. The volume HUD shows while seek mode hides the strip. Logic: `ui_npc_*` in `ui_logic.c` (`ui_npc_select_down` / `_up`, `ui_npc_menu`, tick), host-tested in `ui_logic`. The old "Select toggles volume / seek" path is gone.

**Page 2 (NP-3).** Small art (64 / 96) and title, `Artist . Album`, then the **star rating** (five stars, lit ones accent): wheel up from the first row focuses the stars (a soft box behind them), the wheel then sets 0 to 5 stars, Select or Menu returns to the rows. Rows: **Shuffle** (Off / Songs), **Repeat** (Off / One / All), **Gapless** (On / Off), each toggled by Select
(the same settings as Settings), **Add to playlist (phase 4)** (greyed; Select says `Playlists in phase 4`), **Go to album** and **Go to artist** (open that album / artist in the library with the track's own ids: the storage task keeps the album and album-artist id of the last two library tracks it resolved,
`library_track_ids()`; greyed with `Not in the library` when the track plays from a folder or is a compilation, which has no album artist). Menu goes back to page 1. Page 2 needs a playing file; if playback stops under it, it closes.
**Ratings are saved on the card** (`/.neupod/ratings.bin`, `ratings_store.[ch]`), keyed by file path and size so a rebuilt library keeps them; saved by the storage task under the settings store's write scope (confined to `/.neupod`), loaded when the card is ready. On a write-protected or failing card they stay in RAM and are written as soon as the card allows (retry every 30 s); up to 128 tracks (the least recently rated is dropped). The file is rewritten in place: a power cut during a save loses the ratings (the CRC makes it load as empty), nothing else.

**Volume HUD (NP-4).** A volume change while another screen is on top (the Y2's side keys, the headset's own volume, anything that moves the player's volume) shows the HUD strip for 1.2 s: `Volume -18 dB` or `Headset 62%` with the bar (`update_volume_hud()`, `ui_frame.c`; it polls `player_volume_att()` and `bt_volume_get()`).
Screens that draw the volume themselves (Now Playing page 1 outside seek mode, Test tone) set `ui_screen_t.shows_volume` and get no HUD. Not shown while the screen is off or Hold is on, nor when the volume in use changes (a headset taking over). List scrolling is unchanged: the wheel still only scrolls.

**Bluetooth link lost.** Unchanged ADR-0019 behaviour, new words: playback pauses on the drop and stays on Bluetooth; when the five minutes of automatic reconnecting end with the headset still away the toast says `Couldn't reconnect - paused` (`bt_service_gave_up_seq()`).

- On Bluetooth output the format pill reads the negotiated codec: `SBC 44.1`, or `SBC-XQ` when the headset accepted a bitpool above 53 (the source format is not shown then). A track that is not 44.1 kHz cannot be played on Bluetooth yet and is refused with "not supported on Bluetooth" (see [Bluetooth](bluetooth.md#stage-2b-the-audio-stream-built)).

## Album browser

![Album browser](assets/ui/album-browser.svg)

- Dark full-screen carousel of album artwork; the centre album is enlarged with a reflection.
- Scroll flips through albums with acceleration; **Select** opens the centre album's track
  list; **Menu** returns to the library.
- This view is the one place the UI uses the panel's colour depth heavily; on the AMOLED
  candidate the black background also saves power.

## Settings

![Settings](assets/ui/settings.svg)

- Two-pane: list of settings groups on the left, a live detail pane on the right for the
  highlighted group (shown: Bluetooth — connected device, codec, battery).
- **Select** enters a group; inside, scroll changes the value and Select confirms.
- "Click" is where the piezo / haptic / both / off choice lives (the piezo has no driver yet), and "Haptics" holds the
  actuator (ERM / LRA) and Calibrate rows; hardware decision in [Requirements → Reused parts and mechanical](requirements.md#reused-parts-and-mechanical).
- "Output" chooses headphone / line / optical / USB when the jack detect cannot decide.

## Resolution profiles

The product panel is 320x240 and the mock-ups above are drawn at that size, but the firmware UI is resolution-independent
([ADR-0018](adr/0018-ui-resolution-profiles.md)): every pixel size lives in one struct, `ui_metrics_t`
(`app/ui/ui_metrics.h`, values in `app/ui/ui_metrics.c`), and `ui_task` picks the profile once, after `hal_display_init()`, from
`hal_display_width()` / `hal_display_height()` (the largest profile that fits the panel; 320x240 when none does). Screens
read `UIM->...` (the active profile); `UI_W`, `UI_H`, `UI_STATUS_H`, `UI_CONTENT_H`, `UI_ROW_H` and `UI_ROWS_VISIBLE` in
`ui_internal.h` are shorthands for its first fields. Nothing outside `ui_metrics.c` scales a length.

| | 320x240 (product panel) | 480x360 (Y2 testbed, host `NEUPOD_DISPLAY_SIZE=480x360`) |
| --- | --- | --- |
| Status bar / list row | 24 / 30 px | 36 / 45 px |
| Content area, rows per page | 216 px, 7 | 324 px, 7 |
| Fonts small / body / large / huge | Montserrat 12 / 14 / 20 / 48 (own build, Latin-1) | Montserrat 16 / 20 / 28 / 48 |
| Icons (Lucide, 1-bit) small / large | 16 / 24 px | 24 / 36 px |
| Home split (list / pane), art box | 179 / 141 (56 %), 117 px | 269 / 211, 175 px |
| Now Playing art box, text column | 112 px at (14, 14), x = 140 | 168 px at (21, 21), x = 210 |
| Settings split (groups / summary) | 179 / 141 | 269 / 211 |
| Splash icon, icon row, wordmark | 32 px, y = 142, 48 pt | 48 px, y = 213, 48 pt (the largest size built) |
| LVGL partial-render buffers | 2 x 320 x 24 RGB565 (15 KB each) | 2 x 480 x 36 (34 KB each; hosted builds only) |
| Toast / HUD strip, lock pill | 22 / 26 / 24 px | 33 / 39 / 36 px |

The 480x360 profile is every length of the 320x240 one times 3/2 (the `S()` macro in `ui_metrics.c`, used only in that
table), and fonts step to the next size (14 to 20, 20 to 28: 1.43x and 1.4x, a little under 1.5x, so text that fits at 320
fits at 480). Row counts do not change: both profiles show 7 rows. Adding a profile is one more initialiser in
`ui_metrics.c`; `tests/test_ui_metrics.c` checks every compiled-in profile (title bar and rows fit the height, art box fits
above the progress bar, text lines stack without overlapping, splash captions fit one line, the static LVGL buffers are big
enough) and pins the 320x240 numbers to what the UI had before profiles existed.

**What is compiled where.** `NEUPOD_UI_HIRES` (CMake option; default ON for `NEUPOD_PLATFORM=host` and `y2`, OFF for rp2350 and
esp32s3) adds the 480x360 profile, Montserrat 28 (`lv_conf.h`), 480-wide render buffers (`UI_BUF_W_MAX`), a 192 KB LVGL pool
(`memory_budget.h`) and the layout audit (`ui_audit.c`). Without it the product build has the one 320x240 profile as a compile-time constant, so
the RP2350 image is unchanged (flash and RAM identical in size; see the ADR). A hosted build still picks its profile at run time,
so a host built with the option runs the 320x240 layout byte for byte as before unless told otherwise.

**Host.** `NEUPOD_DISPLAY_SIZE=WxH` (read once; between 320x240 and 640x480, default 320x240) sets the simulated panel size.
`NEUPOD_UI_AUDIT=1` makes every snapshot also walk the widget tree and print `ui: AUDIT FAIL ...` for a visible widget that
lies outside the panel or its container (one pixel over the container's far edge is tolerated; only the list's off-window pool rows
may be clipped away) and for a label whose text does not fit its box (dots / scrolling labels are not measured). Each snapshot prints
`ui: audit '<name>' WxH objects=N violations=V`; the tests require that line. At start-up the UI logs `ui: profile WxH on WxH panel`
and warns when the panel is bigger than the profile (including a build without `NEUPOD_UI_HIRES` on a panel above 320x240). ctest runs
the tours at 480x360 as `hires_tour`, `hires_input_tour`, `hires_wifi_tour`, `hires_bt_tour` and `hires_art_tour` (snapshots in
`build/host/snapshots_hires*`, checked by `hires_art_snapshot`), and the default `runtime_smoke` tour runs the audit at 320x240. Each hires test has its own fake card, so `ctest -j3` (as CI runs it) is safe; the 320x240 tours share one card and hold a resource lock. `neupod` exits 3 when fewer snapshots were written than the tour promised (typically `--run-ms` too short), 2 when the audio task starved, 4 without audio stats.

**Known limits at 480x360.** The album art is still decoded to a 64 x 64 thumbnail (`ART_MAX_DIM`, art.h; 8 KB of static RAM on
the product build) and scaled up by LVGL, 2.6x into the 168 px box, so it is softer than at 320x240. The 48 pt splash wordmark and
the note glyph do not grow (48 is the largest size built). The bring-up scenes (`gfx_strip`, `now_playing_draw`, the display
test menu) are 320x240 only.

## Fonts and display text

The UI draws with its own builds of Montserrat Medium (SIL OFL 1.1) at 12 (Small), 14, 16 (Small at 480x360, hosted builds only), 20, 28 (480x360, hosted builds only) and 48 pt, 4 bpp, merged with
the few Font Awesome symbols that are still drawn from the fonts (`LV_SYMBOL_*`: note, tick, image, Wi-Fi, battery 3, Bluetooth, SD card; the rest of the symbols moved to the Lucide icons, below). They are generated files in `app/ui/fonts/` (`tools/gen_ui_fonts.sh` rebuilds them; the
command line, sources and licences are in `app/ui/fonts/README.md`) and replace LVGL's built-in `lv_font_montserrat_*`, which are switched off in
`lv_conf.h` so the flash is not paid twice (the themes' default font is the 14 too). The metrics profiles point at them (`neupod_font_14` and so on).

| Font | Characters |
|------|------------|
| 12, 14, 16, 20, 28 | ASCII (U+0020 to 007E), Latin-1 (U+00A0 to 00FF, without the soft hyphen), bullet, en and em dash, curly single and double quotes, ellipsis, euro, and the 7 symbols still drawn from the fonts |
| 48 | ASCII and the note symbol only (the splash wordmark and the art-less note; it never shows a name) |

Latin-1 covers the accented letters of Western European names (é ü ñ ø å ß ...) and `¿ ¡`: some rippers write `¿` where the title had a `?`, and it is a
real glyph here. Line heights and baselines are set back to the built-in Montserrat values (the generator would otherwise grow every line by 2 to 5 px
for the accented capitals), so every profile's layout is unchanged; an accent on a capital may reach a pixel or two above the line box.

**Display-text filter.** `ui_text_display()` (`ui_logic.c`, host-tested) turns a raw UTF-8 string into what the fonts can draw, so nothing renders as an
empty box. It keeps every character the fonts have (`ui_text_has_glyph()` is that list, and a host test checks it against the compiled fonts) and maps the
rest: combining accents and zero-width marks are dropped (a name stored as `e` plus a combining acute shows as `e`), other control characters and odd
spaces become a space (a `\n` is kept for the multi-line panes), other dashes, quotes and the minus sign become their ASCII look-alikes, Latin
Extended-A letters become the base letter (`Ł` to `L`, `Œ` to `OE`), fullwidth forms become ASCII, and anything else (CJK, Cyrillic, emoji) becomes one `?`
per character. Malformed UTF-8 becomes `?`; a sequence cut off at the end of the string is dropped. The result is never longer than the input and the
copy is cut only at a character boundary. Stored names and tags are never changed: the filter runs only on the way to a label, so a file is still found
and opened by its real name.

`ui_label_set_text()` (`ui_frame.c`) applies it: it is how a label gets text from a file name, a tag, a device name or a network name, and it leaves the
label alone when the result is what it already shows. Everything drawn from such a string goes through it: the Files and library rows (and their detail
text), the screen titles (a folder name), Now Playing title, artist and album, the Home screen's playing lines, the toasts, the Settings panes
(Bluetooth and Wi-Fi names) and the Bluetooth and Wi-Fi lists. Text the wheel keyboards build is typed from their own character sets and drawn as is.
New code that puts a media string into a label must use it, and a new `LV_SYMBOL_*` needs its code point in `tools/gen_ui_fonts.sh` and
`ui_text_has_glyph()`. Lucide icons are drawn with `ui_icon()` (an icon-font label, never run through the filter).

Flash cost on the RP2350 (Release): see the CHANGELOG entry for UI-17.

## Design system (2026-11-12 review)

From the owner-commissioned [design review](design/2026-11-12-ui-review/README.md); phase 1 (the foundation, backlog `DS-1` to `DS-7` and `HL-1`, see [Backlog](backlog.md#design-review-2026-11-12))
is built. The decisions the owner took where the review conflicted with earlier ones are in the README of the review and apply to every phase (Now Playing: Select opens page 2, hold Select seeks;
a lost Bluetooth link stays paused on Bluetooth, ADR-0019; Light by default; the Pico 2 W is frozen; every third-party asset goes into `LICENSE.md` and the Licences screen).

**Colour tokens and themes (DS-1).** Every colour the UI draws is one of 14 tokens (`ui_theme.h`: bg, surface, bar, line, ink, ink2, ink3, accent, on_accent, accent_soft, amber, amber_soft, green, red),
each with a Light and a Dark value from the review (RGB565-safe: red and blue multiples of 8, green of 4; `test_ui_metrics` checks it and the text contrast). Code names a token (`UI_C_INK`), never a hex value: labels,
backgrounds and borders use shared styles that read the active theme (`ui_style_text()`, `ui_obj_bg()` ...), so switching rewrites those styles and restyles everything at once (one full repaint). The boot splash keeps its own fixed
palette, and the art placeholder is not themed. **Settings > Display > Theme** is Light (default), Dark or Auto and is stored (`SETTING_THEME`, `NPth`); Auto is dark exactly while the backlight is dimmed or off (`ui_theme_is_dark()`), so with Auto-dim off it behaves like Light until the screen goes off (owner decision),
switched before the backlight comes back on a wake. Contrast: the dark theme's white-on-accent is 4.2:1 (below 4.5:1) and the light one 6.4:1 (the review quotes 7.3:1).

**Type scale (DS-2).** Title = the large font (20 / 28), Body = 14 / 20, **Small** = 12 / 16 (secondary lines, times, pills, hints, the status bar's left slot; built from the same Montserrat Medium and the same glyph coverage), Big = 48.

**Text that does not fit (ticker).** Owner decision: one-line text wider than its box scrolls round instead of ending in dots, and a one-line label is always one line high (a dotted or scrolling label with a taller box would wrap). `ui_label_ticker(label, on)` (`ui_frame.c`) switches a label between dots and LVGL's circular scroll: 1 s pause, then `ticker_px_s` (35 px/s, 52 at 480x360) with a gap of three spaces between repeats, only while the text overflows. Used for the Home track title and artist, the focused list row's main text (`style_row`; the other rows keep dots and the label goes back to dots, rewound, when focus leaves) and the status-bar title. Only the label's own box is invalidated per frame. At most eight tickers exist at once. `ui_ticker_set_active(false)` (screen off) parks every ticker; `ui_frame_settle()` rewinds them, so snapshots always show the start position. Rule for later phases: Now Playing title, artist and album use it. Tour: `text-home` (Home with a long title and artist) and `text-longrow` (a focused Files row that is too long), ctest `text_tour` / `hires_text_tour`; the layout audit fails a dotted or ticker label whose box is two lines high.

**Metrics.** Status bar 24 / 36, row 30 / 45 (7 rows in both profiles), padding 10 / 15, selection bar inset 3 / 4 and radius 4 / 6, Home and Settings split 56 / 44, scroll bar 2 px wide with a track, toast strip one row high (30 / 45),
HUD strip 26 / 39, lock pill 24 / 36, Power-hold line 3 / 4 (all in `ui_metrics_t`).

**Status bar (DS-3).** Left slot (Small, ink2), centred title, right-hand group (Wi-Fi, Bluetooth, lock, battery percentage, battery; hidden members close up). The slot belongs to the screen on top:
`ui_screen_set_left(screen, text)` (the text is kept with the screen, so a pop restores it). Built so far: the clock on Home, "n of m" on Now Playing (while a queue plays), the item count on the Music lists, the path in Files
(the title stays "Files"). The battery is drawn (outline, terminal, fill), never "--": the fill is ink, green while charging, red below 15 %, and the percentage shows only below 20 % or while charging.
The Bluetooth icon is ink3 for a saved headset that is not connected, ink connected, accent streaming, hidden with no radio or no headset. (The three busy dots of a background library build are not drawn yet; the slot is the place.)

**Selection bar (DS-4).** One rounded bar, inset from the list edges, behind the rows (`ui_list.c`); the rows are transparent. It slides to an adjacent row in 80 ms; a move of more than three rows (a fast spin) and a new list
jump without animation. The selected row's text, detail and chevron use on_accent, the rest ink / ink2 (greyed rows ink3).

**Strips (DS-5, HL-1; `ui_strips.c`).** `ui_toast_show(text, WARN | INFO, ms)`: a one-line strip under the status bar (amber_soft for a problem, accent_soft for information), slides down from under the bar in 150 ms and out in 200 ms. The
audio route's "Headset disconnected" / "Headset not connected", the Files notices ("Not a playable file", "Path too long") and the library's "Can't play" are toasts now (they were a banner over the first row and a row at the bottom). (There is no "Headphones unplugged" notice
in the firmware.) `ui_hud_show(label, pct, ms)`: the bottom strip (ink, Small text and a bar) for a value changing from any screen, 120 ms in, 1200 ms hold, 200 ms out, the fill follows in 60 ms; the volume HUD itself is NP-4 (phase 2).
`ui_pill_show()` / `ui_hold_line_set()`: the lock pill and the Power-hold line (see Hold). None of them dims or blends anything behind it. The host tour tokens `toast:warn:Text_with_underscores`, `toast:info:...`, `hud:Label:PCT`, `strips:off` and `theme:light|dark|auto`
drive them for `--tour-design` (ctest `design_tour`, `hires_design_tour`).

**Icons (DS-6).** The Lucide set (ISC; chevron-right and lock derive from Feather, MIT) as 1-bit bitmaps at 16 / 24 px (320x240) and 24 / 36 px (480x360): chevron-right, play, pause, skip-back, skip-forward, shuffle, repeat, repeat-1, volume-2, bluetooth, wifi, lock, folder, file, and lock-open (`ui_icons.h`). They are generated from the pinned `lucide-static`
package by `tools/gen_ui_fonts.sh` into `neupod_icons_*.c` and drawn with `ui_icon()` like a label, so they take a theme colour. Lists use the chevron, the Files rows the folder and file icons (`ui_list_item_t.icon`), the status bar Wi-Fi, Bluetooth and lock,
Now Playing play / pause / shuffle / repeat. Battery and signal bars stay drawn shapes. The licence text is `app/ui/fonts/LICENSE-Lucide.txt`.

**Motion (DS-7).** Push slides the new screen in 24 px over 120 ms (ease-out), pop slides the revealed screen in from the left over 100 ms; no alpha, only the content area repaints. (The review fades the popped screen out; the popped screen is destroyed at once, so the mirror slide is used.)
LVGL runs at 30 fps (`LV_DEF_REFR_PERIOD 33`, `lv_conf.h`) on every build; the Y2 stays at 30 until a vsync'd framebuffer is confirmed there. A snapshot or the layout audit first settles any slide or strip in flight (`ui_frame_settle()`).

**Phase 2 (NP-1 to NP-5, LB-1 to LB-3)** is described under Now playing, Music lists and Now Playing art above. Layout numbers (320x240 / 480x360 = x1.5, `ui_metrics_t`): Now Playing art 112 at (10, 12) in the content area, text column at x 134, title y 10, artist y 40, album y 60, pills y 102 (18 px high), progress bar y 142 (4 px, 6 px in seek mode, knob 14), times y 152, volume strip y 190;
two-line rows 40 px (title y 3, second line y 21); album header and page 2: art 64 at y 8, rule at y 76; letter tile 44 px at (w - 58, 14), rail from y 6 over 204 px. The tours `--tour-seek` (`seek-volume`, `np-nowplaying`, `seek-mode`, `seek-scrub`, `seek-cancel`, `seek-go`, `np-page2`, `np-volume-hud`, `seek-mp3`, `seek-unavailable`), `--tour-library` (`lib-songs` two-line, `lib-volume-hud`, `lib-songs-fastscroll`,
`lib-album` with the header) and `--tour-art` snapshot the new screens at both resolutions with the layout audit on every one; the tour tokens `sel:down` / `sel:up` (with `sleep:MS`) hold Select, `vol:N` changes the volume from outside the screen and `rail:hold` / `rail:release` keep the fast-scroll overlay up for a snapshot.

## Implemented (stage 2: home menu)

Firmware screens in `app/ui/` (LVGL, light theme as in the mock-ups above; all drawing is original). Rendering
is partial (two 320x24 strip buffers at 320x240; see Resolution profiles); the list scrolls with one short animation, everything else is static.
Nothing persists across a reboot yet (settings are RAM only).

**Common frame.** A status bar on every screen: title in the middle, battery icon and percentage on the right
("--" when the gauge failed its check; green with a charge symbol while charging, red at 15 % or less), and a
play/pause glyph slot on the left (empty: nothing plays yet), then the Wi-Fi glyph and the **Bluetooth glyph**: grey = a headset is remembered but not
connected, dark = connected, accent blue = streaming audio, hidden when Bluetooth is not up or no headset is remembered and nothing is connected (the bar is light, so the
"connected" colour is the text colour and not white). A **toast** (the strip under the status bar, 3 s, on any screen): "Headset disconnected" when the headset goes away
while playing (the player is paused at once, the position kept, and the output stays Bluetooth), "Headset not connected" when Play is pressed on the Bluetooth output with no
headset. **Menu** goes back one screen (nothing at the top
level), **Select** enters. Lists clamp at both ends (no wrap), the selection bar jumps and the list scrolls
smoothly when the selection leaves the window; a thin scroll bar shows on long lists.

**Scrolling.** The wheel reports counts; 4 counts make one list step, and fast movement multiplies it (2x, 4x, 8x
when many counts arrive within 150 ms; the constants `UI_COUNTS_PER_STEP`, `UI_ACCEL_*` are in
`app/include/runtime_config.h`). Each testbed key press is exactly one step.

| Screen | What it does now |
| --- | --- |
| Home | Left list: Music, Podcasts, Photos, Extras, Settings, Shuffle Songs, Now Playing. Right pane follows the player (polled on every UI tick, and at once when Home comes back on top; the clock and card summary refresh twice a second). Playing a file: the 96 px art box shows the album art thumbnail (the note when there is none), then title (file name when there is no title tag), artist, and a play or pause symbol with the queue position (`3 of 12`, or just `Playing` / `Paused` without a queue; `Finished` once the last track has ended, and `Error`, `Verifying` or `Verified` for those player states, detail stays on Now Playing). Test tone: note, tone name, `Test signal`, play symbol. Nothing playing: the artwork placeholder, `Nothing playing`, the clock (`--:--` until the time is set) and a card summary (type and size, or `No card`). |
| Music | Artists, Albums, Songs, Genres, Compilations, Playlists (`None yet`) and Update Library; the lists are described under [Music lists](#music-lists). |
| Shuffle Songs | Plays the whole library shuffled, building the index first if there is none; Menu on Now Playing comes back to Home. |
| Podcasts, Photos | Placeholder screens saying what is missing. |
| Now Playing | Live: title, artist and album with track number from the file's tags (the file name if there is no title), a format badge such as `FLAC 24/96`, `MP3 320k` or `ALAC 16/44.1`, `Playing` / `Paused` / `Finished` or the error, elapsed and remaining time with a progress bar, and the volume with a level bar. **The wheel is the volume** (2 dB per step, starts at -30 dB; on the Bluetooth output with a headset that has absolute volume it is **the headset's volume**: the label reads `Headset 62%`, the bar is the same scale, a swipe on the headset's stem moves it, and a headset at 0 % shows `Headset 0%` with `Playing - headset volume 0%` on the status line; the software volume then sits at 0 dB, see [Bluetooth](bluetooth.md#stage-2c-avrcp-volume-buttons-metadata-built)); **Select hands the wheel to seek** (below) and back; **Play** pauses and resumes (Select no longer does), and on `Finished` (or after a FLAC MD5 check) plays the same file again from the start, with sound; **Right / Left = next / previous track of the queue** on a short press (acted on at release; Previous more than 3 s into a track restarts it; Next on the last track stops and the screen goes idle); **holding Right / Left for 0.6 s fast-forwards / rewinds** (10x: the bar and the times run ahead, the track jumps there when the key is released; it also ends by itself at the start or end of the track and after 30 s, and another key, the wheel or another screen opening drops it unapplied); with more than one track queued the status line reads `Playing - 3 of 12`; **Menu goes back while the sound continues**. Without anything playing it says `Nothing playing`. Play/Pause works on every screen, and so do **Right / Left (next / previous track) while a queue is playing**, on every screen that does not use them itself (the Files browser keeps Right for the MD5 check of a `.flac`, the Test tone screen and the sliders keep them for their values, text entry keeps them). **Seek mode** (WAV, ALAC, MP3, and FLAC with a seek table; other tracks answer `Seek not available` for 2 s and the wheel stays on volume; WAV, ALAC and FLAC land on the exact frame, an MP3 on a frame header found from the Xing or VBRI table or, for constant bit rate, the byte proportion, so within a frame for CBR and within about 1 % of the track for a VBR file, and the bar then shows where it really landed): the progress bar turns orange and the volume bar grey, the volume label becomes `Seek 1:23`, each wheel step moves the position by about 1/60 of the track (1 to 30 s, accelerated like list scrolling), the new position is applied 0.35 s after the wheel stops (the track restarts at that frame: a short mute, paused stays paused) and the wheel drops back to volume 3 s after the last turn. A scrub or hold is tied to the track it was made on: if another track starts first (gapless handoff, skip) it is dropped. A position at the very end is the end of the track (the queue moves on). |
| Extras | Files, Test tone. |
| Extras, Test tone | Three rows, each `On` / `Off`: **1 kHz tone**, **L/R ident** (1 s left, then 1 s right) and **Scale** (C4 up to C5 and back, looping). Select starts or stops the highlighted one (starting one stops the other); **Left / Right = volume**; the screen shows the volume and the level reaching the DAC. Leaving the screen stops the sound. See [Audio](audio.md). |
| Extras, Files | **Read-only** browser of the card: folders first then files, alphabetical, hidden entries skipped (a name starting with a dot, or the FAT hidden / system attribute: `System Volume Information`, `$RECYCLE.BIN`; the host build has no such attribute, so only the dot rule applies there), size on the right, a chevron on folders. Select opens a folder, Menu goes up (at the root it leaves). **Select on a `.wav`, `.flac`, `.mp3` or `.m4a` (ALAC) plays it** and opens Now Playing; the rest of that folder (playable files only, in the order shown, from the chosen one) is queued and plays on by itself, gaplessly when the rate matches; **Right on a `.flac` verifies its MD5 without sound** (see [Audio](audio.md)). Other files show `Not a playable file` for 3 s; an AAC `.m4a` opens Now Playing with `AAC not supported yet`. At most 256 entries per folder are kept (`... more (first 256 shown)` row after them); names longer than 63 bytes are cut at a character boundary. Directory reads run in the storage task, so the screen shows `Loading...` and keeps responding; without a working card it says why (`Storage unavailable: no card`). |
| Settings | Two panes (groups left, live summary right). **About**: first row **Licences** (Select opens the list of third-party components and their licences; Select on one shows its licence and a short notice; kept in step with `LICENSE.md` by a test, see [Licences](#licences) below), then **Diagnostics** (Select opens a list: the result of every boot check, heap free, core 1 stack, audio blocks / underruns / jitter, and the Bluetooth link: codec, bitpool, bit rate, underruns, frames, drift trims), then version, build hash, platform and uptime. **Display**: Select opens a five-row list: **Backlight** (Select, then scroll adjusts it in 5 % steps, never below 5 %), **Auto-dim** (Select toggles On/Off, default On), **Dim after** (Select cycles 10 s / 15 s / 30 s / 1 min / 2 min / 5 min, default 30 s; greyed and inert while Auto-dim is Off), **Screen off after** (Select cycles Never / 15 s / 30 s / 1 min / 2 min / 3 min / 5 min, default 3 min; counted from the dim, or from the last input when Auto-dim is Off) and **Auto-lock** (Select toggles On/Off, default On: Hold turns on when the screen goes off, [Hold](#hold)); all saved on the card. After the idle time with no input the backlight steps down to 10 % (a set level below 10 % is left alone, idle never brightens); any input restores the set level at once and is delivered to the screen as normal. After the Screen off time the backlight goes fully off; **the input that wakes it is not delivered** to the screen (and with Hold on, nothing is). The idle clock starts when the menu appears. **Click**: Select cycles Haptic / Off today (default Haptic, which needs the haptic driver); Piezo and Both join the cycle once a piezo driver exists; saved on the card, and an older saved On/Off still reads as Haptic/Off. **Haptics**: Select opens a two-row list: **Actuator** (Select toggles ERM / LRA; a changed type calibrates itself) and **Calibrate** (Select runs the auto-calibration, about a second; shows Not needed / Not done / Running... / Done / Failed); see [I2C peripherals](i2c-peripherals.md). **Gapless playback**: Select toggles On/Off (default On); saved on the card; Off keeps the auto-advance but with the ordinary stop and start between tracks. **Resume playing**: Select toggles On/Off (default Off); saved on the card. Off: the last track comes back paused at its place. On: if the player was playing when it was last used, it plays on again at boot (if it was paused or stopped, it still comes back paused). **Bluetooth** (design review BT-1 to BT-3): the right pane names the remembered headset and its state (or "No headset saved"); Select opens a short page: **Audio output** (Select cycles Auto / Wired / Bluetooth, saved on the card; Auto = Bluetooth whenever a headset's stream is open, shown as "Auto: Bluetooth" while it is; **Bluetooth** makes Bluetooth the output at once, headset or not, and keeps it through a Disconnect; Wired never uses Bluetooth; changing the output while a track plays stops it, the place is kept, like Stop), **Devices** (chevron; the remembered headset's state, "None", or why the radio is not usable) and **Absolute volume** (greyed, read-only: Yes when the connected headset reports one, No, or "-" with no link; the firmware has no switch for it). There is no Bluetooth On / Off row: the radio drivers have no clean power-down (backlog). **Devices** starts a scan as it opens (not while audio is streaming: an inquiry would stutter it; then "Scan again" does it on request) and fills in live: the **remembered headset first** with its state on the right (Connected, Streaming, Saved, Pairing..., Connecting..., Reconnecting... with busy dots, No audio, Failed: reason), then the **devices found** (audio devices first, strongest first once the scan ends; phones and other non-audio devices are greyed and carry "(phone)", "(computer)", "(wearable)" or "(other)"), each with the **monochrome three-bar signal** (ink and line, no traffic-light colours), then **Scan again** (**Stop scanning** while it runs). While a scan runs a strip at the bottom says "Scanning" with four dots filling in ink3; the status bar's left slot counts what was found. **Select on a found audio device pairs** ([Bluetooth](bluetooth.md#pairing-and-the-connection-stage-2a)): the scan stops, the row's right text becomes "Pairing..." with busy dots, then the device moves to the top as the remembered headset ("Connected", "Streaming") or the row says "Failed: <reason>"; **Menu cancels a pairing** (the next Menu leaves). One headset is remembered: with one already saved, Select on a found device asks **"Replace <old> with <new>?"** (Select = replace, Menu = keep). A non-audio row answers with the toast "Not an audio device". **Select on the remembered headset opens a small list**: **Connect** (Disconnect while connected, Cancel while working; greyed while a scan runs) and **Forget**, which asks **"Forget <name>?"** (Select = Forget, Menu = Keep: a question screen, no row ever changes its own meaning). Renaming a headset is not offered. The link's life is told by toasts: "<name> disconnected . reconnecting..." (warning) when a connected headset goes away, "<name> connected" (info, 1.2 s) when the automatic reconnect gets it back, "Couldn't reconnect - paused" (warning) when the five minutes run out; playback stays paused on Bluetooth and the output is never switched by itself (ADR-0019). Names from the headset go through the display-text filter ([Fonts and display text](#fonts-and-display-text); the stored name is untouched); up to 48 bytes are kept. **Wi-Fi**: on/off, status (name, address, signal as a meter), **Scan for networks** (opens the **Networks** screen: it scans as it opens; first row Scan again, then the networks found and the remembered ones, a name with `*` is remembered; each name once, with its strongest access point; Select on one starts the join flow, Menu goes back), Other network..., time zone, the time (set by hand if there is no network), Wi-Fi while playing; see [Wi-Fi](wifi.md). **Storage**: card type, size, filesystem, mode, free space (computed by the storage task, `computing...` until done). **Battery**: voltage, charge, rate, state, alerts, gauge status; live, once a second. |

### Now Playing art

The 112 x 112 box (168 at 480x360) at the top left of Now Playing shows the album art of the track that is playing; page 2 and the album header show it at 64 / 96 px.

**Thumbnails and cache (NP-5).** Hosted builds (the simulator and the Y2; `NEUPOD_ART_HIRES`, on by default there) decode to a **160 x 160 RGB565 thumbnail** (50 KB) and keep the last **24** albums (1.2 MB static: 24 slots, least recently used replaced first, the slots on screen are never replaced);
nothing is scaled up: LVGL draws the thumbnail smaller into the box (160 to 112, 96, 64), except Now Playing at 480x360 where the 168 px box shows it 5 % larger. The cache key is the one that already identified an album (folder, album tag, embedded picture size), so a track of an album played or browsed before shows its art at once. The **Pico 2 W** build (frozen, ADR-0001 amendment) keeps the 64 x 64 thumbnail (8 KB), no cache and no browse art
(`ART_CACHE_N` 1, `art.h`); an RP2350B with PSRAM turns the large thumbnails on with `-DNEUPOD_ART_HIRES=ON`. Measured RAM: 24 x 51,200 B = 1,228,800 B of BSS on the host and the Y2 (the Y2 has plenty); RP2350 unchanged.
The album header asks the art service for the album's picture by the library handle of its first song (`art_browse_want()`): the storage task resolves it, reads the file's header for an embedded picture, and decodes it into the cache when it is idle (the playing track's art always goes first).

(The table and text below describe the decoder, written for the 64 px thumbnail.)

| State | What the box shows |
| --- | --- |
| Track just started, art being found or decoded | The dark box with a note glyph (the audio always starts first; art never delays it) |
| JPEG found | The picture, kept in proportion and centred, with the dark box as letterbox bars |
| No art, PNG, progressive JPEG, damaged JPEG, picture too large (over 5120 px) | The note glyph |
| Nothing playing, a test tone, an error, or still opening | The note glyph |

Where the picture comes from, in order: the picture inside the file (MP3 ID3v2 `APIC` / `PIC`, FLAC `PICTURE`, MP4 `covr`; a JPEG is preferred to a PNG, a front cover
to other types), then `cover.jpg`, `folder.jpg`, `front.jpg` in the track's folder (any letter case; that order). An embedded PNG is skipped without being read, and the
folder is then tried. A JPEG that is embedded but progressive or damaged also falls back to the folder files. PNG and progressive JPEG need the PSRAM board
([ADR-0013](adr/0013-bluetooth-ram-psram.md), amendment of 3 Nov 2026).

How it works: after the track has started, the storage task (core 0, the only user of the card) decodes the JPEG with TJpgDec straight from the card in 512-byte reads, shrunk by the
decoder to 1/1, 1/2, 1/4 or 1/8 (the largest shrink that keeps the long side at 64 pixels or more), one MCU row at a time, and averages it into one **64 x 64 RGB565 thumbnail (8 KB of static RAM)**; the
whole picture is never in RAM. LVGL shows the thumbnail scaled to fit the box (1.75 times for a full-size thumbnail; soft edges are the price of the RAM). While it decodes, the task keeps the audio data ring topped up, sleeps 1 ms
after every 2 ms of work (so input and UI keep running, the storage task outranks them) and stops as soon as another request is queued (it restarts afterwards). The result is
kept for the album: a track whose folder, album tag and embedded picture size match the last one shows it at once, without decoding again. A big picture takes
roughly a second or two on the bench (the card read dominates); the `art:` line on the UART gives source, size, scale and time. Decoder working memory (about 8.6 KB at most) comes from the
heap for the length of one decode; if it is not available the note stays. Colour order: the thumbnail is native-order RGB565 like LVGL's draw buffers (the display driver swaps for the panel).

**Click feedback.** Every list step asks the haptic task for a click (ROM effect 3, a gentle click). The request is
a non-blocking queue write: the UI never waits on the I2C bus, requests closer than 35 ms are dropped, and the
driver goes back to standby after 2 s without a click or at any error.

**Keys.** UART testbed: `w`/`k` up, `s`/`j` down, Enter or `e` Select, `m` or Backspace Menu, `a` Left, `d` Right,
`p` or Space Play/Pause. Host window: Up/Down arrows and mouse wheel scroll, Enter Select, Esc/Backspace/`m` Menu,
Left/Right, Space/`p` Play/Pause. The wheel, when it works, sends the same events.

**Signal meter and busy dots.** Wi-Fi network rows and the Wi-Fi status row show signal as four small bars at the right edge instead of dBm (Bluetooth device rows use the monochrome three-bar meter, `ui_list_item_t.mono`: ink bars on line-grey, `ui_signal_bars3()` folds the four levels to three, no colour): 4 or 3 bars green, 2 yellow, 1 red (the unlit bars are grey). Wi-Fi: 4 bars at -60 dBm or stronger, 3 down to -70, 2 down to -80, else 1; Bluetooth is 5 dB stronger (-55 / -65 / -75). The thresholds and the 3 dB hysteresis are `UI_SIG_*` in `runtime_config.h`; `ui_signal_bars()` (`ui_logic.c`, host-tested in `test_ui_logic`) is a pure function. The Wi-Fi icon in the status bar takes the colour of the joined network's bars and changes only after the signal has moved 3 dB past a threshold, so it does not flicker. A list row whose action is still running (a join started from the Networks list, a Wi-Fi or Bluetooth scan, the radio starting) shows four pulsing dots in the same place: set `busy` on the row in the list model (`ui_list_item_t`, alongside `bars`) for as long as the work lasts and call `ui_list_refresh` when it changes; the list animates it with one timer that runs only while a visible row is busy. The model owns the end: a Networks join ends when the station is up, the join fails, or `UI_BUSY_TIMEOUT_MS` (the join deadline) passes.

**Console log (UART).** Every screen push and pop prints `ui: screen 'Title' push|pop [-> 'Revealed'] depth=N lvgl_mem=used/totalB` (modals too), and every list step prints `ui: sel <screen> <index> '<label>'`, so the UI can be driven with the testbed keys without seeing the panel. Passwords are never printed: the text widget is not a list.

**Host tour.** `neupod --tour --snapshots DIR` walks through the screens by script and writes `home`, `music-menu`,
`music-empty`, `extras`, `files`, `files-sub`, `files-many`, `files-many-end`, `settings`, `settings-bt`, `bt-idle`, `bt-scanning` and `bt-found` (the Devices screen), `bt-pair`, `about`, `nowplaying`
(PPM) into `DIR` (the tour's key script predates the longer Settings list, so several of these names no longer show the screen they are named for; use `--tour-bt` for Bluetooth);
`neupod --tour-bt --snapshots DIR` walks the Bluetooth screens and writes `settings-bt` (the Settings pane), `bt-idle` (Settings > Bluetooth), `bt-output-bt` (Audio output set to Bluetooth), `bt-scanning` (Devices with the four dots), `bt-found`, `bt-pairing` (the row's "Pairing..." with busy dots), `bt-paired` (the headset on top, Connected), `bt-options` (Disconnect / Forget), `bt-forget-asks` and `bt-replace-asks` (the question screens), `bt-disconnected` (Saved), `bt-forgotten`, `bt-failed` (a pairing the simulated car kit refuses), then the audio half on a card with a 20 s WAV album: `np-bt-badge` (Now Playing, `SBC 44.1`), `np-bt-volume`, `np-bt-volume0`, `bt-key-next`, `bt-about` and `bt-diag` (About with Diagnostics, and the Diagnostics list scrolled to the Bluetooth link rows), `bt-streaming` (Devices: Streaming, no scan started), `settings-bt-streaming`, `bt-dropped` (the `bt:drop` token makes the simulated headset vanish: the toast "... disconnected . reconnecting...", the player paused), `bt-reconnected` (the info toast, about 2.5 s later) and `bt-gave-up` (the give-up toast, set with the `toast:` token: the five minutes are not simulated); ctest `bt_tour` and, at 480x360 with the layout audit, `hires_bt_tour`; `neupod --tour-art --snapshots DIR` plays three one-track albums (cover file, JPEG inside the FLAC, PNG inside plus a progressive cover) and writes `art-cover`, `art-embedded`, `art-none` (ctest `art_tour`, checked by `art_snapshot`).
`neupod --tour-text --snapshots DIR` (UI-17) walks Extras > Files on a card with accented, `¿`, curly-quote, CJK and emoji names (`tests/tools/make_text_card.py`) and plays a FLAC with accented tags: `text-files`, `text-folder`, `text-nowplaying`, `text-home` (Home with the long accented title: one line, ending in dots) (ctest `text_tour` and `hires_text_tour`);
`ctest` runs it as `runtime_smoke` against a small fixture card (`build/host/fsroot_fixture`,
`NEUPOD_FS_ROOT`) and writes `build/host/snapshots/`.

### Phase 4 (built part): library states, Settings pane, Home

**Full-pane states (LB-4, `ui_pane.c`).** A state with no rows covers the list: heading (Title font), up to two Small lines, a bar with a Small caption, an accent button naming what Select does, and a hint line at the bottom.
`ui_library.c` uses it for opening (shown only after 300 ms), building, an error (with "Try again"), the Update Library result ("Done") and an empty list ("No artists yet", "Update Library"). "Library needs a writable card" is also a toast.
**Determinate progress:** the build first counts the audio files (directory entries only, same filters as the scan; Cancel works during the count), so the bar is `files / total` and the caption reads "552 of 1,204 songs . Aurora Lane"
(folder name from the scan). **Run in background:** Menu on the build pane leaves the screen, the build carries on and the status bar's left slot shows three busy dots on every screen; a toast says how it ended
("Library updated . 1,204 songs", or "Library update stopped" if the card was wanted for something else: any other card request, e.g. Play, still ends a build, the old index stands). Opening Update Library while one runs in the background takes it over.
**Greyed songs (LB-5):** the codec at the right of a greyed two-line row (an album track already showed it in brackets); a queue that skips an unplayable track toasts "Skipped: unsupported codec (AAC)" (`player_skip_notice`).
`ui_frame_push` returns false when the stack is full; Go to album / artist then toast "Can't open: too many screens".

**Settings (ST-1, ST-2).** The right pane is a left-aligned summary: group name (Body), up to four key / value lines (Small, key ink2, value ink right-aligned) and one help line (Small, ink3). Gapless, Shuffle, Repeat, Resume and Click change in place on Select
with a haptic tick and a 60 ms flip of the value in the pane. Settings > Display is Theme, Brightness (the slider), Dim after (10 s to 5 min, then Off: that is Auto-dim), Off after, Auto-lock.

**Home (3.1).** Playing: art, title and artist (tickers), a 3 px progress bar, elapsed (pause glyph in front when paused) and remaining. Idle: the clock at 48 px and the date once the clock is set (otherwise "Nothing playing"), then "1,204 songs . 72 %"
(one Info request for the song count when the panel goes idle; "No library yet" and "Open Music to build it" when there is no index). "Now Playing" is greyed while nothing is queued (Select says "Nothing playing"); Shuffle Songs has no chevron.
The card summary line ("SDHC 32 GB") exists only with `-DNEUPOD_UI_DEBUG=ON`.

### Playlists (phase 4b, LB-7)

Music > Playlists (`ui_playlists.c`, over `playlist_service.h` on the storage task; not on the frozen Pico 2 W, where the row still says `None yet`). The list of lists: **On-The-Go, Recently added, Most played, Top rated, Unplayed**, then every `.m3u` / `.m3u8` found in `/`, `/Playlists` and `/Music`, each with its count at the right (`128+` where the count is a floor). Select opens a list as two-line rows (title, then artist or folder); Select on a song plays the whole list from it (`player_play_paths`: the queue holds whole paths, Shuffle and Repeat apply). A row the card cannot play (file missing, path that is not on the card, not a music file) is greyed with the reason on its second line and answers Select with a toast. Empty lists, loading and errors are full-pane layouts like the library's. Entries are read like a player would: `#EXTM3U` / `#EXTINF`, relative and absolute paths, Windows separators and drive letters, `file://`, byte-order mark, Latin-1 or UTF-8 (`pl_m3u.h`).
Smart lists: Recently added = the 50 newest files by modified time (one pass over the index), Most played = the 25 with most counted plays, Top rated = four stars or more, Unplayed = library songs with no counted play, in title order, the first 128. **On-The-Go**: Now Playing page 2 > **Add to On-The-Go** (toast `Added to On-The-Go`, `Already in On-The-Go`, or why not); in the list a long press of Select (0.5 s) takes the song off, and a last row **Clear On-The-Go** asks first. Files and formats: [Storage](storage.md#playlists-play-counts-and-on-the-go-design-review-lb-7). Tour `--tour-playlists` (ctest `playlists_tour`, `hires_playlists_tour`; snapshots `pl-*`).

## Not yet drawn

- Hold-switch lock overlay, charging screen, USB mass-storage ("Connected") screen.
- Podcasts and Photos browsers; the search screen.

## Hold

Decision: [ADR-0020](adr/0020-hold.md). Hold is the iPod's lock switch, on every platform. While it is on, the wheel and every button do nothing:
`ui_frame_input()` swallows them before any screen sees them (so there is no click, no Play/Pause, no scroll), and each one only restarts the idle clock and
shows the lock pill ("Locked · hold Power 2 s to unlock"). Headset buttons (AVRCP) keep working; the Y2's side volume keys are locked with the rest. A lock icon sits in the status bar's right-hand group while Hold is on.

| What | How |
| --- | --- |
| Auto-lock | the screen goes fully off (Screen off after, or a Power tap) and Settings > Display > Auto-lock is On |
| Gesture | **Power held 2 s** toggles Hold: from 0.4 s a 3 px accent line grows under the status bar and a dark pill at the bottom says "Keep holding to lock · 10 s = off" (or "... to unlock"; letting go early does nothing), at 2 s the pill says "Locked" / "Unlocked" and a haptic click plays |
| Power-off | still held: at 5 s the line turns red and grows again while the pill counts "Release to cancel · shutting down in 5 ... 1" (letting go cancels), at 10 s the power-off is requested (Y2; the Pico rig cannot, and shows no countdown) |
| Tap | **Power tapped** (under 0.4 s) turns the screen off now; the next input wakes it (and is lost) |
| Switch | the iPod 6G donor board's slide switch (`hal_input_hold_switch()`), same state; none on the Y2 or the Pico rig |
| Console / simulator | no Power key on the Pico rig: the console key `l` toggles Hold. Simulator: **F1** is Power (hold it) |

The reminder ("Locked · hold Power 2 s to unlock"), the confirmations and the countdown are the lock pill and the Power-hold line of the design system (`ui_strips.c`; HL-1): nothing is dimmed, blended or covered except the pill itself. They are drawn at both UI profiles and covered by
the layout audit (`hold_tour`, `hires_hold_tour`: ring, locked, swallowed keys, countdown, power-off, switch, screen off, wake, unlock, the Display rows).

## Licences

Settings > About > **Licences** lists the third-party parts of the firmware and their licences (`app/ui/ui_licence_data.c`): the neuPod licences, FreeRTOS, LVGL,
Montserrat, Lucide, Font Awesome, FatFs, TJpgDec, dr_flac / dr_mp3, Apple Lossless, mbed TLS, BTstack (with the non-commercial note), the Android SBC codec and, on the
RP2350 build only, lwIP, cyw43-driver, the SD card driver and the Pico SDK. Select on a row shows the licence as a headline and a short plain notice (no
scrolling: at most 400 characters). The table is the on-device copy of the one in [LICENSE.md](https://github.com/StuartF303/neuPod/blob/main/LICENSE.md); `ctest -R licences`
fails if a component is listed on the device but not named, with its licence, in LICENSE.md. Adding a dependency means adding it to both.
