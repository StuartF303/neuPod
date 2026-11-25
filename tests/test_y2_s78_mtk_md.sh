#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The Y2 modem init script (testbed/y2/buildroot/board/y2/overlay/etc/init.d/S78mtk-md) against a fake /dev, /run, /data, ccci sysfs
# file, dumchar_info and fake stock daemons in a throw-away directory. Usage: test_y2_s78_mtk_md.sh <path to S78mtk-md>
# Run it with another shell too: SHELL_UNDER_TEST="busybox sh" test_y2_s78_mtk_md.sh <script> (default: sh).
set -u
SCRIPT=$1
SH=${SHELL_UNDER_TEST:-sh}
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
# a dumchar table with names the script does not know about (otp, keystore) and the header line
cat >"$T/dumchar_info" <<EOF2
Part_Name	Size	StartAddr	Type	MapTo
preloader	0x0000000000040000	0x0000000000000000	2	/dev/misc-sd
mbr	0x0000000000080000	0x0000000000000000	2	/dev/mbr
nvram	0x0000000000500000	0x0000000000000000	2	/dev/nvram
otp	0x0000000000010000	0x0000000000000000	2	/dev/otp
keystore	0x0000000000100000	0x0000000000000000	2	/dev/keystore
seccfg	0x0000000000020000	0x0000000000000000	2	/dev/seccfg
sec_ro	0x0000000000600000	0x0000000000000000	2	/dev/sec_ro
BOOTIMG	0x0000000001000000	0x0000000000000000	2	/dev/bootimg
recovery	0x0000000001000000	0x0000000000000000	2	/dev/recovery
EOF2
# plain files stand in for the dumchar nodes
for n in nvram misc pro_info protect_f protect_s mbr ebr1 ebr2 expdb logo android cache usrdata fat bmtpool uboot otp keystore \
	preloader misc-sd seccfg sec_ro bootimg recovery mmcblk0 mmcblk0p4 mmcblk0p7; do
	: >"$T/dev/$n"
done

run() {
	rm -f "$T/S78.log"
	MD_STAGE="$T/stage" MD_BIN="$T/bin" MD_DEV="$T/dev" MD_RUN="$T/run" MD_DATA="$T/data" MD_CCCI_BOOT="$T/ccci_boot" \
		MD_DUMCHAR_INFO="$T/dumchar_info" MD_LOG="$T/S78.log" MD_WAIT_S=${WAIT:-6} $SH "$SCRIPT" "$1" >"$T/out.txt" 2>&1
}
# start returns before the background stage ends (and the stage's output goes to the log file only): wait for its last log line (up to ~$1 s)
wait_finished() {
	w=0
	while [ "$w" -lt "${1:-30}" ] && ! grep -q "modem bring-up finished" "$T/S78.log"; do
		sleep 1
		w=$((w + 1))
	done
	grep -q "modem bring-up finished" "$T/S78.log"
}
count() { [ -f "$1" ] && wc -l <"$1" | tr -d ' ' || echo 0; }
is_null_link() { [ -L "$T/dev/$1" ] && [ "$(readlink "$T/dev/$1")" = /dev/null ]; }

# --- not staged: nothing happens ---
rmdir "$T/stage"
run start
check "not staged: nothing run" test ! -e "$T/fsd.args"
check "not staged: no /dev/block" test ! -e "$T/dev/block"
check "not staged: no property area" test ! -e "$T/dev/__properties__"
check "not staged: quiet" test ! -s "$T/out.txt"
check "not staged: no log file" test ! -e "$T/S78.log"
check "not staged: nodes untouched" test ! -L "$T/dev/nvram"
mkdir "$T/stage"

# --- staged: start must not wait for the modem ---
t0=$(date +%s)
run start
t1=$(date +%s)
check "start returns success" test "$?" = 0
check "first start with no log file: no error noise" test "$(grep -c "can't open" "$T/out.txt")" = 0
check "start does not wait for the modem" test "$((t1 - t0))" -le 3
check "background stage pid file" test -s "$T/run/md_stage.pid"
wait_finished 30
check "property area exists before the daemons" test -f "$T/dev/__properties__"
check "property area is 128 KiB, 0444" test "$(wc -c <"$T/dev/__properties__" | tr -d ' ')" = 131072 -a "$(stat -c %a "$T/dev/__properties__")" = 444
check "property header bytes" test "$(head -c 16 "$T/dev/__properties__" | od -An -tx1 | tr -d ' \n')" = 140000000000000050524f50abd06efc
check "block symlink mmcblk0" test -L "$T/dev/block/mmcblk0"
check "block symlink mmcblk0p4" test -L "$T/dev/block/mmcblk0p4"
for n in nvram misc pro_info protect_f protect_s mbr ebr1 ebr2 expdb logo android cache usrdata fat bmtpool uboot otp keystore; do
	check "$n is a symlink to /dev/null" is_null_link "$n"
done
for n in preloader misc-sd seccfg sec_ro bootimg recovery mmcblk0 mmcblk0p4 mmcblk0p7; do
	check "$n stays real" test ! -L "$T/dev/$n"
done
check "names only in dumchar_info are nulled (otp, keystore)" test -e "$T/dev/.real-otp" -a -e "$T/dev/.real-keystore"
check "kept nodes are logged" grep -q "dumchar node left real: sec_ro" "$T/S78.log"
check "nulled nodes are not logged as left real" test "$(grep -c 'left real: nvram\|left real: otp' "$T/S78.log")" = 0
check "modem NVRAM dirs" test -d "$T/data/nvram/md" -a -d "$T/data/nvram/md2" -a -d "$T/data/ccci_cfg"
check "ccci_fsd started with stock arg 0" test "$(cat "$T/fsd.args")" = 0
check "ccci_mdinit started with stock arg 0" test "$(cat "$T/mdinit.args")" = 0
check "pid files" test -s "$T/run/ccci_fsd.pid" -a -s "$T/run/ccci_mdinit.pid"
check "logs md1:2" grep -q "modem up (md1:2)" "$T/S78.log"
check "the log file has the same lines, background stage included" grep -q "modem bring-up finished" "$T/S78.log"
check "log lines carry an uptime stamp" grep -Eq '^S78mtk-md: [0-9.]+ ccci_fsd started' "$T/S78.log"
check "no muxd binary: flight mode skipped, logged" grep -q "gsm0710muxd is missing" "$T/S78.log"

# --- a second start: nothing runs again ---
run start
check "fsd not started twice" test "$(count "$T/fsd.args")" = 1
check "mdinit not started twice" test "$(count "$T/mdinit.args")" = 1

# --- restart while the modem is up is a logged no-op ---
run restart
check "restart with modem up: logged no-op" grep -q "restart: modem is up" "$T/S78.log"
check "restart with modem up: daemons still there" test -s "$T/run/ccci_mdinit.pid"

# --- stop ---
pids="$(cat "$T/run/ccci_fsd.pid") $(cat "$T/run/ccci_mdinit.pid")"
run stop
gone() { # a zombie counts as gone (a container's PID 1 may not reap it)
	for p in $pids; do
		[ -r "/proc/$p/stat" ] && [ "$(sed 's/.*) //' "/proc/$p/stat" | cut -c1)" != Z ] && return 1
	done
	return 0
}
check "stop ends both daemons" gone
check "stop removes the pid files" test ! -e "$T/run/ccci_fsd.pid" -a ! -e "$T/run/ccci_mdinit.pid" -a ! -e "$T/run/md_stage.pid"

# --- fail open: the modem never reaches md1:2 ---
rm -f "$T/fsd.args" "$T/mdinit.args"
echo md1:0 >"$T/ccci_boot"
cat >"$T/bin/ccci_mdinit" <<EOF2
#!/bin/sh
exec sleep 60
EOF2
WAIT=2 run start
check "no md1:2: still exits 0" test "$?" = 0
wait_finished 10
check "no md1:2: logged" grep -q "did not reach md1:2" "$T/S78.log"
check "no md1:2: finished anyway" grep -q "modem bring-up finished" "$T/S78.log"
echo md1:0 >"$T/ccci_boot"
rm -f "$T/fsd.args"
WAIT=2 run restart
wait_finished 10
check "restart with the modem down starts the daemons again" test -s "$T/fsd.args"
run stop

# --- start with stdout on a pipe returns at once (the stage must not hold it open) ---
run stop
echo md1:0 >"$T/ccci_boot"
t0=$(date +%s)
MD_STAGE="$T/stage" MD_BIN="$T/bin" MD_DEV="$T/dev" MD_RUN="$T/run" MD_DATA="$T/data" MD_CCCI_BOOT="$T/ccci_boot" \
	MD_DUMCHAR_INFO="$T/dumchar_info" MD_LOG="$T/S78.log" MD_WAIT_S=8 $SH "$SCRIPT" start 2>&1 | cat >/dev/null
t1=$(date +%s)
check "start with stdout on a pipe returns promptly" test "$((t1 - t0))" -le 4
run stop

# --- modem already up but daemons not running: start does not re-run ccci_mdinit ---
rm -f "$T/fsd.args" "$T/mdinit.args"
echo md1:2 >"$T/ccci_boot"
run start
check "md1:2 and no daemons: nothing started" test ! -e "$T/fsd.args" -a ! -e "$T/mdinit.args"
check "md1:2 and no daemons: logged" grep -q "modem is already up" "$T/S78.log"
check "md1:2, no marker, no stage: the stage alone is started" grep -q "starting the background stage only" "$T/S78.log"
wait_finished 15
check "md1:2, no marker, no stage: it ran to the end" grep -q "modem bring-up finished" "$T/S78.log"
check "md1:2, no marker, no stage: ccci daemons still not started" test ! -e "$T/fsd.args" -a ! -e "$T/mdinit.args"
run stop
echo done >"$T/run/md_cfun.done"
run start
sleep 1
check "md1:2, marker present: no stage" test ! -s "$T/run/md_stage.pid" && ! grep -q "background stage only" "$T/S78.log"
rm -f "$T/run/md_cfun.done"
sleep 30 &
fake=$!
echo $fake >"$T/run/md_stage.pid"
run start
check "md1:2, stage alive: no second stage" test "$(cat "$T/run/md_stage.pid")" = "$fake" && ! grep -q "background stage only" "$T/S78.log"
kill $fake 2>/dev/null
rm -f "$T/run/md_stage.pid"
echo md1:0 >"$T/ccci_boot"

# --- fail open: staged but daemons missing ---
mv "$T/bin/ccci_fsd" "$T/bin/ccci_fsd.off"
rm -f "$T/fsd.args"
run start
check "missing daemon: exits 0" test "$?" = 0
check "missing daemon: nothing run" test ! -e "$T/fsd.args"
check "missing daemon: says why" grep -q "missing" "$T/S78.log"
mv "$T/bin/ccci_fsd.off" "$T/bin/ccci_fsd"

# --- dumchar_info unreadable: the fixed list still applies ---
for n in nvram otp; do rm -f "$T/dev/$n" "$T/dev/.real-$n"; : >"$T/dev/$n"; done
mv "$T/dumchar_info" "$T/dumchar_info.off"
cat >"$T/bin/ccci_mdinit" <<EOF2
#!/bin/sh
sleep 1
echo md1:2 >"$T/ccci_boot"
exec sleep 60
EOF2
run start
wait_finished 30
check "no dumchar_info: fixed list still nulled" is_null_link nvram
check "no dumchar_info: unknown names are left (logged only with the file)" test ! -L "$T/dev/otp"
run stop
mv "$T/dumchar_info.off" "$T/dumchar_info"

# --- flight mode through a fake gsm0710muxd (a real pseudo-terminal at $RADIO/pttycmd1 that answers AT commands) ---
if command -v python3 >/dev/null 2>&1 && command -v stty >/dev/null 2>&1; then
	cat >"$T/bin/gsm0710muxd" <<'EOF2'
#!/usr/bin/env python3
import os, pty, sys, time, tty
radio = os.environ["MD_RADIO"]
d = os.environ["MD_TEST_DIR"]
open(d + "/muxd.args", "a").write(" ".join(sys.argv[1:]) + "\n")
mode = os.environ.get("MD_TEST_MODE", "ok")
if mode == "nochannel":
    time.sleep(60)
if mode == "alwaysdie":
    sys.exit(4)
if mode == "diefirst":
    c = d + "/muxd.count"
    n = int(open(c).read()) if os.path.exists(c) else 0
    open(c, "w").write(str(n + 1))
    if n == 0:
        print("modem not ready")
        sys.exit(3)
m, s = pty.openpty()
tty.setraw(s)  # as a real mux channel: no echo, so stale data is not looped back as commands
os.makedirs(radio, exist_ok=True)
os.symlink(os.ttyname(s), radio + "/pttycmd1")
if mode in ("stale", "stale-noack"):
    os.write(m, b"\r\nOK\r\n\r\n+EIND: 128\r\n")  # waiting in the channel before anyone asks
buf = b""
n_cfun = 0
while True:
    buf += os.read(m, 256)
    while b"\r" in buf:
        cmd, buf = buf.split(b"\r", 1)
        cmd = cmd.strip().decode()
        open(d + "/at.log", "a").write(cmd + "\n")
        if cmd == "AT+CFUN=4":
            n_cfun += 1
            if mode in ("noack", "stale-noack"):
                out = b""
            elif mode == "nocarrier":
                out = b"\r\nNO CARRIER\r\n"
            elif mode == "cmserror":
                out = b"\r\n+CMS ERROR: 500\r\n"
            elif mode == "flaky" and n_cfun == 1:
                out = b"\r\nERROR\r\n"
            else:
                out = b"\r\nOK\r\n"
        elif cmd == "AT+CFUN?":
            if mode == "badcfun":
                out = b"\r\n+CFUN: 1\r\n\r\nOK\r\n"
            elif mode == "cfun0":
                out = b"\r\n+CFUN: 0\r\n\r\nOK\r\n"
            elif mode == "unsolicited":
                out = b"\r\n+CREG: 0\r\n\r\n+CFUN: 4\r\n\r\nOK\r\n"
            else:
                out = b"\r\n+CFUN: 4\r\n\r\nOK\r\n"
        else:
            out = b"\r\nERROR\r\n"
        os.write(m, out)
EOF2
	chmod 755 "$T/bin/gsm0710muxd"
	flight() { # <mode>: a fresh boot of the script
		run stop
		rm -rf "$T/radio" "$T/at.log" "$T/muxd.args" "$T/muxd.count" "$T/run/md_cfun.done" "$T/S78.log"
		echo md1:0 >"$T/ccci_boot"
		MD_TEST_DIR=$T MD_TEST_MODE=$1 MD_RADIO="$T/radio" MD_MUX_WAIT_S=3 MD_AT_WAIT_S=${AT_W:-2} MD_SETTLE_S=1 MD_UP_SETTLE_S=0 MD_MUX_RETRY_S=1 MD_MUX_TRIES=2 run start
		wait_finished 60
	}
	export MD_TEST_DIR MD_TEST_MODE MD_RADIO
	flight ok
	check "flight: muxd started with the stock arguments" test "$(cat "$T/muxd.args")" = "-s /dev/ttyC0 -f 512 -n 8 -m basic"
	check "flight: AT+CFUN=4 then AT+CFUN?" test "$(tr '\n' ' ' <"$T/at.log")" = "AT+CFUN=4 AT+CFUN? "
	check "flight: logged as on" grep -q "flight mode on (attempt 1" "$T/S78.log"
	check "flight: muxd pid file" test -s "$T/run/gsm0710muxd.pid"
	check "flight: marker written" test -e "$T/run/md_cfun.done"
	MD_TEST_DIR=$T MD_TEST_MODE=ok MD_RADIO="$T/radio" run start
	check "flight: not handled twice in one boot" test "$(grep -c 'AT+CFUN=4' "$T/at.log")" = 1
	flight flaky
	check "flight: retried after an ERROR" test "$(grep -c 'AT+CFUN=4' "$T/at.log")" = 2
	check "flight: confirmed on attempt 2" grep -q "flight mode on (attempt 2" "$T/S78.log"
	flight unsolicited
	check "flight: unsolicited lines are tolerated" grep -q "flight mode on (attempt 1" "$T/S78.log"
	flight stale
	check "flight: a stale OK before the command does not hurt" grep -q "flight mode on (attempt 1" "$T/S78.log"
	flight stale-noack
	check "flight: a stale OK is not taken as the answer" test "$(grep -c 'not acknowledged' "$T/S78.log")" = 3
	flight noack
	check "flight: no OK, 3 attempts then gives up" test "$(grep -c 'AT+CFUN=4' "$T/at.log")" = 3
	check "flight: gives up, logged, exit 0" grep -q "not confirmed after 3 attempts" "$T/S78.log"
	check "flight: exhausted marker" test "$(cat "$T/run/md_cfun.done")" = exhausted
	for m in nocarrier cmserror; do
		t0=$(date +%s)
		AT_W=20 flight $m
		t1=$(date +%s)
		check "flight: $m ends the wait at once" test "$((t1 - t0))" -le 14
		check "flight: $m is a failure: 3 attempts" test "$(grep -c 'AT+CFUN=4' "$T/at.log")" = 3
		check "flight: $m is logged, fail open" grep -q "not confirmed after 3 attempts" "$T/S78.log"
		check "flight: $m exhausted marker" test "$(cat "$T/run/md_cfun.done")" = exhausted
	done
	flight cfun0
	check "flight: +CFUN: 0 counts as radio off" grep -q "radio off (attempt 1" "$T/S78.log"
	check "flight: CFUN=4 is still sent" grep -q "AT+CFUN=4" "$T/at.log"
	check "flight: done marker for radio off" test "$(cat "$T/run/md_cfun.done")" = done
	flight diefirst
	check "flight: muxd that exits early is logged with its status" grep -q "gsm0710muxd exited (status 3)" "$T/S78.log"
	check "flight: and started again" test "$(cat "$T/muxd.count")" = 2
	check "flight: second try succeeds" grep -q "flight mode on (attempt 1" "$T/S78.log"
	check "flight: muxd's own output is in the log file" grep -q "modem not ready" "$T/S78.log"
	flight alwaysdie
	check "flight: muxd that never stays up: gives up, logged" grep -q "would not stay up after 2 tries" "$T/S78.log"
	check "flight: gave up, no AT sent" test ! -e "$T/at.log"
	check "flight: gave up, stage still finished" grep -q "modem bring-up finished" "$T/S78.log"
	flight badcfun
	check "flight: wrong CFUN? is logged" grep -q "AT+CFUN? replied" "$T/S78.log"
	flight nochannel
	check "flight: no channel is logged" grep -q "no .*pttycmd1: flight mode not sent" "$T/S78.log"
	check "flight: no channel, nothing sent" test ! -e "$T/at.log"
	# stop in the middle of an AT exchange leaves no descendant of the stage
	flight noack >/dev/null 2>&1 || :
	run stop
	rm -f "$T/at.log"
	echo md1:0 >"$T/ccci_boot"
	MD_TEST_DIR=$T MD_TEST_MODE=noack MD_RADIO="$T/radio" MD_MUX_WAIT_S=3 MD_AT_WAIT_S=20 MD_SETTLE_S=1 MD_UP_SETTLE_S=0 run start
	w=0
	while [ "$w" -lt 20 ] && ! grep -q 'AT+CFUN=4' "$T/at.log" 2>/dev/null; do sleep 1; w=$((w + 1)); done
	sp=$(cat "$T/run/md_stage.pid")
	descendants() { # all descendants of $1
		snap=$(for st in /proc/[0-9]*/stat; do c=${st#/proc/}; echo "${c%/stat} $(sed 's/.*) //' "$st" 2>/dev/null | cut -d' ' -f2)"; done)
		kids=$1
		while [ -n "$kids" ]; do
			new=$(echo "$snap" | while read -r c pp; do for k in $kids; do [ "$pp" = "$k" ] && echo "$c"; done; done)
			echo $new
			kids=$(echo $new)
		done
	}
	dpids=$(descendants "$sp")
	check "stop mid-exchange: the stage has grandchildren to find" test "$(echo $dpids | wc -w)" -ge 3
	run stop
	left=
	for p in $dpids $sp; do
		[ -r "/proc/$p/stat" ] && [ "$(sed 's/.*) //' "/proc/$p/stat" | cut -c1)" != Z ] && left="$left $p"
	done
	check "stop mid-exchange: no descendant of the stage survives" test -z "$left"
	run stop
	check "flight: stop removes the muxd pid file" test ! -e "$T/run/gsm0710muxd.pid"
	mv "$T/bin/gsm0710muxd" "$T/bin/gsm0710muxd.off"
	flight ok
	check "flight: muxd missing is logged" grep -q "gsm0710muxd is missing" "$T/S78.log"
	check "flight: the modem still counts as up" grep -q "modem up (md1:2)" "$T/S78.log"
	run stop
else
	echo "skip flight-mode checks (needs python3 and stty)"
fi

# --- the script never writes the places it must not touch ---
check "no nvram_daemon" test "$(grep -v '^#' "$SCRIPT" | grep -c 'nvram_daemon')" = 0
check "no raw eMMC write" test "$(grep -v '^#' "$SCRIPT" | grep -E 'mmcblk0' | grep -vc 'ln -s')" = 0
check "ttyC0 only handed to the muxer" test "$(grep -v '^#' "$SCRIPT" | grep -c 'ttyC0')" = 1

exit $fail
