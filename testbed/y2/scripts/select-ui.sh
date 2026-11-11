#!/usr/bin/env bash
# Choose which UI owns the Y2's screen: the C firmware ("fw") or the .NET demo ("demo").
#
#   select-ui.sh fw|demo [--dry-run]
#
# Env: Y2_HOST (default root@192.168.7.2).
# Writes /etc/neupod-ui (read by S90neupod-demo and S91neupod-fw at boot: each starts only if selected, a missing file
# means demo), stops the UI that is running, and starts the selected one. The two never run together: both draw on /dev/fb0.
# Needs the selected UI to be installed (deploy-fw.sh / deploy-demo.sh); if it is not, nothing is changed.
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
case "$want" in
	fw) new_init=/etc/init.d/S91neupod-fw; old_init=/etc/init.d/S90neupod-demo; bin=/opt/neupod-fw/neupod ;;
	demo) new_init=/etc/init.d/S90neupod-demo; old_init=/etc/init.d/S91neupod-fw; bin=/opt/neupod-demo/NeuPod.Y2Demo ;;
esac
if [ ! -x "$new_init" ] || [ ! -x "$bin" ]; then
	echo "$want is not installed on the device ($bin or $new_init missing): nothing changed" >&2
	exit 1
fi
[ -x "$old_init" ] && "$old_init" stop || true
echo "$want" > /etc/neupod-ui.new
mv /etc/neupod-ui.new /etc/neupod-ui
sync
"$new_init" start
echo "selected: $(cat /etc/neupod-ui)"
'

if [ "$DRY" = 1 ]; then
	echo "DRY: ${SSH[*]} 'sh -s -- $WHICH' <<< (stop the other UI, write /etc/neupod-ui, start $WHICH)"
	exit 0
fi
"${SSH[@]}" "sh -s -- $WHICH" <<<"$REMOTE"
