# UI fonts

`neupod_font_14.c`, `_20.c`, `_28.c`, `_48.c` are generated files. Do not edit them; run `tools/gen_ui_fonts.sh` (needs node; it fetches
`lv_font_conv` 1.5.3 through npx) to rebuild them.

| Font | Sizes | Glyphs |
|------|-------|--------|
| Montserrat Medium (SIL OFL 1.1, `lib/lvgl/scripts/generators/built_in_font/Montserrat-Medium.ttf`) | 14, 20, 28 | U+0020-007E, U+00A0-00FF (Latin-1 without the soft hyphen U+00AD), U+2022, U+2013, U+2014, U+2018, U+2019, U+201C, U+201D, U+2026, U+20AC |
| Montserrat Medium | 48 | U+0020-007E |
| Font Awesome 5 Free Solid (`FontAwesome5-Solid+Brands+Regular.woff` beside it; icons CC BY 4.0, font files SIL OFL 1.1) | all four | the 19 `LV_SYMBOL_*` the UI uses plus power, lock and unlock (`UI_SYM_*`, Hold; 48: only `LV_SYMBOL_AUDIO`) |

4 bpp, uncompressed, fast-kerning format: the same options as LVGL's built-in fonts, so ASCII text renders pixel-for-pixel as before. The
line height and baseline are set back to the built-in values (the generator would grow every line for the accented capitals), see `docs/ui.md`. The
size-28 file is compiled for hosted builds only (`NEUPOD_UI_HIRES`).

`ui_text_display()` in `app/ui/ui_logic.c` keeps exactly the code points listed above; change both together. A new `LV_SYMBOL_*` in the UI needs
its code point added to `SYMS` in the script.

Licence: `OFL-Montserrat.txt` (Montserrat Project Authors). The Font Awesome notice is recorded in `LICENSE.md`.
