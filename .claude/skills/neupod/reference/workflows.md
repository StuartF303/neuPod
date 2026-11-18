# Workflows

Run everything from the repo root (or the worktree). Docker builds use the CI images, so local toolchains are optional.

## Host

```sh
scripts/build.sh host
ctest --test-dir build/host --output-on-failure            # full suite incl. UI tours (slow: library tours ~4 min)
ctest --test-dir build/host -R <name> --output-on-failure  # one test
```

No local toolchain: `docker run --rm --user $(id -u):$(id -g) -v "$PWD":"$PWD" -w "$PWD" neupod-ci:local scripts/build.sh host`.
UI profile: hosted builds carry 320x240, 480x360 and 600x450; `NEUPOD_TOUCH=1` makes the mouse a touch panel.

## Pico 2 W rig (frozen)

```sh
scripts/build.sh rp2350              # --clean after a CMake option change
scripts/flash.sh                     # OpenOCD over the Debug Probe; programs, verifies, resumes both cores
scripts/flash.sh rp2350 display_test # a bench test image
scripts/console.sh                   # UART0 115200 on the probe (/dev/ttyACM0)
```

Keys over the UART: `w`/`s` scroll, `e` select, `m` back, `a`/`d` left/right, `p` play/pause, `l` Hold. `.bench/k.sh "<keys>" [gap]`
sends a string one key at a time. Console lines end in `\r`: capture with `stdbuf -o0 tr -d '\r' < /dev/ttyACM0`, and never leave
a stale `cat` on the port (it steals bytes). Confirm the highlighted row's label in the log before sending `e`.

## RP2350-Touch-AMOLED-2.41

```sh
scripts/build.sh rp2350-amoled241
picotool load -f -x build/rp2350-amoled241/platforms/rp2350/neupod.uf2   # a running image reboots to BOOTSEL itself
```

Console: `/dev/serial/by-id/usb-Raspberry_Pi_Pico_*`. ModemManager grabs a new port for up to a minute; boot lines in that
window are lost (press reset or reflash to see them). Touch logging: `-DNEUPOD_AMOLED_TOUCH_LOG=ON`. Orientation:
`-DNEUPOD_AMOLED_LANDSCAPE=3` for the other landscape.

## ESP32-S3

The `build/esp32s3` directory remembers the path it was configured at, so mount the tree at `/project`:

```sh
docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp -v "$PWD":/project -w /project \
  espressif/idf:v5.3.2 idf.py -C platforms/esp32s3 -B build/esp32s3 build
P=$(ls /dev/serial/by-id/usb-Espressif* | head -1)
~/.venvs/esptool/bin/esptool --chip esp32s3 -p "$P" -b 460800 write-flash 0x10000 build/esp32s3/neupod.bin        # app only
# first install / partition change: from build/esp32s3, write-flash @flash_args
```

In a git worktree, also mount the main repo's `.git` read-only at the same path, or the IDF version stamp prints a git error.
Restore the factory image: `esptool --chip esp32s3 -p "$P" write-flash 0 ~/esp32amoled-backup/factory-flash-16MB.bin`.
Console: same port, 115200 nominal (USB). Keys typed there drive the UI.

## Y2

```sh
testbed/y2/scripts/deploy-fw.sh            # cross-build (y2) + stage + self-test + swap; restores the old build if the new one dies
testbed/y2/scripts/deploy-fw.sh --no-build # reuse build/y2
ssh root@192.168.7.2 'tail -f /tmp/neupod-fw.log'           # firmware log (RAM, cut at 256 KB)
ssh root@192.168.7.2 'echo s > /run/neupod.keys'            # keys: w/s scroll, e select, m menu, a/d, p play
.claude/skills/neupod/scripts/y2-shot.sh [out.png]          # screenshot from /dev/fb0
```

- Firmware: `/opt/neupod-fw/neupod`, started by `/etc/init.d/S91neupod-fw` (respawns; exit status 77 = power off, no respawn).
  Env: `/etc/neupod-fw.env` (`NEUPOD_*` vars, e.g. `NEUPOD_BT_HCI_DUMP=/tmp/x.pklg`, `NEUPOD_BT_INQ_TXPWR=6`, `NEUPOD_BT_INQ_MODE=1`, `NEUPOD_BT_MTK_FC66=1`, `NEUPOD_BT_STOCK_INIT=1`, `NEUPOD_BT_MTK_FC54=1`, `NEUPOD_BT_LE_SCAN_DURING_INQ=1`; see docs/bluetooth.md). UI selection: `/etc/neupod-ui` = `fw|demo`.
- Bluetooth: `/etc/init.d/S80mtk-bt` brings the MT6582 combo chip up at boot (`6620_launcher`). Controller is BT 4.0, no
  Secure Connections.
- Battery: `/sys/class/power_supply/battery/{capacity,batt_vol,status}`. `BatteryAverageCurrent` only means anything while
  charging. No hardware coulomb counter.
- Field logging: `testbed/y2/tools/walklog/walklog.sh` (`start|stop|status`) logs battery + firmware log to `/opt/neupod-logs`.
- Serial console (rescue/boot): `/dev/ttyACM1` when the Pico rig is also attached (`testbed/y2/scripts/usb-console.sh`).
- After a reflash dropbear has a new host key: `ssh-keygen -R 192.168.7.2` (check the device is the owner's first).
- Flashing partitions, the rescue image, mtkclient: **owner only**, via `testbed/y2/scripts/flash-*.sh`. Details are in the
  private `testbed/y2/docs/` and `testbed/y2/README.md`.

## CI and merging

```sh
gh pr checks <n> --watch --interval 30      # run in the background; jobs: host, rp2350, rp2350-amoled241, esp32s3, y2, build-deploy
gh pr merge <n> --squash --delete-branch    # once green (standing OK)
```
