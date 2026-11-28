# Progress: Custom mode for the device view (AP-1)

The checklist for [IMPLEMENTATION-BRIEF.md](IMPLEMENTATION-BRIEF.md). Tick an item when its PR merges and add the PR
number and date: `- [x] item (#123, 2026-12-01)`. Mark a blocked item `BLOCKED: reason` and leave it unticked.

Every item means: unit and golden tests pass, `flutter analyze` is clean, and no golden of Light or Dark mode changed.

## 1. The model and the rule

- [x] `PartPaint`, `CustomLook`, `CustomLooks`, `ModeSetting`; JSON that round-trips and clamps (#176, 2026-11-28)
- [x] `hsvToRgb` and `mix` in `colour_math.dart` (#176, 2026-11-28)
- [x] `paintPart`: ink, flat, transparency given back; matches the brief's check values (#176, 2026-11-28)
- [x] Walk test: every colour on the grid reaches 4.5:1, solid and see-through (#176, 2026-11-28)
- [x] Increase Contrast function: every colour on the grid reaches 7:1 (#176, 2026-11-28)

## 2. One paint type for the device

- [x] `DevicePaint`; silver and black map onto it; painters read it only (#176, 2026-11-28)
- [x] Every existing device and wheel golden byte-identical (#176, 2026-11-28)

## 3. Custom paint, solid

- [x] Body, wheel and centre in custom colours with their gradients (#176, 2026-11-28)
- [x] Legends, edge, groove, touch and press tints in ink (#176, 2026-11-28)
- [x] Frame outer edge; phone status-bar style from the body's top colour (#176, 2026-11-28)
- [x] Goldens: Harbour, Chalk, Flare, default light, default dark; wheel states (#176, 2026-11-28)

## 4. Transparency and the board

- [x] Board assets, `boards.json`, `build_board` tool (frost copy, `frostDark`, `frostLight`), placeholders in (#178, 2026-11-28)
- [x] The nine layers; cap; wordmark badge; frost under the wheel and the centre (#178, 2026-11-28)
- [x] Anchor on the wheel's centre at 375x667, 393x852 and 440x956 (#178, 2026-11-28)
- [x] No picture decoded when every part is solid (#178, 2026-11-28)
- [x] Asset test on the frost pair (#178, 2026-11-28)
- [x] Goldens: Sea glass, Clear case at three sizes, see-through wheel states (#178, 2026-11-28)

## 5. The mode setting

- [x] `StyleChoice.mode` is `ModeSetting`; Light, Dark and Auto unchanged (#176, 2026-11-28)
- [x] `customLooksProvider` with the three starters, behind a store interface (#176, 2026-11-28)
- [x] Custom: screen mode from the look, paint from the look; falls back to Auto with no look (#176, 2026-11-28)
- [x] Looks persisted: a small JSON settings store (`settings/settings.json` beside `stats/`, atomic write, tolerant read, in the Android backup rules) (#176, 2026-11-28)

## 6. Settings rows and the lists

- [x] Mode cycles Light, Dark, Auto, Custom; Custom look row (#177, 2026-11-28)
- [x] Looks list: tick, preview at rest, `New look`, greyed at six (#177, 2026-11-28)
- [x] A look: seven rows; Screen cycles in place (#177, 2026-11-28)
- [x] Name by the wheel text entry, 16 characters (#177, 2026-11-28)
- [x] Reset to default and Delete look, each behind a panel (#177, 2026-11-28)
- [x] Board picker with its preview pane (#177, 2026-11-28)
- [x] Goldens in Classic Light and Modern Dark; largest tier for the two lists (#177, 2026-11-28)
- [x] Screen-reader labels (#177, 2026-11-28)

## 7. The part editor

- [x] Layout: part tabs, colour wheel, swatch and hex, four fields, legend note, hint (#177, 2026-11-28)
- [x] Wheel steps (5, 4, 4, 5), hue wraps, ends give the end haptic (#177, 2026-11-28)
- [x] Previous and Next move the field; centre steps the part; Menu goes back (#177, 2026-11-28)
- [x] Live repaint and save on every change; edge pulse on a part change (#177, 2026-11-28)
- [x] Direct touch: colour wheel, bars, fields, tabs (#177, 2026-11-28)
- [x] Large and largest tier layouts (#177, 2026-11-28)
- [x] Wheel semantics: field as label, value and part as value (#177, 2026-11-28)
- [x] Goldens and the two widget tests (#177, 2026-11-28)

## 8. Accessibility

- [x] Reduce Transparency: solid, no picture loaded, Transparency field greyed with its reason (#177, #178, 2026-11-28)
- [x] Increase Contrast: solid, 7:1, edges at 60 % (#177, 2026-11-28)
- [x] Reduce Motion: no pulse, no fade (#177, 2026-11-28)
- [ ] Goldens for both settings

## 9. Finish

- [ ] Real board pictures. BLOCKED: the owner's to supply (README Open question 2)
- [x] Release check: no placeholder board in a store build (#178, 2026-11-28)
- [ ] Device check on the iPhone and the Pixel, recorded with the date
- [ ] `docs/app/README.md`, `docs/app/PROGRESS.md`, `mobile/lib/ui/device/README.md`, `CHANGELOG.md`
- [ ] This folder's README updated where the build differs, with the reason

## Notes

- 2026-11-25: Design written. Board pictures are placeholders made by `render.py`; `boards/` is empty.
- 2026-11-28 (stage 2, #178): Board pictures are the render.py placeholders, `"placeholder": true` in `boards.json`; `dart run tool/check_release_boards.dart` (and the app workflow's `release_check` input) fail while any board has the flag. Reduce Transparency is read through a hand-written method channel `neupod_device/accessibility` (iOS `UIAccessibility`; Android has no such setting and answers false, so Custom mode keeps its transparency there). Increase Contrast comes from `MediaQuery.highContrast`; its item and golden are still open. The frost pair is measured from the saved frost copy by `tool/build_board.dart`; the white-wheel asset test treats black to white as the worst pair (it keeps exactly 50 %). The wordmark badge uses line height 1.2 as in render.py.
- 2026-11-28 (#177): Section 8's Reduce Transparency and its golden are stage 2's. The Increase Contrast golden
  (`device_393x852_custom_seaglass_increase_contrast`) is in; the Reduce Transparency one is still to do.
  Flutter has no Reduce Transparency flag: `reduceTransparencyProvider` (`app/accessibility_providers.dart`) now follows
  the platform read (#178); the editor greys the Transparency field from it.
- 2026-11-28 (#177): Harbour's saturation 62 and the wheel's brightness 94 are between two 4 % detents, so the wheel alone
  reaches the nearest detent (60 or 64, 92 or 96); the widget test asserts hue and brightness exact and saturation within 2.
  A value off the grid first lands on the grid (a detent), so 218 degrees reaches 205 in three detents.
- 2026-11-28 (#177): `domain/style/board_list.dart` is now generated from `boards.json` by `tool/build_board.dart` (#178). The board picker's pane is a plain tile until there are pictures.
