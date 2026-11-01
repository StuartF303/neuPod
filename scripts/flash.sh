#!/usr/bin/env bash
# Flash build/app/neupod.elf over the Raspberry Pi Debug Probe (CMSIS-DAP) via OpenOCD.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELF="${1:-$ROOT/build/app/neupod.elf}"
[[ -f "$ELF" ]] || { echo "ELF not found: $ELF (build first)" >&2; exit 1; }
exec openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
    -c "adapter speed 5000" \
    -c "program $ELF verify reset exit"
