#!/usr/bin/env bash
# Exports the app icon PNGs from the SVG masters in docs/design/2026-11-26-icon-set/svg/ (the source of truth),
# then regenerates the platform icons. Run by hand from mobile/ when the masters change; the PNGs are committed.
# Needs rsvg-convert (librsvg).
set -euo pipefail
cd "$(dirname "$0")/.."
SVG=../docs/design/2026-11-26-icon-set/svg
OUT=assets/app_icon
mkdir -p "$OUT"
rsvg-convert -w 1024 -h 1024 "$SVG/app-icon-master.svg"        -o "$OUT/icon.png"
rsvg-convert -w 1024 -h 1024 "$SVG/app-icon-master-dark.svg"   -o "$OUT/icon_dark.png"
rsvg-convert -w 1024 -h 1024 "$SVG/app-icon-master-tinted.svg" -o "$OUT/icon_tinted.png"
rsvg-convert -w 432  -h 432  "$SVG/android-foreground.svg"     -o "$OUT/android_fg.png"
rsvg-convert -w 432  -h 432  "$SVG/android-background.svg"     -o "$OUT/android_bg.png"
rsvg-convert -w 432  -h 432  "$SVG/android-monochrome.svg"     -o "$OUT/android_mono.png"
dart run flutter_launcher_icons
