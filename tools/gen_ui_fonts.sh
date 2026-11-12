#!/usr/bin/env bash
# Regenerate the UI fonts (app/ui/fonts/neupod_font_*.c): Montserrat Medium (SIL OFL 1.1) with Latin-1 and a few typographic
# code points, merged with the Font Awesome 5 symbols the UI draws (LV_SYMBOL_*). Same sources and flags as LVGL's own built-in
# fonts (lib/lvgl/scripts/generators/built_in_font), so ASCII glyphs render exactly as lv_font_montserrat_* did.
# Needs node (npx fetches lv_font_conv, pinned below). Run from anywhere:  tools/gen_ui_fonts.sh
# Keep the code point lists in step with the ranges in ui_text_display() (app/ui/ui_logic.c): the filter keeps exactly what the fonts have.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/lib/lvgl/scripts/generators/built_in_font"
OUT="$ROOT/app/ui/fonts"
[[ -f "$SRC/Montserrat-Medium.ttf" ]] || { echo "LVGL submodule missing: git submodule update --init lib/lvgl" >&2; exit 1; }

cd "$SRC" # the generated header records the command line: keep it free of machine-specific paths
REL="$(realpath --relative-to="$SRC" "$OUT")"

# ASCII, Latin-1 (U+00A0..U+00FF without the soft hyphen U+00AD, which must not draw), bullet, en/em dash, curly quotes, ellipsis, euro.
TEXT="0x20-0x7E,0xA0-0xAC,0xAE-0xFF,0x2022,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2026,0x20AC"
# Font Awesome code points of every LV_SYMBOL_* the UI uses: AUDIO OK IMAGE PLAY PAUSE RIGHT SHUFFLE LOOP DIRECTORY CHARGE FILE WIFI
# BATTERY_FULL/3/2/1/EMPTY BLUETOOTH SD_CARD.
SYMS="61441,61452,61502,61515,61516,61524,61556,61561,61563,61671,61787,61931,62016,62017,62018,62019,62020,62099,63426"

# lv_font_conv sizes the line box to the tallest glyph, so adding accented capitals would make every line 2 to 5 px taller and move the whole
# layout (ADR-0018 pins the metrics to LVGL's built-in Montserrat). The generated line_height / base_line are therefore set back to the
# built-in values: an accent on a capital (A-ring, A-diaeresis) may reach 1 to 3 px above the line box, which the rows have room for.
gen() { # size line-height base-line text-range symbol-range
    npx --yes lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size "$1" --lv-font-name "neupod_font_$1" \
        --font Montserrat-Medium.ttf -r "$4" \
        --font FontAwesome5-Solid+Brands+Regular.woff -r "$5" \
        --format lvgl --force-fast-kern-format -o "$REL/neupod_font_$1.c"
    sed -i "s/[.]line_height = [0-9]*,/.line_height = $2,/; s/[.]base_line = [0-9]*,/.base_line = $3,/" "$REL/neupod_font_$1.c"
}

gen 14 16 3 "$TEXT" "$SYMS"
gen 20 22 4 "$TEXT" "$SYMS"
gen 28 30 5 "$TEXT" "$SYMS"      # 480x360 profile; compiled for hosted builds only (NEUPOD_UI_HIRES)
gen 48 52 9 "0x20-0x7E" "61441"  # splash wordmark and the art-less note: ASCII and the note symbol only
