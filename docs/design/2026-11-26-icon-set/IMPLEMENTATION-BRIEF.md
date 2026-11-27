# Implementation brief: icon set (26 November 2026)

For the engineers building the [icon set](README.md). The README holds the decisions; this file says what to do with
them. Tick items off in [PROGRESS.md](PROGRESS.md) as they merge. Firmware first (ADR-0022: it is the reference), then
the app in the same or the next PR, or a backlog item for the gap.

## 1. Files in this folder

| Path | What | Use |
| --- | --- | --- |
| `svg/<glyph>.svg` (13) | 24x24, `stroke="currentColor"`, 2 px, round caps and joins | Source of truth for every glyph |
| `svg/app-icon-master.svg` | 1024x1024, light | iOS AppIcon, light |
| `svg/app-icon-master-dark.svg` | 1024x1024, dark | iOS AppIcon, dark |
| `svg/app-icon-master-tinted.svg` | 1024x1024, grayscale on black | iOS AppIcon, tinted |
| `svg/android-foreground.svg`, `-background.svg`, `-monochrome.svg` | 108x108 dp viewBox | Android adaptive icon layers |
| `svg/app-icon-direction-a-monogram.svg`, `-c-trace.svg` | 1024 masters | Not chosen; kept for the record |
| `render.py` | Builds the preview font and the sheets | Re-run after any SVG change |

## 2. Firmware: getting the glyphs into the icon font

Today `tools/gen_ui_fonts.sh` turns `lucide-static` 1.72.0's `lucide.ttf` into `app/ui/fonts/neupod_icons_{16,24,30,36,45}.c`
at 1 bpp, with the code points in `ICONS` and the defines in `app/ui/ui_icons.h`.

**2a. Lucide glyphs (no new step).** Add these code points to `ICONS`, in this order after the existing ones:

```
# scanning, opening, can't play, busy, speaker, download, downloading, star, bluetooth connected
0xe0bd,0xe147,0xe10a,0xe6fe,0xe0b6,0xe166,0xe0b2,0xe089,0xe176,0xe1b8
```

**2b. New glyphs (one extra step).** `jack-plug`, `record` and `star-filled` are not in Lucide. Build them into a small
TrueType font from `svg/` and merge it into the same `lv_font_conv` call, so they come out in the same `neupod_icons_*`
fonts with the same settings:

1. Add `tools/build_icon_extra_font.py` (Python, `fonttools` and `picosvg`): for each new SVG, expand strokes to
   outlines with picosvg, scale to Lucide's grid (24 units to 1000 per em, `y' = 1000 - y * 1000 / 24`, ascent 1000,
   descent 0, advance 1000), and map them to **U+EF00 `jack-plug`, U+EF01 `record`, U+EF02 `star-filled`**. Output
   `build/neupod-icons-extra.ttf` (not committed). `build_extra_font()` in this folder's `render.py` is a working
   reference: lift it.
2. In `gen_ui_fonts.sh`, call it before the icon loop and pass a second font to the same command:
   `--font lucide.ttf -r "$ICONS" --font "$EXTRA_TTF" -r 0xef00-0xef02`. Flags stay as they are (`--bpp 1 --no-compress
   --no-prefilter`, sizes 16, 24, 30, 36, 45).
3. Pin `picosvg` and `fonttools` versions in the script header, as `lv_font_conv` and `lucide-static` are pinned, so
   the generated `.c` files are reproducible. Record the SVGs' sha256 in the generated header comment.

**2c. Defines in `app/ui/ui_icons.h`** (UTF-8 of each code point):

| Define | Code point | Glyph |
| --- | --- | --- |
| `UI_ICON_FAST_FORWARD` | U+E0BD | fast-forward |
| `UI_ICON_REWIND` | U+E147 | rewind |
| `UI_ICON_OPENING` | U+E10A | loader-circle |
| `UI_ICON_CANT_PLAY` | U+E6FE | play-off |
| `UI_ICON_BUSY` | U+E0B6 | ellipsis (static places only; the status bar keeps its drawn dots) |
| `UI_ICON_SPEAKER` | U+E166 | speaker |
| `UI_ICON_DOWNLOAD` | U+E0B2 | download |
| `UI_ICON_DOWNLOADING` | U+E089 | cloud-download |
| `UI_ICON_STAR` | U+E176 | star (unlit) |
| `UI_ICON_STAR_FILLED` | U+EF02 | star-filled (lit) |
| `UI_ICON_BLUETOOTH_CONNECTED` | U+E1B8 | bluetooth-connected |
| `UI_ICON_JACK_PLUG` | U+EF00 | jack-plug |
| `UI_ICON_RECORD` | U+EF01 | record |

Once page 2 uses the new stars, remove `UI_SYMBOL_STAR` and its code point 61445 (U+F005) from `SYMS` in
`gen_ui_fonts.sh` (check nothing else draws it first), and keep `ui_text_has_glyph()` in step.

**2d. Which code draws which glyph**

| Place | Today | Change |
| --- | --- | --- |
| Status bar, play-state slot (`ui_status_*`) | play / pause, then shuffle / repeat | Add: scanning (`FAST_FORWARD` / `REWIND` while a seek key is held), `OPENING` while a stream opens, `CANT_PLAY` when the current track failed, `RECORD` while recording. Mode glyphs follow as today |
| Status bar, right zone | Wi-Fi, Bluetooth, headphones, lock, battery | Order: busy dots (drawn), Wi-Fi, Bluetooth, output, lock, battery. Bluetooth: `BLUETOOTH_CONNECTED` + active colour when connected or in use, `BLUETOOTH` + dim when saved only. Output: `HEADPHONES`, `JACK_PLUG` (Line) or `SPEAKER` (external) |
| Now Playing page 2, rating | Font Awesome star | `STAR_FILLED` in the accent token for lit, `STAR` in the line-grey token for unlit, icon font large |
| List rows with a rating | none | Same pair at the icon font's small size |
| Greyed row that can't play | text reason | `CANT_PLAY` before the reason (the reason stays) |

**2e. Theme tokens.** Glyph colours come from theme tokens like text (`ui_icon()`). New or changed values, per the
README's contrast table: Classic Light `status_dim` `#474b52`, `bt_active` `#123f8f`, `recording` `#8a1a15`,
`rating_lit` = selection blue `#1858c8`, `rating_unlit` `#737373`; Modern `status_dim` `#9a9a9a`, `recording`
`#ff7b70`, `rating_lit` = the art's light tint (the progress fill's colour), `rating_unlit` `#7c7c7c`. Flat fills stay
RGB565-exact on the LCD: round these with the existing helper and re-check 4.5:1 after rounding. Classic Dark values in
`render.py` are provisional and are **not** to be added as a theme.

## 3. App (Flutter, `mobile/`)

**3a. Glyphs.** The app draws icons from SVG path data (`lib/ui/style/icons/neupod_glyphs.dart`) with the SVGs kept in
`assets/icons/lucide/`. Copy the 13 SVGs from `svg/` into `assets/icons/` (Lucide ones into `lucide/`, the three new
ones into a new `assets/icons/neupod/` with this folder's licence note) and add their path data to `neupod_glyphs.dart`
under the same names. `star-filled` and `record` need the painter to honour `fill="currentColor"`: check
`svg_shapes.dart` does, or add it. Use them in the same places as the firmware (2d), plus:

- Download (an action) on album and song rows and menus: `download`;
- an iCloud file not yet on the phone: `cloud-download` on the row, with the reason in the row's second line;
- output picker: `speaker` for every external output that is not a Bluetooth headset. Never Apple's AirPlay glyph,
  even where iOS's route picker would offer it.

**3b. App icon, one source for both platforms.** Add `flutter_launcher_icons` (dev dependency, version pinned) and
export PNGs from the SVG masters with `rsvg-convert` in a small script, `mobile/tool/export_app_icon.sh`, run by hand
when the masters change (the PNGs are committed, the SVGs stay the source here):

```
rsvg-convert -w 1024 -h 1024 svg/app-icon-master.svg        -o mobile/assets/app_icon/icon.png
rsvg-convert -w 1024 -h 1024 svg/app-icon-master-dark.svg   -o mobile/assets/app_icon/icon_dark.png
rsvg-convert -w 1024 -h 1024 svg/app-icon-master-tinted.svg -o mobile/assets/app_icon/icon_tinted.png
rsvg-convert -w 432  -h 432  svg/android-foreground.svg     -o mobile/assets/app_icon/android_fg.png
rsvg-convert -w 432  -h 432  svg/android-background.svg     -o mobile/assets/app_icon/android_bg.png
rsvg-convert -w 432  -h 432  svg/android-monochrome.svg     -o mobile/assets/app_icon/android_mono.png
```

```yaml
flutter_launcher_icons:
  android: true
  ios: true
  image_path: assets/app_icon/icon.png
  image_path_ios_dark_transparent: assets/app_icon/icon_dark.png
  image_path_ios_tinted_grayscale: assets/app_icon/icon_tinted.png
  remove_alpha_ios: true
  adaptive_icon_foreground: assets/app_icon/android_fg.png
  adaptive_icon_background: assets/app_icon/android_bg.png
  adaptive_icon_monochrome: assets/app_icon/android_mono.png
```

Run `dart run flutter_launcher_icons` and commit the generated `AppIcon.appiconset` and `mipmap-*` resources. The iOS
dark master is opaque (black body finish); if a reviewer prefers Apple's transparent dark style, export it without its
first `<rect>` (the background) and keep `remove_alpha_ios` off for that file only.

## 4. Checks

- Snapshot tours (firmware and app) for the status bar in each state of the README's sheet, at 320x240 and 600x450.
- A host test that every `UI_ICON_*` define has a glyph in each `neupod_icons_*` font (no tofu at 16 px).
- `tests/test_licences.c` and `LICENSE.md` updated together (README, Licences).
- Compare the generated 1-bit glyphs with `mockups/glyphs-1bit-*.png`: they are made by the same `lv_font_conv` call,
  so any difference means the build step drifted.
- Phone: icon checked on a real home screen in light, dark and tinted (iOS) and with themed icons on (Android).

Do not change behaviour beyond what is listed: this brief adds glyphs and the app icon, nothing else.
