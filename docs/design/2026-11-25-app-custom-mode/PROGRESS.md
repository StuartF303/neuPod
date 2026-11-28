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

- [ ] Board assets, `boards.json`, `build_board` tool (frost copy, `frostDark`, `frostLight`), placeholders in
- [ ] The nine layers; cap; wordmark badge; frost under the wheel and the centre
- [ ] Anchor on the wheel's centre at 375x667, 393x852 and 440x956
- [ ] No picture decoded when every part is solid
- [ ] Asset test on the frost pair
- [ ] Goldens: Sea glass, Clear case at three sizes, see-through wheel states

## 5. The mode setting

- [x] `StyleChoice.mode` is `ModeSetting`; Light, Dark and Auto unchanged (#176, 2026-11-28)
- [x] `customLooksProvider` with the three starters, behind a store interface (#176, 2026-11-28)
- [x] Custom: screen mode from the look, paint from the look; falls back to Auto with no look (#176, 2026-11-28)
- [x] Looks persisted: a small JSON settings store (`settings/settings.json` beside `stats/`, atomic write, tolerant read, in the Android backup rules) (#176, 2026-11-28)

## 6. Settings rows and the lists

- [ ] Mode cycles Light, Dark, Auto, Custom; Custom look row
- [ ] Looks list: tick, preview at rest, `New look`, greyed at six
- [ ] A look: seven rows; Screen cycles in place
- [ ] Name by the wheel text entry, 16 characters
- [ ] Reset to default and Delete look, each behind a panel
- [ ] Board picker with its preview pane
- [ ] Goldens in Classic Light and Modern Dark; largest tier for the two lists
- [ ] Screen-reader labels

## 7. The part editor

- [ ] Layout: part tabs, colour wheel, swatch and hex, four fields, legend note, hint
- [ ] Wheel steps (5, 4, 4, 5), hue wraps, ends give the end haptic
- [ ] Previous and Next move the field; centre steps the part; Menu goes back
- [ ] Live repaint and save on every change; edge pulse on a part change
- [ ] Direct touch: colour wheel, bars, fields, tabs
- [ ] Large and largest tier layouts
- [ ] Wheel semantics: field as label, value and part as value
- [ ] Goldens and the two widget tests

## 8. Accessibility

- [ ] Reduce Transparency: solid, no picture loaded, Transparency field greyed with its reason
- [ ] Increase Contrast: solid, 7:1, edges at 60 %
- [ ] Reduce Motion: no pulse, no fade
- [ ] Goldens for both settings

## 9. Finish

- [ ] Real board pictures. BLOCKED: the owner's to supply (README Open question 2)
- [ ] Release check: no placeholder board in a store build
- [ ] Device check on the iPhone and the Pixel, recorded with the date
- [ ] `docs/app/README.md`, `docs/app/PROGRESS.md`, `mobile/lib/ui/device/README.md`, `CHANGELOG.md`
- [ ] This folder's README updated where the build differs, with the reason

## Notes

- 2026-11-25: Design written. Board pictures are placeholders made by `render.py`; `boards/` is empty.
