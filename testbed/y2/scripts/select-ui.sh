#!/usr/bin/env bash
# Choose which UI owns the Y2's screen: the C firmware ("fw") or the .NET demo ("demo").
#
#   select-ui.sh fw|demo [--dry-run]
#
# Env: Y2_HOST (default root@192.168.7.2).
# Writes /etc/neupod-ui (read by S90neupod-demo and S91neupod-fw at boot: each starts only if selected, a missing file
# means demo), stops the UI that is running, and starts the selected one. The two never run together: both draw on /dev/fb0.
# Needs the selected UI to be installed (deploy-fw.sh / deploy-demo.sh); if it is not, nothing is changed. After starting it,
# the script waits 5 s and checks the new UI is still running; if it is not, the previous selection (file and running UI) is
# restored and the script exits non-zero.
# Touches only /etc/neupod-ui and the two init scripts' start/stop. No flashing, no partition writes.
set -euo pipefail

HOST="${Y2_HOST:-root@192.168.7.2}"
DRY=0
WHICH=""
for a in "$@"; do
	case "$a" in
		fw|demo) WHICH="$a" ;;
		--dry-run) DRY=1 ;;
		-h|--help) sed -n '2,/^set -euo/{/^set -euo/!p}' "$0"; exit 0 ;;
		*) echo "unknown argument: $a (usage: select-ui.sh fw|demo [--dry-run])" >&2; exit 2 ;;
	esac
done
[ -n "$WHICH" ] || { echo "usage: select-ui.sh fw|demo [--dry-run]" >&2; exit 2; }

SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")

# Remote script, run with the choice as $1. set -e: a failed step stops here instead of leaving two UIs fighting.
REMOTE='
set -e
want=$1
UI=/etc/neupod-ui
case "$want" in
	fw) new_init=/etc/init.d/S91neupod-fw; old_init=/etc/init.d/S90neupod-demo; bin=/opt/neupod-fw/neupod
	    other=demo; log=/tmp/neupod-fw.log; pidf=/run/neupod-fw.app.pid ;;
	demo) new_init=/etc/init.d/S90neupod-demo; old_init=/etc/init.d/S91neupod-fw; bin=/opt/neupod-demo/NeuPod.Y2Demo
	    other=fw; log=/tmp/neupod-demo.log; pidf=/run/neupod-demo.app.pid ;;
esac
if [ ! -x "$new_init" ] || [ ! -x "$bin" ]; then
	echo "$want is not installed on the device ($bin or $new_init missing): nothing changed" >&2
	exit 1
fi
ui_now() { tr -d " \t\r\n" <"$UI" 2>/dev/null || true; }
set_ui() { echo "$1" >"$UI.new"; mv "$UI.new" "$UI"; sync; }
PREV_EXISTS=0; [ -e "$UI" ] && PREV_EXISTS=1
PREV_UI=$(ui_now)
[ "${PREV_UI:-demo}" = fw ] && prev=fw || prev=demo   # a missing or unrecognised file means demo
restore() {
	# back to what was running before: stop the one that failed, restore the file, start the previous UI
	"$new_init" stop || true
	if [ "$PREV_EXISTS" = 1 ]; then set_ui "$PREV_UI"; else rm -f "$UI"; fi
	if [ "$prev" = fw ]; then /etc/init.d/S91neupod-fw start || true; else /etc/init.d/S90neupod-demo start || true; fi
}
[ -x "$old_init" ] && "$old_init" stop || true
set_ui "$want"
"$new_init" start
# the selected UI must stay up: its app running and no crash line in its log
sleep 5
ap=$(cat "$pidf" 2>/dev/null || true)
if [ -n "$ap" ] && kill -0 "$ap" 2>/dev/null && ! grep -q "exited (" "$log" 2>/dev/null; then
	echo "selected: $(cat $UI) (up, pid $ap)"
else
	echo "$want did not stay up; going back to $prev. Log tail:" >&2
	tail -n 15 "$log" >&2 || true
	restore
	exit 1
fi
'

if [ "$DRY" = 1 ]; then
	echo "DRY: ${SSH[*]} 'sh -s -- $WHICH' <<< (stop the other UI, write /etc/neupod-ui, start $WHICH)"
	exit 0
fi
"${SSH[@]}" "sh -s -- $WHICH" <<<"$REMOTE"
