#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The Y2 modem init script (testbed/y2/buildroot/board/y2/overlay/etc/init.d/S78mtk-md) against a fake /dev, /run, /data, ccci sysfs
# file and fake stock daemons in a throw-away directory. Usage: test_y2_s78_mtk_md.sh <path to S78mtk-md>
set -u
SCRIPT=$1
T=$(mktemp -d)
trap 'for f in "$T"/run/*.pid; do kill "$(cat "$f" 2>/dev/null)" 2>/dev/null; done; rm -rf "$T"' EXIT
fail=0
check() { # <description> <condition command...>
	d=$1
	shift
	if "$@"; then echo "ok   $d"; else echo "FAIL $d"; fail=1; fi
}

mkdir -p "$T/stage" "$T/bin" "$T/dev" "$T/run" "$T/data"
cat >"$T/bin/ccci_fsd" <<EOF2
#!/bin/sh
echo "\$@" >>"$T/fsd.args"
exec sleep 60
EOF2
# the fake mdinit "boots the modem": it writes md1:2 into the fake sysfs file
cat >"$T/bin/ccci_mdinit" <<EOF2
#!/bin/sh
echo "\$@" >>"$T/mdinit.args"
sleep 1
echo md1:2 >"$T/ccci_boot"
exec sleep 60
EOF2
chmod 755 "$T/bin/ccci_fsd" "$T/bin/ccci_mdinit"
echo md1:0 >"$T/ccci_boot"
# plain files stand in for the dumchar nodes
for n in nvram misc pro_info protect_f protect_s mbr ebr1 ebr2 expdb logo android cache usrdata fat bmtpool uboot preloader misc-sd seccfg sec_ro bootimg recovery mmcblk0 mmcblk0p4; do
	: >"$T/dev/$n"
done

run() {
	MD_STAGE="$T/stage" MD_BIN="$T/bin" MD_DEV="$T/dev" MD_RUN="$T/run" MD_DATA="$T/data" MD_CCCI_BOOT="$T/ccci_boot" \
		MD_WAIT_S=${WAIT:-6} sh "$SCRIPT" "$1" >"$T/out.txt" 2>&1
}
count() { [ -f "$1" ] && wc -l <"$1" | tr -d ' ' || echo 0; }
is_null_link() { [ -L "$T/dev/$1" ] && [ "$(readlink "$T/dev/$1")" = /dev/null ]; }

# --- not staged: nothing happens ---
rmdir "$T/stage"
run start
check "not staged: nothing run" test ! -e "$T/fsd.args"
check "not staged: no /dev/block" test ! -e "$T/dev/block"
check "not staged: quiet" test ! -s "$T/out.txt"
check "not staged: nodes untouched" test ! -L "$T/dev/nvram"
mkdir "$T/stage"

# --- staged ---
run start
check "start returns success" test "$?" = 0
check "block symlink mmcblk0" test -L "$T/dev/block/mmcblk0"
check "block symlink mmcblk0p4" test -L "$T/dev/block/mmcblk0p4"
for n in nvram misc pro_info protect_f protect_s mbr ebr1 ebr2 expdb logo android cache usrdata fat bmtpool uboot; do
	check "$n is a symlink to /dev/null" is_null_link "$n"
done
for n in preloader misc-sd seccfg sec_ro bootimg recovery mmcblk0 mmcblk0p4; do
	check "$n stays real" test ! -L "$T/dev/$n"
done
check "real node kept aside" test -e "$T/dev/.real-nvram"
check "modem NVRAM dirs" test -d "$T/data/nvram/md" -a -d "$T/data/nvram/md2" -a -d "$T/data/ccci_cfg"
check "ccci_fsd started with stock arg 0" test "$(cat "$T/fsd.args")" = 0
check "ccci_mdinit started with stock arg 0" test "$(cat "$T/mdinit.args")" = 0
check "pid files" test -s "$T/run/ccci_fsd.pid" -a -s "$T/run/ccci_mdinit.pid"
check "logs md1:2" grep -q "modem up (md1:2)" "$T/out.txt"

# --- a second start: nothing runs again ---
run start
check "fsd not started twice" test "$(count "$T/fsd.args")" = 1
check "mdinit not started twice" test "$(count "$T/mdinit.args")" = 1

# --- stop ---
pids="$(cat "$T/run/ccci_fsd.pid") $(cat "$T/run/ccci_mdinit.pid")"
run stop
sleep 1
gone() { # a zombie counts as gone (a container's PID 1 may not reap it)
	for p in $pids; do
		[ -r "/proc/$p/stat" ] && [ "$(sed 's/.*) //' "/proc/$p/stat" | cut -c1)" != Z ] && return 1
	done
	return 0
}
check "stop ends both daemons" gone
check "stop removes the pid files" test ! -e "$T/run/ccci_fsd.pid" -a ! -e "$T/run/ccci_mdinit.pid"

# --- fail open: the modem never reaches md1:2 ---
rm -f "$T/fsd.args" "$T/mdinit.args"
echo md1:0 >"$T/ccci_boot"
cat >"$T/bin/ccci_mdinit" <<EOF2
#!/bin/sh
exec sleep 60
EOF2
WAIT=2 run start
check "no md1:2: still exits 0" test "$?" = 0
check "no md1:2: logged" grep -q "did not reach md1:2" "$T/out.txt"
run stop

# --- fail open: staged but daemons missing ---
mv "$T/bin/ccci_fsd" "$T/bin/ccci_fsd.off"
rm -f "$T/fsd.args"
run start
check "missing daemon: exits 0" test "$?" = 0
check "missing daemon: nothing run" test ! -e "$T/fsd.args"
check "missing daemon: says why" grep -q "missing" "$T/out.txt"

# --- the script never writes the places it must not touch ---
check "no nvram_daemon" test "$(grep -v '^#' "$SCRIPT" | grep -c 'nvram_daemon')" = 0
check "no raw eMMC write" test "$(grep -v '^#' "$SCRIPT" | grep -E 'mmcblk0' | grep -vc 'ln -s')" = 0

exit $fail
