#!/usr/bin/env bash
# Build the .NET demo for the Y2 (self-contained linux-arm) and install it over ssh.
#
#   deploy-demo.sh [--no-restart] [--dry-run]
#
# Env: Y2_HOST (default root@192.168.7.2).
#   --no-restart  copy and stage only: the new build is left in /opt/neupod-demo.new and the init script is
#                 refreshed, but nothing running is touched and the live install is not swapped (replacing the
#                 files under a running .NET process can crash it on its next lazy assembly load).
#   --dry-run     print what would run; no build, no ssh.
# Touches only: /opt/neupod-demo (staged as /opt/neupod-demo.new, renamed in; the previous build is kept as
# /opt/neupod-demo.old until the new one has stayed up, and restored if it does not), /etc/init.d/S90neupod-demo,
# and (indirectly, via the app) /etc/neupod-demo.conf. No sudo, no flashing, no partition writes.
#
# ssh host-key checking is left at its default. After the Y2 is reflashed or its rootfs is rebuilt, dropbear
# generates a new host key and ssh refuses to connect; verify the device is yours, then run
#   ssh-keygen -R <host>        (and reconnect once to accept the new key)
set -euo pipefail

HOST="${Y2_HOST:-root@192.168.7.2}"
RESTART=1
DRY=0
for a in "$@"; do
	case "$a" in
		--no-restart) RESTART=0 ;;
		--dry-run) DRY=1 ;;
		-h|--help) sed -n '2,/^set -euo/{/^set -euo/!p}' "$0"; exit 0 ;;
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

if [ "$DRY" = 0 ]; then
	echo "==> check ssh to $HOST"
	if ! err="$("${SSH[@]}" true 2>&1)"; then
		echo "$err" >&2
		if grep -q "REMOTE HOST IDENTIFICATION HAS CHANGED" <<<"$err"; then
			echo "error: the Y2's ssh host key changed (normal after a reflash or a new rootfs build)." >&2
			echo "       If you expected that, run: ssh-keygen -R ${HOST#*@}   then connect once to accept the new key." >&2
		else
			echo "error: cannot reach $HOST over ssh (BatchMode; key auth required)." >&2
		fi
		exit 1
	fi
fi

echo "==> publish (linux-arm, self-contained, Release)"
run dotnet publish "$PROJ" -c Release -r linux-arm --self-contained -o "$OUT" --nologo -v quiet

echo "==> stage on $HOST:/opt/neupod-demo.new"
if [ "$DRY" = 1 ]; then
	echo "DRY: tar --owner=0 --group=0 -C $OUT -cf - . | ${SSH[*]} 'rm -rf /opt/neupod-demo.new && mkdir -p /opt/neupod-demo.new && tar -xf - -C /opt/neupod-demo.new'"
else
	[ -f "$OUT/NeuPod.Y2Demo" ] || { echo "publish produced no NeuPod.Y2Demo" >&2; exit 1; }
	# Owner 0 so files are not owned by the build host's uid on the device. Plain tar: the device's busybox tar has no -z.
	tar --owner=0 --group=0 -C "$OUT" -cf - . | "${SSH[@]}" 'rm -rf /opt/neupod-demo.new && mkdir -p /opt/neupod-demo.new && tar -xf - -C /opt/neupod-demo.new'
	# A failed or truncated copy never reaches /opt/neupod-demo.
	"${SSH[@]}" 'chmod 755 /opt/neupod-demo.new/NeuPod.Y2Demo && test -x /opt/neupod-demo.new/NeuPod.Y2Demo'
fi

echo "==> install init script"
if [ "$DRY" = 1 ]; then
	echo "DRY: ${SSH[*]} 'cat > /etc/init.d/S90neupod-demo.new && chmod 755 ... && mv ... /etc/init.d/S90neupod-demo' < $INIT"
else
	"${SSH[@]}" 'set -e; cat > /etc/init.d/S90neupod-demo.new; chmod 755 /etc/init.d/S90neupod-demo.new; mv /etc/init.d/S90neupod-demo.new /etc/init.d/S90neupod-demo' < "$INIT"
fi

if [ "$RESTART" = 0 ]; then
	echo "staged in /opt/neupod-demo.new; not swapped or restarted (--no-restart). Re-run without the flag to install."
	exit 0
fi

echo "==> swap into place, restart, verify (rolls back to the previous build if the new one does not stay up)"
# Remote script. set -e so a failed rename can never be masked by a later command. The previous build is deleted
# only after the new one has started and stayed up. Remaining window: between the two mv commands there is no
# /opt/neupod-demo (the app is already stopped, and the init script is a no-op when the binary is absent), so a
# power cut or lost ssh in that instant leaves /opt/neupod-demo.old and .new on disk for a manual mv.
REMOTE_SWAP='
set -e
NEW=/opt/neupod-demo.new; CUR=/opt/neupod-demo; OLD=/opt/neupod-demo.old; INIT=/etc/init.d/S90neupod-demo; LOG=/tmp/neupod-demo.log
rollback() {
	"$INIT" stop || true
	rm -rf "$CUR"
	if [ -e "$OLD" ]; then mv "$OLD" "$CUR" && "$INIT" start; fi
}
"$INIT" stop
rm -rf "$OLD"
if [ -e "$CUR" ]; then mv "$CUR" "$OLD"; fi
if ! mv "$NEW" "$CUR"; then
	echo "swap failed; restoring previous build" >&2
	rollback
	exit 1
fi
sync
"$INIT" start
sleep 4
ap=$(cat /run/neupod-demo.app.pid 2>/dev/null || true)
if [ -n "$ap" ] && kill -0 "$ap" 2>/dev/null && ! grep -q "exited (" "$LOG" 2>/dev/null; then
	rm -rf "$OLD"
	echo "new build is up (pid $ap)"
else
	echo "new build did not stay up; restoring previous build. Log tail:" >&2
	tail -n 20 "$LOG" >&2 || true
	rollback
	exit 1
fi
'
if [ "$DRY" = 1 ]; then
	echo "DRY: ${SSH[*]} 'sh -s' <<< (remote swap script)"
else
	"${SSH[@]}" 'sh -s' <<<"$REMOTE_SWAP"
fi
echo "done. log: ${SSH[*]} cat /tmp/neupod-demo.log"
