#!/usr/bin/env bash
# Open the target's serial console at 115200 8N1.
# Usage: scripts/console.sh [rp2350|esp32s3]
#   rp2350  (default): Debug Probe UART bridge (UART0 GP0/GP1) in minicom.
#   esp32s3:           idf.py monitor; set PORT=/dev/ttyXXX to choose the serial port.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="${1:-rp2350}"

case "$PLATFORM" in
    rp2350)
        shopt -s nullglob
        ports=(/dev/serial/by-id/*Debug_Probe*)
        if (( ${#ports[@]} == 0 )); then
            echo "No Debug Probe UART found under /dev/serial/by-id (is it plugged in?)" >&2
            exit 1
        fi
        if (( ${#ports[@]} > 1 )); then
            echo "Multiple Debug Probe ports found; using ${ports[0]}:" >&2
            printf '  %s\n' "${ports[@]}" >&2
        fi
        exec minicom -D "${ports[0]}" -b 115200
        ;;
    esp32s3)
        command -v idf.py >/dev/null || { echo "idf.py not found (. ~/esp/esp-idf/export.sh)" >&2; exit 1; }
        exec idf.py -C "$ROOT/platforms/esp32s3" -B "$ROOT/build/esp32s3" ${PORT:+-p "$PORT"} monitor
        ;;
    *)
        echo "Unknown platform '$PLATFORM' (rp2350, esp32s3)" >&2
        exit 2
        ;;
esac
