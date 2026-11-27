#!/usr/bin/env bash
# Regenerate the UI fonts (app/ui/fonts/neupod_font_*.c): Montserrat Medium (SIL OFL 1.1) with Latin-1 and a few typographic
# code points, merged with the Font Awesome 5 symbols the UI still draws from the fonts (LV_SYMBOL_*). Same sources and flags as LVGL's own built-in
# fonts (lib/lvgl/scripts/generators/built_in_font), so ASCII glyphs render exactly as lv_font_montserrat_* did.
# Also generates the Lucide icon set (DS-6; ISC licence, a few icons derive from Feather, MIT): app/ui/fonts/neupod_icons_*.c, 1 bit per pixel.
# The icon set's own glyphs (jack plug, record, lit star; docs/design/2026-11-26-icon-set) are built into a small font by tools/build_icon_extra_font.py
# and merged into the same icon fonts by the same lv_font_conv call (the generated header's "--font neupod-icons-extra.ttf" is that font).
# Needs node (npx fetches lv_font_conv, npm fetches lucide-static; both pinned below) and python3 with fonttools 4.65.0, picosvg 0.23.0 and skia-pathops 0.9.2 (pinned in
# build_icon_extra_font.py; PYTHON=... picks the interpreter, e.g. a venv's). Run from anywhere:  tools/gen_ui_fonts.sh
# Keep the code point lists in step with the ranges in ui_text_display() (app/ui/ui_logic.c): the filter keeps exactly what the fonts have.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/lib/lvgl/scripts/generators/built_in_font"
OUT="$ROOT/app/ui/fonts"
[[ -f "$SRC/Montserrat-Medium.ttf" ]] || { echo "LVGL submodule missing: git submodule update --init lib/lvgl" >&2; exit 1; }

cd "$SRC" # the generated header records the command line: keep it free of machine-specific paths
REL="$(python3 -c 'import os, sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$OUT" "$SRC")"
PYTHON="${PYTHON:-python3}"
sedi() { sed "$1" "$2" > "$2.tmp" && mv "$2.tmp" "$2"; } # in-place edit with GNU or BSD sed

# ASCII, Latin-1 (U+00A0..U+00FF without the soft hyphen U+00AD, which must not draw), bullet, en/em dash, curly quotes, ellipsis, euro.
TEXT="0x20-0x7E,0xA0-0xAC,0xAE-0xFF,0x2022,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2026,0x20AC"
# Font Awesome code points of every LV_SYMBOL_* the UI still draws from the fonts: AUDIO (the note), OK, IMAGE, WIFI, BATTERY_3, BLUETOOTH and
# SD_CARD (the Wi-Fi list's tick and the headset note; the boot splash's icon row is Lucide since 2026-11-13). Everything else (chevron, play, pause, shuffle, repeat, folder, file,
# lock, battery, power ...) is a Lucide icon or a drawn shape now (DS-6, docs/ui.md "Icons"); the rating star (61445) went with the icon set of 2026-11-26, ratings use
# the icon font's star and lit star. Keep ui_text_has_glyph() (app/ui/ui_logic.c) in step.
SYMS="61441,61452,61502,61931,62017,62099,63426"

# Lucide icons (https://lucide.dev), as the font the Lucide project publishes: the 14 glyphs of the design review plus lock-open and the boot splash's check icons. The code points are
# the Lucide font's own (UI_ICON_* in app/ui/ui_icons.h). lucide-static is pinned by version and by the tarball's integrity hash.
LUCIDE_VER="1.72.0"
LUCIDE_INTEGRITY="sha512-QhdZ60O7Zcs/e8gw40uMfEn055xZTHBZoNi0xb+/1R2dTd0v4FXS46VL6rKSwC+V92tLBiqsAbCcijy7Wy4j4w=="
# chevron-right play pause skip-back skip-forward shuffle repeat repeat-1 volume-2 bluetooth wifi lock folder file lock-open
# then the boot splash's check row (reinstated 2026-11-13; it also uses volume-2, bluetooth and wifi): monitor hard-drive battery-medium vibrate circle-dot
ICONS="0xe06f,0xe13c,0xe12e,0xe15f,0xe160,0xe15e,0xe146,0xe1fd,0xe1ab,0xe05c,0xe1ae,0xe10b,0xe0d7,0xe0c0,0xe10c,0xe11d,0xe0ed,0xe057,0xe223,0xe345"
# then the icon set (docs/design/2026-11-26-icon-set): fast-forward rewind loader-circle play-off ellipsis speaker download cloud-download star bluetooth-connected,
# and headphones (the status bar's output slot)
ICONS="$ICONS,0xe0bd,0xe147,0xe10a,0xe6fe,0xe0b6,0xe166,0xe0b2,0xe089,0xe176,0xe1b8,0xe0f1"
# The icon set's own glyphs, built by tools/build_icon_extra_font.py: jack-plug U+EF00, record U+EF01, star-filled U+EF02
EXTRA="0xef00-0xef02"

# lv_font_conv sizes the line box to the tallest glyph, so adding accented capitals would make every line 2 to 5 px taller and move the whole
# layout (ADR-0018 pins the metrics to LVGL's built-in Montserrat). The generated line_height / base_line are therefore set back to the
# built-in values: an accent on a capital (A-ring, A-diaeresis) may reach 1 to 3 px above the line box, which the rows have room for.
gen() { # size line-height base-line text-range symbol-range
    npx --yes lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size "$1" --lv-font-name "neupod_font_$1" \
        --font Montserrat-Medium.ttf -r "$4" \
        --font FontAwesome5-Solid+Brands+Regular.woff -r "$5" \
        --format lvgl --force-fast-kern-format -o "$REL/neupod_font_$1.c"
    sedi "s/[.]line_height = [0-9]*,/.line_height = $2,/; s/[.]base_line = [0-9]*,/.base_line = $3,/" "$REL/neupod_font_$1.c"
}

gen 12 15 3 "$TEXT" "$SYMS"      # Small, 320x240 profile: secondary lines, times, pills, hints (DS-2)
gen 14 16 3 "$TEXT" "$SYMS"
gen 16 18 3 "$TEXT" "$SYMS"      # Small, 480x360 profile; compiled for hosted builds only (NEUPOD_UI_HIRES)
gen 20 22 4 "$TEXT" "$SYMS"
gen 28 30 5 "$TEXT" "$SYMS"      # 480x360 profile; compiled for hosted builds only (NEUPOD_UI_HIRES)
gen 22 24 4 "$TEXT" "$SYMS"      # Small, 600x450 profile (built-in Montserrat metrics); compiled for hosted builds and NEUPOD_UI_PROFILE=600x450 only
gen 26 29 5 "$TEXT" "$SYMS"      # Body, 600x450 profile; same
gen 38 41 7 "$TEXT" "$SYMS"      # Large, 600x450 profile; same
gen 48 52 9 "0x20-0x7E" "61441"  # splash wordmark and the art-less note: ASCII and the note symbol only

# ---- Lucide icons: 1 bpp (the review's "1-bit alpha bitmaps"); 16 and 24 px for 320x240, 24 and 36 px for 480x360, 30 and 45 px for 600x450 ----
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
(cd "$TMP" && npm pack "lucide-static@$LUCIDE_VER" --silent >/dev/null)
GOT="sha512-$(openssl dgst -sha512 -binary "$TMP/lucide-static-$LUCIDE_VER.tgz" | base64 | tr -d '\n')"
[[ "$GOT" == "$LUCIDE_INTEGRITY" ]] || { echo "lucide-static $LUCIDE_VER: integrity mismatch ($GOT)" >&2; exit 1; }
tar -xzf "$TMP/lucide-static-$LUCIDE_VER.tgz" -C "$TMP" package/font/lucide.ttf package/LICENSE
cp "$TMP/package/LICENSE" "$OUT/LICENSE-Lucide.txt"
# The extra font (not committed) beside lucide.ttf, and its sources' sha256 for the generated header
"$PYTHON" "$ROOT/tools/build_icon_extra_font.py" "$TMP/package/font/neupod-icons-extra.ttf" | sed 's|^| * from |' > "$TMP/sums.txt"
for sz in 16 24 30 36 45; do
    (cd "$TMP/package/font" && npx --yes lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 1 --size "$sz" --lv-font-name "neupod_icons_$sz" \
        --font lucide.ttf -r "$ICONS" --font neupod-icons-extra.ttf -r "$EXTRA" --format lvgl -o "$TMP/neupod_icons_$sz.c")
    # keep the machine-specific temp path out of the recorded command line; name the extra font's maker and sources under it
    sed "s| -o [^ ]*neupod_icons_$sz.c| -o app/ui/fonts/neupod_icons_$sz.c|; s|--font lucide.ttf|--font lucide-static@$LUCIDE_VER/font/lucide.ttf|; /^ [*] Opts:/r $TMP/sums.txt" \
        "$TMP/neupod_icons_$sz.c" > "$OUT/neupod_icons_$sz.c"
done
# The line height stays what it was before the icon set (as the text fonts' are pinned above): bluetooth-connected is one row taller than the
# other glyphs at 16 and 36 px and would grow those fonts' line by 1 px, moving every icon in the status bar and the rows. Its top row draws
# above the label instead, inside LVGL's extra draw area (a quarter of the line height).
sedi "s/[.]line_height = [0-9]*,/.line_height = 14,/" "$OUT/neupod_icons_16.c"
sedi "s/[.]line_height = [0-9]*,/.line_height = 33,/" "$OUT/neupod_icons_36.c"
