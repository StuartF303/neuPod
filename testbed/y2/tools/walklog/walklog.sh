#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# walklog.sh start|stop|status — field-test logger for the Y2 (busybox sh).
# Every 30 s: battery + system sample to $DIR/<tag>-batt.csv. Continuously: the firmware log (minus the
# per-second "alive" lines), each line prefixed with the wall-clock time, to $DIR/<tag>-fw.log. sync every 5 min.
DIR=/opt/neupod-logs
PIDF=/run/walklog.pid
B=/sys/class/power_supply/battery
PERIOD=${WALKLOG_PERIOD:-30}

sample_loop() {
	csv=$1
	echo "time,uptime_s,capacity_pct,status,batt_mv,avg_current_ma,batt_temp_c10,usb_online,load1,mem_free_kb,fw_pid" >"$csv"
	n=0
	while :; do
		up=$(cut -d' ' -f1 /proc/uptime)
		load=$(cut -d' ' -f1 /proc/loadavg)
		memf=$(awk '/MemFree/{print $2}' /proc/meminfo)
		echo "$(date +%FT%T),$up,$(cat $B/capacity),$(cat $B/status),$(cat $B/batt_vol),$(cat $B/BatteryAverageCurrent),$(cat $B/batt_temp),$(cat /sys/class/power_supply/usb/online),$load,$memf,$(cat /run/neupod-fw.app.pid 2>/dev/null)" >>"$csv"
		n=$((n + 1))
		[ $((n % 10)) -eq 0 ] && sync
		sleep "$PERIOD"
	done
}

fw_loop() {
	out=$1
	# -F follows the log across the wrapper's truncation (it is cut back at 256 KB).
	tail -n 0 -F /tmp/neupod-fw.log 2>/dev/null | while IFS= read -r line; do
		case "$line" in "neuPod alive"*) continue ;; esac
		echo "$(date +%T) $line"
	done >>"$out"
}

case "$1" in
start)
	[ -f "$PIDF" ] && kill -0 "$(cat $PIDF)" 2>/dev/null && { echo "already running"; exit 0; }
	mkdir -p "$DIR"
	tag=walk-$(date +%Y%m%d-%H%M%S)
	# Each loop is its own session (setsid), so stop can kill its whole process group (the tail | while pipeline too).
	setsid "$0" _sample "$DIR/$tag-batt.csv" >/dev/null 2>&1 </dev/null &
	echo $! >"$PIDF"
	setsid "$0" _fw "$DIR/$tag-fw.log" >/dev/null 2>&1 </dev/null &
	echo $! >>"$PIDF"
	echo "logging to $DIR/$tag-*"
	;;
_sample) sample_loop "$2" ;;
_fw) fw_loop "$2" ;;
stop)
	for p in $(cat "$PIDF" 2>/dev/null); do kill -TERM -"$p" 2>/dev/null || kill "$p" 2>/dev/null; done
	rm -f "$PIDF"; sync; echo stopped
	;;
status)
	for p in $(cat "$PIDF" 2>/dev/null); do kill -0 "$p" 2>/dev/null && echo "pid $p running" || echo "pid $p gone"; done
	ls -l "$DIR"
	;;
*) echo "usage: $0 start|stop|status" ;;
esac
