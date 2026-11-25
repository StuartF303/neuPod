# Implementation brief: Custom mode for the device view (AP-1)

You are building the design in [README.md](README.md) in this folder, in the Flutter app under `mobile/`. This brief
stands alone: everything you need to build is here or in the mock-ups. Read the README for the reasons, look at every
PNG in `mockups/`, and open the matching `.html` when you need an exact size or colour. `render.py` in this folder is
the reference for the colour arithmetic; where this brief and `render.py` disagree, stop and ask.

You work through [PROGRESS.md](PROGRESS.md) one item at a time and tick each item when its PR merges. It is the only
record of where you are.

**Not yours to decide:** anything under the README's Open questions. Build what was drawn and leave the question open.

## Before the first step

1. Read `docs/app/TEAM-BRIEF.md` and `CONTRIBUTING.md`; their rules win over this brief.
2. Read `mobile/lib/ui/device/README.md`, `device_finish.dart`, `wheel_painter.dart`, `device_view.dart`,
   `mobile/lib/ui/screens/shell/shell_style.dart`, `mobile/lib/ui/screens/browse/settings_screen.dart` and
   `mobile/lib/domain/style/colour_math.dart`.
3. Run the app's tests. Everything passes before you change anything; if not, stop and report.
4. This feature changes paint, not layout. `device_geometry.dart`, the hit zones and the wheel's numbers are not
   touched. A golden of Light or Dark mode that changes is a bug.

## The data model

Pure Dart, in `mobile/lib/domain/style/`. No Flutter imports.

```dart
enum LookPart { body, wheel, centre }
enum LookScreen { light, dark, auto }
enum ModeSetting { light, dark, auto, custom }

/// Whole numbers, as the editor sets them. RGB is computed, never stored.
final class PartPaint {
  final int hue;          // 0..359
  final int saturation;   // 0..100
  final int brightness;   // 0..100
  final int transparency; // 0..85 body, 0..60 wheel and centre
}

final class CustomLook {
  final String id;        // made once, never reused
  final String name;      // 1..16 characters
  final PartPaint body, wheel, centre;
  final String board;     // 'main-top' | 'main-bottom' | 'copper'; unknown falls back to 'main-top'
  final LookScreen screen;
}

final class CustomLooks {
  final List<CustomLook> looks; // 0..6, in list order
  final String? lookInUse;      // an id in looks, or null
}
```

JSON is the README's section 10 example, field for field. Reading clamps every number into its range and drops a
seventh look and beyond; it never throws on a bad value.

Not stored, always computed: RGB, gradients, ink, the flat flag, the transparency kept.

## The contrast rule, as numbers

`paintPart(PartPaint p, LookPart part, {List<int>? grounds, double need = 4.5})` returns the colours to draw with.
It must give the same results as `paint()` in `render.py`.

| Constant | Value |
| --- | --- |
| Ink | `0x000000` or `0xffffff` only |
| Target | 4.5; 7.0 with Increase Contrast |
| Switch for one flat colour | relative luminance 0.179: black at or above, white below |
| Gradient ends, mixed toward white (top) and black (bottom) | body 0.10 (stops at 0, 0.45 for the colour itself, 1); wheel 0.06; centre 0.06 |
| Most transparency | body 85; wheel 60; centre 60 |
| Step when giving back transparency | 5 |
| Mix | per channel, in sRGB bytes: `round(a + (b - a) * t)` |
| Contrast | `contrastRatio` in `colour_math.dart` (WCAG 2) |

Steps:

1. `t = min(p.transparency, most)`; `flat = false`; `base = hsv(p)`.
2. `stops = flat ? [base, base, base] : [mix(base, white, amp), base, mix(base, black, amp)]`.
3. `under = stops`, or when `t > 0` and `grounds` is given, every stop composited over every ground:
   `mix(stop, ground, t / 100)`.
4. `black = min contrast(0x000000, u)` over `under`; `white` likewise. `ink = black >= white ? black : white`;
   `worst = max(black, white)`.
5. If `worst < need` and not `flat`: `flat = true`, go to 2.
6. While `worst < need` and `t > 0`: `t -= 5`, go to 2.
7. Return `base`, `stops`, `ink`, `worst`, `t` (the transparency kept) and `flat`.

`grounds` is given for the **wheel only**: the board's `frostDark` and `frostLight`. The body and the centre pass
none.

Other inks: the phone's status bar is black when `relativeLuminance(body.stops[0]) >= 0.179`, else white. The
wordmark uses the body's ink. Edges: ink at 30 % (60 % with Increase Contrast); groove ink at 22 %; frame outer edge
body ink at 16 %. Tints, with `t` the transparency kept as a fraction: finger on the ring `0.07 + 0.06 t` for black
ink or `0.08 + 0.06 t` for white; pressed `0.13 + 0.10 t` or `0.14 + 0.10 t`.

Increase Contrast: `transparency = 0`, then for `d = 0, 1, 2, ...` try `brightness - d` then `brightness + d` (inside
0..100) and take the first whose `paintPart(..., need: 7.0).worst >= 7.0`.

Check values (from `python3 render.py --numbers`; base, ink, worst, transparency kept, flat; wheel over `main-top`):

| Look | Body | Wheel | Centre |
| --- | --- | --- | --- |
| Harbour | `#2b5370` white 6.28, 0, no | `#f0e9dd` black 15.28, 0, no | `#23445c` white 8.62, 0, no |
| Sea glass | `#74b2a3` black 7.04, 55, no | `#dcf5ef` black 16.06, 0, no | `#5c998a` black 5.71, 0, no |
| Clear case | `#dde6eb` black 13.30, 85, no | `#ebf2f5` black 4.86, 60, **flat** | `#d8e1e6` black 13.95, 60, no |
| Flare (the README's awkward case) | `#db1a61` white 4.85, 0, **flat** | `#4c172b` white 12.16, 0, no | `#f26118` black 5.77, 0, no |

## Assets

Per board, under `mobile/assets/boards/` and listed in `pubspec.yaml`:

| File | What |
| --- | --- |
| `<id>.webp` | 1200 x 3120 px, opaque, sRGB. Pixel (600, 2040) is the anchor |
| `<id>.frost.webp` | the same picture, Gaussian blur radius 36 px, saved at 300 x 780 px |
| an entry in `boards.json` | `id`, `name`, `ground` (`#rrggbb`), `frostDark`, `frostLight` |

`mobile/tool/build_board.dart` (or a script beside it) makes the frost copy and measures `frostDark` and `frostLight`:
the darkest and lightest pixel, by relative luminance, of the frost copy inside radius 384 px (at full size) of the
anchor.

**The pictures do not exist yet.** Build with placeholders: export the three from this folder's `render.py`
(`board_svg`) and keep their `PLACEHOLDER · NOT A NEUPOD BOARD` lettering. Ids `main-top`, `main-bottom`, `copper`;
names `Main board, top`, `Main board, bottom`, `Copper only`. Never add a picture of a board that is not neuPod's
own. Real pictures are the owner's to supply (README Open question 2); a release check in step 9 keeps placeholders
out of a store build.

Drawing a picture: scale `W / 1200`; put the anchor on the wheel's centre; paint `ground` wherever the picture does
not reach.

## The steps

Each step is one PR unless it says otherwise. Every step: unit and golden tests pass, `flutter analyze` clean, no
existing golden changes.

### 1. The model and the rule

Files: `mobile/lib/domain/style/custom_look.dart`, `custom_paint.dart` (new); `colour_math.dart` (add `hsvToRgb`,
`mix`); `mobile/test/domain/style/custom_look_test.dart`, `custom_paint_test.dart` (new).

Accept when: the JSON round-trips and clamps; `paintPart` matches the check values above exactly; a test walks hue in
15s, saturation in 10s and brightness in 5s for all three parts solid, and the wheel at 60 % over each board's frost
pair, and every result has `worst >= 4.5`; the Increase Contrast function returns `worst >= 7.0` for the same walk.

### 2. One paint type for the device

Files: `mobile/lib/ui/device/device_finish.dart`, `wheel_painter.dart`, `device_view.dart`, `device_options.dart`.

`DeviceFinish` is an enum of two finishes. Add a value class (`DevicePaint`) holding what the painters read: body
stops and opacity, ring and centre stops and opacities, edge, groove, legend ink, wordmark ink, press and touch
tints, status-bar ink, and whether there is a cap, a badge and a board. The two finishes map onto it with their
table values unchanged. The painters read `DevicePaint` only.

Accept when: every existing device and wheel golden is byte-identical.

### 3. Custom paint, solid

Files: `device_view.dart`, `wheel_painter.dart`, new `mobile/lib/ui/device/custom_device_paint.dart` (a look, a
board and the accessibility flags in; a `DevicePaint` out).

Solid parts only. Legends, edge, groove and tints in ink. The frame's outer 1 px edge. Status-bar style from the
body's top colour.

Goldens: `device_393x852_custom_harbour`, `_custom_chalk`, `_custom_flare`, `_custom_default_light`,
`_custom_default_dark`; `wheel_states_custom_harbour` (idle, ring, menu, centre, disabled).

Accept when: the goldens agree with `look-opaque.png`, `awkward-pale-classic-light.png`,
`awkward-saturated-modern-dark.png`, `default-light.png`, `default-dark.png` in structure, colour and ink.

### 4. Transparency and the board

Files: `device_view.dart`, `wheel_painter.dart`, new `mobile/lib/ui/device/board_picture.dart`,
`mobile/assets/boards/*`, `pubspec.yaml`, `mobile/tool/build_board.dart`.

The README's nine layers, in order. The cap. The wordmark's badge. The frost copy clipped to the wheel and to the
centre. Pictures decoded once and cached; none decoded when every part is solid.

Goldens: `device_393x852_custom_seaglass`, `_custom_clear`, and Clear case at 375x667 and 440x956 (the anchor);
`wheel_states_custom_clear`.

Accept when: the anchor sits on the wheel's centre at all three sizes; the goldens agree with
`look-part-transparent.png`, `look-clear-case.png` and `wheel-states-see-through.png`; an asset test fails a board
whose frost pair makes a white wheel (0, 0, 100) asked for 60 % keep less than 50 %.

### 5. The mode setting

Files: `mobile/lib/ui/screens/shell/shell_style.dart`, `mobile/lib/app/neupod_app.dart`, `device_options.dart`, new
`mobile/lib/app/custom_looks_provider.dart`.

`StyleChoice.mode` becomes `ModeSetting`. In Custom the screen's mode is the look's `screen` (Auto follows the
phone) and the device's paint is the look's. A `customLooksProvider` holds `CustomLooks`, seeded with the three
starters (README section 7), behind a small store interface. Settings are not persisted anywhere in the app yet
(`docs/app/PROGRESS.md`, phase 6): keep looks in memory behind that interface with the JSON codec ready, and wire
the store when the Settings item lands. Do not invent a storage mechanism here.

Accept when: Light, Dark and Auto behave exactly as before; Custom with no look in use falls back to Auto.

### 6. Settings rows and the lists

Files: `settings_screen.dart`; new `mobile/lib/ui/screens/custom/looks_screen.dart`, `look_screen.dart`,
`reset_delete_screen.dart`, `board_screen.dart`; `mobile/lib/ui/screens/README.md`.

Mode cycles Light, Dark, Auto, Custom. The Custom look row. The looks list with tick, preview after 250 ms at rest
and `New look` (greyed with `6 of 6` at six). A look's seven rows. Name through the existing wheel text entry, 16
characters. Reset and Delete, each behind the centred panel. The board picker with its preview pane. No scroll bar
is drawn on these screens.

Goldens, Classic Light and Modern Dark, standard tier: `settings_custom`, `custom_looks`, `custom_look`,
`custom_board`, `custom_reset_delete`; Classic Light at the largest tier for `custom_looks` and `custom_look`.

Accept when: they agree with `settings-mode-rows.png`, `looks-list.png`, `look.png` and `board-picker.png`; nothing
scrolls at the standard tier; every row has its screen-reader label as README section 8.

### 7. The part editor

Files: new `mobile/lib/ui/screens/custom/part_picker_screen.dart`, `part_picker_view.dart`, `colour_wheel.dart`.

The layout of README section 2 at 600x450. Wheel: Hue 5 a detent, wrapping; Saturation and Brightness 4; Transparency
5; ends give `endReached()`. Previous and Next move the field; the centre steps the part; Menu goes back. Each
change repaints the device at once and is saved. The legend note. The part's edge pulse (not with Reduce Motion).
With direct touch on: drag on the colour wheel, drag on a bar, tap a field or a tab. The wheel's semantics: label
the field, value the number and the part.

Goldens: `custom_part_body`, `custom_part_wheel_clear`, `custom_part_centre` in Classic Light and Modern Dark;
`custom_part_body` at the large and largest tiers in Classic Light.

Accept when: they agree with `editor-body.png`, `editor-wheel.png`, `editor-centre.png` and
`editor-largest-text.png`; a widget test makes Harbour from the default with the wheel bus alone; a widget test
shows `Held at` when the rule gives transparency back.

### 8. Accessibility

Files: `custom_device_paint.dart`, `part_picker_view.dart`, `mobile/lib/app/device_platform.dart` and the
`neupod_device` plugin if a platform flag is missing.

Reduce Transparency and Increase Contrast as README section 8. Find the current Flutter and platform APIs for both
and check them against current documentation; if Flutter does not expose Reduce Transparency, read it through the
plugin. Android: use what the system offers and say in the PR what that is.

Goldens: `device_393x852_custom_clear_reduce_transparency`, `_custom_seaglass_increase_contrast`.

Accept when: they agree with `a11y-reduce-transparency.png` and `a11y-increase-contrast.png`; no board picture is
decoded with either setting on; the stored look is unchanged by both.

### 9. Finish

Files: `docs/app/README.md`, `docs/app/PROGRESS.md`, `mobile/lib/ui/device/README.md`, `CHANGELOG.md`, the Licences
data if an asset needs a line, CI.

A release check that fails the build when `boards.json` still marks a picture as a placeholder. The device check on
the owner's iPhone and Pixel: every screen here, both styles, the three starters, each accessibility setting,
recorded with the date. This folder's README updated where the build ended up different, with the reason.

## Stop and ask the owner

- anything that depends on a README Open question;
- the rule and `render.py` disagree, or a check value cannot be matched;
- an existing golden would have to change;
- a platform has no way to read an accessibility setting;
- the same step has failed its acceptance three times.

When you stop, leave the tree clean, `PROGRESS.md` current, and say exactly what you need.

## Done

Every item in `PROGRESS.md` is ticked or marked `BLOCKED` with an owner question attached, CI is green on `main`, and
the final report lists what shipped, what is blocked and on what, and what was and was not checked on the phones.
