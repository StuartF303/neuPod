#!/usr/bin/env bash
# Flash firmware to the target.
# Usage: scripts/flash.sh [rp2350|esp32s3] [target-name|path/to.elf]
#   rp2350  (default): OpenOCD via the Raspberry Pi Debug Probe (CMSIS-DAP).
#                      Target defaults to 'neupod'; e.g. 'display_test'.
#   esp32s3:           idf.py flash; set PORT=/dev/ttyXXX to choose the serial port.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="rp2350"
if [[ "${1:-}" == "rp2350" || "${1:-}" == "esp32s3" ]]; then PLATFORM="$1"; shift; fi

case "$PLATFORM" in
    rp2350)
        ARG="${1:-neupod}"
        if [[ "$ARG" == *.elf ]]; then
            ELF="$ARG"
        else
            ELF="$ROOT/build/rp2350/platforms/rp2350/$ARG.elf"
        fi
        [[ -f "$ELF" ]] || { echo "ELF not found: $ELF (run scripts/build.sh rp2350)" >&2; exit 1; }
        exec openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
            -c "adapter speed 5000" \
            -c "program $ELF verify reset exit"
        ;;
    esp32s3)
        command -v idf.py >/dev/null || { echo "idf.py not found (. ~/esp/esp-idf/export.sh)" >&2; exit 1; }
        exec idf.py -C "$ROOT/platforms/esp32s3" -B "$ROOT/build/esp32s3" ${PORT:+-p "$PORT"} flash
        ;;
esac
