#!/usr/bin/env bash
# Build the Y2 LOGO image: stock logo.bin from the factory firmware with the two innioasis pictures (images 0 and 38)
# replaced by the canonical neuPod splash. Everything else (the charging animation) stays byte-identical. Host only:
# never touches the device. Flash the result with scripts/flash-logo.sh.
#
#   testbed/y2/scripts/build-logo.sh [--out FILE] [--force]
#     --out FILE  default: ~/y2-build/y2-logo.bin. An existing file is never overwritten without --force.
#   Env: Y2_FIRMWARE (default ~/y2-firmware/y2_v3.2.0_FM-20260813; stock logo.bin is read from there)
#
# Colour pipeline: rescue/splash/neupod-splash-480x360.png is already RGB565-exact (y2splash.py quantize), so the
# picture LK shows, the fbtool splash (rescue/overlay/etc/splash.y2i) and this logo have identical pixels.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
Y2="$(cd "$HERE/.." && pwd)"
OUT="$HOME/y2-build/y2-logo.bin"; FORCE=()
while (($#)); do
    case $1 in
        --out) OUT="${2:?--out needs a file}"; shift ;;
        --force) FORCE=(--force) ;;
        -h|--help) sed -n '2,11p' "$0"; exit 0 ;;
        *) echo "error: unknown option $1" >&2; exit 1 ;;
    esac
    shift
done
FW="${Y2_FIRMWARE:-$HOME/y2-firmware/y2_v3.2.0_FM-20260813}"
[[ -f $FW/logo.bin ]] || { echo "error: stock logo.bin not found: $FW/logo.bin" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
python3 -I "$Y2/tools/y2logo.py" replace "$FW/logo.bin" "$Y2/rescue/splash/neupod-splash-480x360.png" "$OUT" ${FORCE[@]+"${FORCE[@]}"}
python3 -I "$Y2/tools/y2logo.py" info "$OUT" | head -1
