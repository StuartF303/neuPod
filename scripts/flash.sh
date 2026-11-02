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
        # OpenOCD's reset leaves BOTH cores halted by the debugger; "program ... reset exit" then
        # quits with core 1 still halted at the bootrom (PC 0xDA), so an image that launches core 1
        # (the audio core) never gets it. So: program and verify, reset-halt, and explicitly resume
        # core 1 and core 0 (only if still halted). OpenOCD resumes the whole SMP group when one
        # member is resumed, so by the time core 0 is reached it is usually already running: that is
        # why the unconditional "resume" printed "[rp2350.cm0] not halted / context restore failed";
        # the state check makes that message go away. Works for bare-metal test images too (core 1
        # then just waits in the bootrom). NEUPOD_OPENOCD_EXTRA='-c "..."' adds commands for experiments.
        EXTRA=()
        if [[ -n "${NEUPOD_OPENOCD_EXTRA:-}" ]]; then eval "EXTRA=(${NEUPOD_OPENOCD_EXTRA})"; fi # honours quotes, e.g. '-c "..."'
        exec openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
            -c "adapter speed 5000" \
            ${EXTRA[@]+"${EXTRA[@]}"} \
            -c "program $ELF verify" \
            -c "reset halt" \
            -c "targets rp2350.cm1" \
            -c 'if {[rp2350.cm1 curstate] eq "halted"} {resume}' \
            -c "targets rp2350.cm0" \
            -c 'if {[rp2350.cm0 curstate] eq "halted"} {resume}' \
            -c "shutdown"
        ;;
    esp32s3)
        command -v idf.py >/dev/null || { echo "idf.py not found (. ~/esp/esp-idf/export.sh)" >&2; exit 1; }
        exec idf.py -C "$ROOT/platforms/esp32s3" -B "$ROOT/build/esp32s3" ${PORT:+-p "$PORT"} flash
        ;;
esac
