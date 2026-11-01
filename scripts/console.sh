#!/usr/bin/env bash
# Open the Debug Probe's UART bridge at 115200 8N1 in minicom.
set -euo pipefail
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
