# Progress: icon set (26 November 2026)

Checklist for the [implementation brief](IMPLEMENTATION-BRIEF.md). Tick an item in the PR that merges it.

## Owner

- [x] Choose the app icon direction: B, Turning wheel (owner, 26 Nov 2026)
- [x] Answer open questions 2 to 5: the README's recommendations (owner, 27 Nov 2026)

## Firmware

- [x] `tools/build_icon_extra_font.py`: the three new glyphs to `neupod-icons-extra.ttf` at U+EF00 to U+EF02, pinned tools (this PR, 2026-11-27)
- [x] `tools/gen_ui_fonts.sh`: ten Lucide code points added to `ICONS` (and headphones, which the output slot needs and the font lacked); the extra font merged into the icon fonts (this PR, 2026-11-27)
- [x] Regenerate `app/ui/fonts/neupod_icons_{16,24,30,36,45}.c`; compare with `mockups/glyphs-1bit-*.png` (this PR, 2026-11-27)
- [x] `app/ui/ui_icons.h`: the 13 defines (and `UI_ICON_HEADPHONES`) (this PR, 2026-11-27)
- [x] Status bar: scanning, opening, can't play and recording in the play-state slot. Opening and recording are hooks only (`ui_status_set_activity()`): the firmware streams and records nothing yet (this PR, 2026-11-27)
- [x] Status bar: right zone order busy, Wi-Fi, Bluetooth, output, lock, battery; `JACK_PLUG` and `SPEAKER` outputs. `SPEAKER` is a hook only (`ui_status_set_external_speaker()`): no network speaker output yet (this PR, 2026-11-27)
- [x] Bluetooth: `bluetooth-connected` + active colour when connected; plain + dim when saved (this PR, 2026-11-27)
- [x] Theme tokens: Classic Light and Modern values from the README's contrast table, RGB565-rounded and re-checked. Classic and Modern are not built yet (UI styles phase 1): the values went into today's Light and Dark themes, where the UI styles work will find them (this PR, 2026-11-27)
- [x] Ratings: lit and unlit stars on Now Playing page 2 and in rated list rows (Playlists > Top rated); `UI_SYMBOL_STAR` and 61445 removed (this PR, 2026-11-27)
- [x] Greyed can't-play rows show `CANT_PLAY` before the reason (this PR, 2026-11-27)
- [x] Host test: every `UI_ICON_*` has a glyph at every size (this PR, 2026-11-27)
- [x] Snapshot tours updated at 320x240 and 600x450 (the design tour's `ds-status-*`, also 480x360) (this PR, 2026-11-27)
- [x] `LICENSE.md` Lucide row and `app/ui/ui_licence_data.c` updated; `tests/test_licences.c` passes (this PR, 2026-11-27)

## App

- [ ] SVGs copied to `mobile/assets/icons/lucide/` and `mobile/assets/icons/neupod/`; path data in `neupod_glyphs.dart`
- [ ] Painter honours `fill="currentColor"` (record dot, lit star)
- [ ] Same glyph placement as the firmware; download, downloading and speaker in the app-only places
- [ ] `mobile/tool/export_app_icon.sh` and the exported PNGs
- [ ] `flutter_launcher_icons` configured and run; `AppIcon.appiconset` and `mipmap-*` committed
- [ ] Checked on an iPhone (light, dark, tinted) and a Pixel (themed icons on and off)
- [ ] Licences screen in the app lists the Lucide additions
- [ ] Backlog AP-2 closed; app PROGRESS release item ticked

## Later

- [ ] Re-check every glyph on Classic Dark and Modern Light once they are designed (UI styles open question 1)
