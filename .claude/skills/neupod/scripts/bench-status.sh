#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Show which neuPod boards are attached and how to reach them. Read-only: nothing is written to any device.
Y2_HOST=${Y2_HOST:-root@192.168.7.2}
has() { lsusb | grep -qi "$1"; }
line() { printf '%-28s %s\n' "$1" "$2"; }

echo "== USB"
has 2e8a:000c && line "Pico 2 W rig (Debug Probe)" "scripts/flash.sh, scripts/console.sh (/dev/ttyACM0)"
has 2e8a:000f && line "RP2350 in BOOTSEL" "picotool load -f -x <uf2>"
for p in /dev/serial/by-id/usb-Raspberry_Pi_Pico_*; do [ -e "$p" ] && line "RP2350 USB stdio" "$p"; done
for p in /dev/serial/by-id/usb-Espressif*; do [ -e "$p" ] && line "ESP32-S3 AMOLED" "$p"; done
has 18d1:4ee8 && line "Y2 (Buildroot)" "ssh $Y2_HOST"
if has 0e8d:2000 || has 0e8d:0003; then line "Y2 in MTK preloader/BROM" "owner only (mtkclient)"; fi

echo "== Y2"
if ping -c1 -W1 "${Y2_HOST#*@}" >/dev/null 2>&1; then
  ssh -o ConnectTimeout=3 -o BatchMode=yes "$Y2_HOST" '
    printf "uptime     %s\n" "$(cut -d" " -f1 /proc/uptime)s"
    printf "battery    %s%% %s mV %s (usb %s)\n" "$(cat /sys/class/power_supply/battery/capacity)" \
      "$(cat /sys/class/power_supply/battery/batt_vol)" "$(cat /sys/class/power_supply/battery/status)" \
      "$(cat /sys/class/power_supply/usb/online)"
    printf "ui         %s\n" "$(cat /etc/neupod-ui 2>/dev/null)"
    printf "firmware   pid %s, %s\n" "$(cat /run/neupod-fw.app.pid 2>/dev/null || echo none)" \
      "$(ls -l /opt/neupod-fw/neupod 2>/dev/null | awk "{print \$6, \$7, \$8}")"
    printf "last log   %s\n" "$(grep -v "neuPod alive" /tmp/neupod-fw.log 2>/dev/null | tail -1)"
  ' 2>&1 | sed 's/^/  /'
else
  echo "  not reachable at ${Y2_HOST#*@}"
fi

echo "== Repo"
git -C "$(dirname "$0")" log --oneline -1 2>/dev/null | sed 's/^/  HEAD /'
