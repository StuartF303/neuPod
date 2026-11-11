#!/usr/bin/env bash
# Cross-build the neuPod C firmware for the Y2 (NEUPOD_PLATFORM=y2) and install it over ssh.
#
#   deploy-fw.sh [--select] [--no-restart] [--no-build] [--dry-run]
#
# Env: Y2_HOST (default root@192.168.7.2); NEUPOD_Y2_TOOLCHAIN / NEUPOD_Y2_TRIPLE (see cmake/toolchain-y2.cmake);
#      PICO_SDK_PATH (the secrets code needs its mbed TLS; the build refuses a stub unless NEUPOD_ALLOW_CRYPTO_STUB=1).
#   --select      also make the firmware the UI: stop the .NET demo, write /etc/neupod-ui = fw, start the firmware.
#                 Nothing on the device changes (demo still running, /etc/neupod-ui untouched) until the new build is staged,
#                 its checksum matches and it passed a self-test. If the new build then does not stay up, the previous build
#                 is restored; if the firmware was already the selected UI it stays selected with the old binary, otherwise
#                 the previous selection (and the demo) come back.
#   --no-restart  copy and stage only: the new build is left in /opt/neupod-fw.new and the init scripts are refreshed,
#                 but nothing running is touched and the live install is not swapped.
#   --no-build    use the existing build/y2 output.
#   --dry-run     print what would run; no build, no ssh.
# Touches only: /opt/neupod-fw (staged as /opt/neupod-fw.new, renamed in; the previous build is kept as /opt/neupod-fw.old
# until the new one has stayed up, and restored if it does not), /etc/init.d/S91neupod-fw (new), /etc/init.d/S90neupod-demo
# (refreshed so it honours /etc/neupod-ui), and /etc/neupod-ui with --select. No sudo, no flashing, no partition writes,
# and the firmware never opens the internal eMMC (it only ever opens a removable microSD, see platforms/y2/hal/y2_sd.h).
#
# ssh host-key checking is left at its default. After the Y2 is reflashed or its rootfs is rebuilt, dropbear generates a
# new host key and ssh refuses to connect; verify the device is yours, then run
#   ssh-keygen -R <host>        (and reconnect once to accept the new key)
set -euo pipefail

HOST="${Y2_HOST:-root@192.168.7.2}"
SELECT=0
RESTART=1
BUILD=1
DRY=0
for a in "$@"; do
	case "$a" in
		--select) SELECT=1 ;;
		--no-restart) RESTART=0 ;;
		--no-build) BUILD=0 ;;
		--dry-run) DRY=1 ;;
		-h|--help) sed -n '2,/^set -euo/{/^set -euo/!p}' "$0"; exit 0 ;;
		*) echo "unknown option: $a" >&2; exit 2 ;;
	esac
done
if [ "$SELECT" = 1 ] && [ "$RESTART" = 0 ]; then
	echo "--select and --no-restart contradict each other" >&2
	exit 2
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD_DIR="$ROOT/build/y2"
BIN="$BUILD_DIR/platforms/y2/neupod.stripped"
OVERLAY="$HERE/../buildroot/board/y2/overlay/etc/init.d"
INIT_FW="$OVERLAY/S91neupod-fw"
INIT_DEMO="$OVERLAY/S90neupod-demo"

SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")

run() {
	if [ "$DRY" = 1 ]; then printf 'DRY: %s\n' "$*"; else "$@"; fi
}

[ -f "$INIT_FW" ] || { echo "missing $INIT_FW" >&2; exit 1; }
[ -f "$INIT_DEMO" ] || { echo "missing $INIT_DEMO" >&2; exit 1; }

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

if [ "$BUILD" = 1 ]; then
	echo "==> cross-build (NEUPOD_PLATFORM=y2, Release)"
	CRYPTO=(-DNEUPOD_REQUIRE_CRYPTO=ON)
	[ "${NEUPOD_ALLOW_CRYPTO_STUB:-0}" = 1 ] && CRYPTO=()
	run cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DNEUPOD_PLATFORM=y2 -DCMAKE_BUILD_TYPE=Release "${CRYPTO[@]}"
	run ninja -C "$BUILD_DIR"
fi

if [ "$DRY" = 0 ]; then
	[ -f "$BIN" ] || { echo "no $BIN: build first (or drop --no-build)" >&2; exit 1; }
	SUM="$(sha256sum "$BIN" | cut -d' ' -f1)"
fi

echo "==> stage on $HOST:/opt/neupod-fw.new"
if [ "$DRY" = 1 ]; then
	echo "DRY: ${SSH[*]} 'rm -rf /opt/neupod-fw.new && mkdir -p /opt/neupod-fw.new && cat > /opt/neupod-fw.new/neupod' < $BIN"
else
	"${SSH[@]}" 'set -e; rm -rf /opt/neupod-fw.new; mkdir -p /opt/neupod-fw.new; cat > /opt/neupod-fw.new/neupod; chmod 755 /opt/neupod-fw.new/neupod' < "$BIN"
	# A failed or truncated copy never reaches /opt/neupod-fw.
	got="$("${SSH[@]}" 'sha256sum /opt/neupod-fw.new/neupod' | cut -d' ' -f1)"
	if [ "$got" != "$SUM" ]; then
		echo "error: copy on the device differs from the build ($got != $SUM); not installing" >&2
		"${SSH[@]}" 'rm -rf /opt/neupod-fw.new' || true
		exit 1
	fi
fi

# Self-test of the staged binary on the device: with an unknown option it must print its usage and exit 64 without touching
# the display or the card (argument parsing comes first). Catches a wrong architecture, a missing library or a bad copy
# before anything on the device is changed.
echo "==> self-test the staged build"
if [ "$DRY" = 1 ]; then
	echo "DRY: ${SSH[*]} '/opt/neupod-fw.new/neupod --self-test; expect exit 64'"
else
	rc=0
	"${SSH[@]}" '/opt/neupod-fw.new/neupod --self-test >/dev/null 2>&1; exit $?' || rc=$?
	if [ "$rc" != 64 ]; then
		echo "error: the staged build did not start properly on the device (exit $rc); not installing, nothing was changed" >&2
		"${SSH[@]}" 'rm -rf /opt/neupod-fw.new' || true
		exit 1
	fi
fi

install_init() { # <local file> <remote path>
	if [ "$DRY" = 1 ]; then
		echo "DRY: ${SSH[*]} 'cat > $2.new && chmod 755 $2.new && mv $2.new $2' < $1"
	else
		"${SSH[@]}" "set -e; cat > $2.new; chmod 755 $2.new; mv $2.new $2" < "$1"
	fi
}
echo "==> install init scripts (S91neupod-fw, and S90neupod-demo so it honours /etc/neupod-ui)"
install_init "$INIT_FW" /etc/init.d/S91neupod-fw
install_init "$INIT_DEMO" /etc/init.d/S90neupod-demo

if [ "$RESTART" = 0 ]; then
	echo "staged in /opt/neupod-fw.new; not swapped or restarted (--no-restart). Re-run without the flag to install."
	exit 0
fi

echo "==> swap into place, restart, verify (rolls back to the previous build if the new one does not stay up)"
# Remote script, $1 = 1 to select the firmware as the UI. set -e so a failed rename can never be masked by a later command.
# The previous build is deleted only after the new one has started and stayed up. Remaining window: between the two mv
# commands there is no /opt/neupod-fw (the app is already stopped, and the init script is a no-op when the binary is
# absent), so a power cut or lost ssh in that instant leaves /opt/neupod-fw.old and .new on disk for a manual mv.
REMOTE_SWAP='
set -e
SELECT=$1
NEW=/opt/neupod-fw.new; CUR=/opt/neupod-fw; OLD=/opt/neupod-fw.old
FW=/etc/init.d/S91neupod-fw; DEMO=/etc/init.d/S90neupod-demo; UI=/etc/neupod-ui; LOG=/tmp/neupod-fw.log
ui_now() { tr -d " \t\r\n" <"$UI" 2>/dev/null || true; }
set_ui() { echo "$1" >"$UI.new"; mv "$UI.new" "$UI"; sync; }
PREV_UI=$(ui_now)
PREV_EXISTS=0; [ -e "$UI" ] && PREV_EXISTS=1
FW_SEL=0; [ "$PREV_UI" = fw ] && FW_SEL=1
restore_ui() { if [ "$PREV_EXISTS" = 1 ]; then set_ui "$PREV_UI"; else rm -f "$UI"; fi; }
rollback() {
	"$FW" stop || true
	rm -rf "$CUR"
	if [ -e "$OLD" ]; then mv "$OLD" "$CUR"; fi
	if [ "$FW_SEL" = 1 ] && [ -x "$CUR/neupod" ]; then
		echo "the firmware was already selected: keeping it, with the previous build" >&2
		"$FW" start || true
	elif [ "$FW_SEL" = 1 ] || [ "$SELECT" = 1 ]; then
		# nothing to fall back to in the firmware, or a selection that did not work out: the demo it was
		echo "restoring the previous selection and the demo" >&2
		if [ "$FW_SEL" = 1 ]; then set_ui demo; else restore_ui; fi
		"$DEMO" start || true
	fi
}
[ -x "$NEW/neupod" ] || { echo "no staged build in $NEW" >&2; exit 1; }
# From here on the device is changed. Stop whichever UI owns the screen.
if [ "$SELECT" = 1 ] && [ "$FW_SEL" != 1 ]; then
	"$DEMO" stop || true
fi
"$FW" stop
rm -rf "$OLD"
if [ -e "$CUR" ]; then mv "$CUR" "$OLD"; fi
if ! mv "$NEW" "$CUR"; then
	echo "swap failed; restoring previous build" >&2
	rollback
	exit 1
fi
sync
if [ "$SELECT" = 1 ]; then set_ui fw; fi
if [ "$(ui_now)" != fw ]; then
	rm -rf "$OLD"
	echo "installed in /opt/neupod-fw; the firmware is not the selected UI (use --select, or select-ui.sh fw)"
	exit 0
fi
"$FW" start
sleep 5
ap=$(cat /run/neupod-fw.app.pid 2>/dev/null || true)
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
	echo "DRY: ${SSH[*]} 'sh -s -- $SELECT' <<< (remote swap script)"
else
	"${SSH[@]}" "sh -s -- $SELECT" <<<"$REMOTE_SWAP"
fi
echo "done. log: ${SSH[*]} cat /tmp/neupod-fw.log   keys: ${SSH[*]} 'echo s > /run/neupod.keys'"
