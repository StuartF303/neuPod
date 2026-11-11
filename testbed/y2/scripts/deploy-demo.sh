#!/usr/bin/env bash
# Build the .NET demo for the Y2 (self-contained linux-arm) and install it over ssh.
#
#   deploy-demo.sh [--no-restart] [--dry-run]
#
# Env: Y2_HOST (default root@192.168.7.2).
# Touches only: /opt/neupod-demo (staged as /opt/neupod-demo.new, then renamed), /etc/init.d/S90neupod-demo,
# and (indirectly, via the app) /etc/neupod-demo.conf. No sudo, no flashing, no partition writes.
set -euo pipefail

HOST="${Y2_HOST:-root@192.168.7.2}"
RESTART=1
DRY=0
for a in "$@"; do
	case "$a" in
		--no-restart) RESTART=0 ;;
		--dry-run) DRY=1 ;;
		-h|--help) sed -n '2,8p' "$0"; exit 0 ;;
		*) echo "unknown option: $a" >&2; exit 2 ;;
	esac
done

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$HERE/../dotnet/src/NeuPod.Y2Demo"
INIT="$HERE/../buildroot/board/y2/overlay/etc/init.d/S90neupod-demo"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/neupod-demo-publish.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")

run() {
	if [ "$DRY" = 1 ]; then printf 'DRY: %s\n' "$*"; else "$@"; fi
}

[ -f "$INIT" ] || { echo "missing $INIT" >&2; exit 1; }

echo "==> publish (linux-arm, self-contained, Release)"
run dotnet publish "$PROJ" -c Release -r linux-arm --self-contained -o "$OUT" --nologo -v quiet

echo "==> stage on $HOST:/opt/neupod-demo.new"
if [ "$DRY" = 1 ]; then
	echo "DRY: tar -C $OUT -cf - . | ${SSH[*]} 'rm -rf /opt/neupod-demo.new && mkdir -p /opt/neupod-demo.new && tar -xf - -C /opt/neupod-demo.new'"
else
	[ -f "$OUT/NeuPod.Y2Demo" ] || { echo "publish produced no NeuPod.Y2Demo" >&2; exit 1; }
	tar -C "$OUT" -cf - . | "${SSH[@]}" 'rm -rf /opt/neupod-demo.new && mkdir -p /opt/neupod-demo.new && tar -xf - -C /opt/neupod-demo.new'
	# A failed or truncated copy never reaches /opt/neupod-demo.
	"${SSH[@]}" 'test -x /opt/neupod-demo.new/NeuPod.Y2Demo || chmod 755 /opt/neupod-demo.new/NeuPod.Y2Demo; test -x /opt/neupod-demo.new/NeuPod.Y2Demo'
fi

echo "==> install init script"
if [ "$DRY" = 1 ]; then
	echo "DRY: ${SSH[*]} 'cat > /etc/init.d/S90neupod-demo.new && chmod 755 ... && mv ... /etc/init.d/S90neupod-demo' < $INIT"
else
	"${SSH[@]}" 'cat > /etc/init.d/S90neupod-demo.new && chmod 755 /etc/init.d/S90neupod-demo.new && mv /etc/init.d/S90neupod-demo.new /etc/init.d/S90neupod-demo' < "$INIT"
fi

echo "==> swap into place"
if [ "$RESTART" = 1 ]; then
	run "${SSH[@]}" '/etc/init.d/S90neupod-demo stop; rm -rf /opt/neupod-demo.old; [ ! -e /opt/neupod-demo ] || mv /opt/neupod-demo /opt/neupod-demo.old; mv /opt/neupod-demo.new /opt/neupod-demo; rm -rf /opt/neupod-demo.old; sync'
	echo "==> restart"
	run "${SSH[@]}" '/etc/init.d/S90neupod-demo start'
else
	# Renaming under a running app is safe (it keeps its open files); it just keeps running the old build.
	run "${SSH[@]}" 'rm -rf /opt/neupod-demo.old; [ ! -e /opt/neupod-demo ] || mv /opt/neupod-demo /opt/neupod-demo.old; mv /opt/neupod-demo.new /opt/neupod-demo; rm -rf /opt/neupod-demo.old; sync'
	echo "installed; not restarted (--no-restart)"
fi
echo "done. log: ${SSH[*]} cat /tmp/neupod-demo.log"
