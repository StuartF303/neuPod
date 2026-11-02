# scripts/

All scripts take the platform as the first argument; the default is `rp2350`.
Build output goes to `build/<platform>/`.

| Script | Usage | Notes |
|--------|-------|-------|
| `build.sh` | `build.sh [rp2350\|esp32s3\|host] [--clean] [-- args]` | CMake + Ninja for rp2350/host; `idf.py` for esp32s3. Extra args go to CMake / `idf.py build`. |
| `flash.sh` | `flash.sh [rp2350\|esp32s3] [target\|file.elf]` | rp2350: OpenOCD over the Debug Probe (CMSIS-DAP); target defaults to `neupod` (e.g. `display_test`). esp32s3: `idf.py flash`, port from `$PORT`. |
| `console.sh` | `console.sh [rp2350\|esp32s3]` | rp2350: minicom on the Debug Probe UART at 115200. esp32s3: `idf.py monitor`, port from `$PORT`. |
| `setup-esp-idf.sh` | `setup-esp-idf.sh [git-ref]` | Installs ESP-IDF (default `v5.3.2`) to `~/esp/esp-idf` with ESP32-S3 tools. Override location with `$IDF_DIR`. |

**RP2350 and the second core.** OpenOCD's reset leaves both cores halted by the debugger, and a plain
"program ... reset exit" quits with core 1 still halted in the bootrom (PC `0xDA`), so the audio core
never starts. `flash.sh` therefore programs and verifies, does `reset halt`, then resumes core 1 and
core 0 (each only if still halted; in OpenOCD's SMP group resuming one core resumes both, so the
second check normally finds it running, which is what the old "[rp2350.cm0] not halted / context
restore failed" message was). The bare-metal test images are unaffected. `NEUPOD_OPENOCD_EXTRA` takes
extra OpenOCD commands, inserted before `program`.

Examples:

    scripts/build.sh rp2350 --clean
    scripts/flash.sh                    # rp2350, neupod.elf
    scripts/flash.sh rp2350 display_test
    scripts/console.sh

    scripts/build.sh host && ctest --test-dir build/host --output-on-failure
