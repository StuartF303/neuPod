#!/usr/bin/env bash
# Regenerate the UI fonts (app/ui/fonts/neupod_font_*.c): Montserrat Medium (SIL OFL 1.1) with Latin-1 and a few typographic
# code points, merged with the Font Awesome 5 symbols the UI still draws from the fonts (LV_SYMBOL_*). Same sources and flags as LVGL's own built-in
# fonts (lib/lvgl/scripts/generators/built_in_font), so ASCII glyphs render exactly as lv_font_montserrat_* did.
# Also generates the Lucide icon set (DS-6; ISC licence, a few icons derive from Feather, MIT): app/ui/fonts/neupod_icons_*.c, 1 bit per pixel.
# Needs node (npx fetches lv_font_conv, npm fetches lucide-static; both pinned below). Run from anywhere:  tools/gen_ui_fonts.sh
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
# Font Awesome code points of every LV_SYMBOL_* the UI still draws from the fonts: AUDIO (the note), OK, IMAGE, WIFI, BATTERY_3, BLUETOOTH and
# SD_CARD (the boot splash's icon row, the Wi-Fi list's tick and the headset note). Everything else (chevron, play, pause, shuffle, repeat, folder, file,
# lock, battery, power ...) is a Lucide icon or a drawn shape now (DS-6, docs/ui.md "Icons"). Keep ui_text_has_glyph() (app/ui/ui_logic.c) in step.
SYMS="61441,61445,61452,61502,61931,62017,62099,63426"

# Lucide icons (https://lucide.dev), as the font the Lucide project publishes: the 14 glyphs of the design review plus lock-open. The code points are
# the Lucide font's own (UI_ICON_* in app/ui/ui_icons.h). lucide-static is pinned by version and by the tarball's integrity hash.
LUCIDE_VER="1.72.0"
LUCIDE_INTEGRITY="sha512-QhdZ60O7Zcs/e8gw40uMfEn055xZTHBZoNi0xb+/1R2dTd0v4FXS46VL6rKSwC+V92tLBiqsAbCcijy7Wy4j4w=="
# chevron-right play pause skip-back skip-forward shuffle repeat repeat-1 volume-2 bluetooth wifi lock folder file lock-open
ICONS="0xe06f,0xe13c,0xe12e,0xe15f,0xe160,0xe15e,0xe146,0xe1fd,0xe1ab,0xe05c,0xe1ae,0xe10b,0xe0d7,0xe0c0,0xe10c"

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

gen 12 15 3 "$TEXT" "$SYMS"      # Small, 320x240 profile: secondary lines, times, pills, hints (DS-2)
gen 14 16 3 "$TEXT" "$SYMS"
gen 16 18 3 "$TEXT" "$SYMS"      # Small, 480x360 profile; compiled for hosted builds only (NEUPOD_UI_HIRES)
gen 20 22 4 "$TEXT" "$SYMS"
gen 28 30 5 "$TEXT" "$SYMS"      # 480x360 profile; compiled for hosted builds only (NEUPOD_UI_HIRES)
gen 48 52 9 "0x20-0x7E" "61441"  # splash wordmark and the art-less note: ASCII and the note symbol only

# ---- Lucide icons: 1 bpp (the review's "1-bit alpha bitmaps"); 16 and 24 px for 320x240, 24 and 36 px for 480x360 ----
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
(cd "$TMP" && npm pack "lucide-static@$LUCIDE_VER" --silent >/dev/null)
GOT="sha512-$(openssl dgst -sha512 -binary "$TMP/lucide-static-$LUCIDE_VER.tgz" | base64 -w0)"
[[ "$GOT" == "$LUCIDE_INTEGRITY" ]] || { echo "lucide-static $LUCIDE_VER: integrity mismatch ($GOT)" >&2; exit 1; }
tar -xzf "$TMP/lucide-static-$LUCIDE_VER.tgz" -C "$TMP" package/font/lucide.ttf package/LICENSE
cp "$TMP/package/LICENSE" "$OUT/LICENSE-Lucide.txt"
for sz in 16 24 36; do
    (cd "$TMP/package/font" && npx --yes lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 1 --size "$sz" --lv-font-name "neupod_icons_$sz" \
        --font lucide.ttf -r "$ICONS" --format lvgl -o "$TMP/neupod_icons_$sz.c")
    # keep the machine-specific temp path out of the recorded command line
    sed "s| -o [^ ]*neupod_icons_$sz.c| -o app/ui/fonts/neupod_icons_$sz.c|; s|--font lucide.ttf|--font lucide-static@$LUCIDE_VER/font/lucide.ttf|" \
        "$TMP/neupod_icons_$sz.c" > "$OUT/neupod_icons_$sz.c"
done
