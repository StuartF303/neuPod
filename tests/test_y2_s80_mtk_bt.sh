#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The Y2 Bluetooth init script (testbed/y2/buildroot/board/y2/overlay/etc/init.d/S80mtk-bt) against a fake /dev, /run, /proc/devices
# and fake stock helpers in a throw-away directory. Usage: test_y2_s80_mtk_bt.sh <path to S80mtk-bt>
set -u
SCRIPT=$1
T=$(mktemp -d)
trap 'kill "$(cat "$T/run/6620_launcher.pid" 2>/dev/null)" 2>/dev/null; rm -rf "$T"' EXIT
fail=0
check() { # <description> <condition command...>
	d=$1
	shift
	if "$@"; then echo "ok   $d"; else echo "FAIL $d"; fail=1; fi
}

mkdir -p "$T/stage" "$T/bin" "$T/dev" "$T/run" "$T/fakebin"
cat >"$T/bin/wmt_loader" <<EOF
#!/bin/sh
echo run >>"$T/loader.count"
exit 255
EOF
cat >"$T/bin/6620_launcher" <<EOF
#!/bin/sh
echo run >>"$T/launcher.count"
exec sleep 60
EOF
# mknod needs root for a device node: record the request and make a plain file instead.
cat >"$T/fakebin/mknod" <<EOF
#!/bin/sh
echo "\$@" >>"$T/mknod.log"
: >"\$1"
EOF
chmod 755 "$T/bin/wmt_loader" "$T/bin/6620_launcher" "$T/fakebin/mknod"
cat >"$T/devices" <<EOF
Character devices:
  1 mem
141 mtk_stp_usktrk
153 mtk_wmt_WIFI_chrdev
190 mtk_stp_wmt
191 mtk_stp_GPS_chrdev
192 mtk_stp_BT_chrdev

Block devices:
  1 ramdisk
179 mmc
EOF

run() {
	PATH="$T/fakebin:$PATH" MTK_STAGE="$T/stage" MTK_BIN="$T/bin" MTK_DEV="$T/dev" MTK_RUN="$T/run" \
		MTK_PROC_DEVICES="$T/devices" MTK_WAIT_S=3 sh "$SCRIPT" "$1" >"$T/out.txt" 2>&1
}
count() { [ -f "$1" ] && wc -l <"$1" | tr -d ' ' || echo 0; }
mode_of() { stat -c '%a' "$1"; }

# --- not staged: nothing happens ---
rmdir "$T/stage"
run start
check "not staged: no property area" test ! -e "$T/dev/__properties__"
check "not staged: nothing run" test ! -e "$T/loader.count"
check "not staged: quiet" test ! -s "$T/out.txt"
mkdir "$T/stage"

# --- staged ---
run start
check "property area exists" test -f "$T/dev/__properties__"
check "property area is 128 KiB" test "$(wc -c <"$T/dev/__properties__" | tr -d ' ')" = 131072
check "property area is 0444" test "$(mode_of "$T/dev/__properties__")" = 444
check "property header bytes" test "$(head -c 16 "$T/dev/__properties__" | od -An -tx1 | tr -d ' \n')" = 140000000000000050524f50abd06efc
check "rest of the property area is zero" test "$(tail -c +17 "$T/dev/__properties__" | tr -d '\000' | wc -c | tr -d ' ')" = 0
check "wmt_loader ran once" test "$(count "$T/loader.count")" = 1
check "loader marker written" test -e "$T/run/wmt_loader.done"
check "exit 255 is logged, not fatal" grep -q "wmt_loader exited 255" "$T/out.txt"
check "stpwmt node from /proc/devices" grep -q "stpwmt c 190 0" "$T/mknod.log"
check "stpbt node from /proc/devices" grep -q "stpbt c 192 0" "$T/mknod.log"
check "wmtWifi node from /proc/devices" grep -q "wmtWifi c 153 0" "$T/mknod.log"
check "nodes are 0660" test "$(mode_of "$T/dev/stpbt")" = 660
check "launcher started once" test "$(count "$T/launcher.count")" = 1
check "launcher pid file" test -s "$T/run/6620_launcher.pid"
check "block-device majors are not used" test "$(grep -c "mmc\|ramdisk" "$T/mknod.log")" = 0

# --- a second start in the same boot: nothing is run again ---
before=$(cat "$T/dev/__properties__" | od -An -tx1 | tr -d ' \n' | cksum)
run start
check "wmt_loader not run twice" test "$(count "$T/loader.count")" = 1
check "launcher not started twice" test "$(count "$T/launcher.count")" = 1
check "second start says the launcher is running" grep -q "already running" "$T/out.txt"
after=$(cat "$T/dev/__properties__" | od -An -tx1 | tr -d ' \n' | cksum)
check "existing property area is left alone" test "$before" = "$after"

# --- stop ---
pid=$(cat "$T/run/6620_launcher.pid")
run stop
sleep 1
# Gone or a zombie: in a container the orphaned launcher may be reparented to a PID 1 that never reaps it, and kill -0
# succeeds on a zombie. On the Y2, init reaps it.
check "stop ends the launcher" sh -c "! { [ -r /proc/$pid/stat ] && [ \"\$(sed 's/.*) //' /proc/$pid/stat | cut -c1)\" != Z ]; }"
check "stop removes the pid file" test ! -e "$T/run/6620_launcher.pid"

# --- staged but the stock helpers are missing: says so, does nothing else ---
rm -rf "$T/run" "$T/dev" "$T/loader.count"
mkdir "$T/run" "$T/dev"
mv "$T/bin/wmt_loader" "$T/bin/wmt_loader.off"
run start
check "missing helper: nothing run" test ! -e "$T/loader.count"
check "missing helper: no property area" test ! -e "$T/dev/__properties__"
check "missing helper: says why" grep -q "missing" "$T/out.txt"

# --- the script never names the places it must not touch as something to open or write ---
check "no nvram_daemon" test "$(grep -v '^#' "$SCRIPT" | grep -c 'nvram_daemon')" = 0
check "no eMMC access" test "$(grep -v '^#' "$SCRIPT" | grep -c 'mmcblk')" = 0
check "no write to /data" test "$(grep -v '^#' "$SCRIPT" | grep -c '/data')" = 0

exit $fail
