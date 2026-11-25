#!/usr/bin/env bash
# Get the Y2 into y2-rescue with no key presses (host side). Idempotent: resumes from wherever the device is.
#
#   testbed/y2/scripts/boot-rescue.sh
#
# LK on this unit has no usable recovery/fastboot key check, so the way in is:
#   (a) preloader handshake `mtk meta FASTBOOT` -> LK stays in fastboot (0bb4:0c01)
#   (b) `lkboot.py reboot-recovery` (`oem reboot-recovery`) -> LK boots the RECOVERY partition (y2-rescue)
#   (c) the rescue gadget enumerates (18d1:4ee7), its DHCP server gives this PC 192.168.7.1
# Nothing is written to flash: the handshake and the oem command only set the boot mode.
# Already in rescue (18d1:4ee7 present)? Only the last step (link + log) runs. Log: ~/y2-backup/flash-logs/.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
Y2="$(cd "$HERE/.." && pwd)"
MTK="$Y2/.venv/bin/mtk"
PY="$Y2/.venv/bin/python"
LKBOOT="$Y2/tools/lkboot.py"
LOGDIR="$HOME/y2-backup/flash-logs"
HOST_IP=192.168.7.1 DEV_IP=192.168.7.2
LK_ID=0bb4:0c01 RESCUE_ID=18d1:4ee7

[[ ${1:-} == -h || ${1:-} == --help ]] && { sed -n '2,11p' "$0"; exit 0; }
[[ $# -eq 0 ]] || { echo "error: no arguments expected" >&2; exit 1; }

mkdir -p "$LOGDIR"
LOG="$LOGDIR/boot-rescue-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG") 2>&1
echo "boot-rescue $(date -Is); log: $LOG"

die() { echo "error: $*" >&2; exit 1; }
present() { lsusb -d "$1" >/dev/null 2>&1; }
wait_usb() { # id seconds
    local i
    for ((i = 0; i < $2; i++)); do present "$1" && return 0; sleep 1; done
    return 1
}
# Host network interface that belongs to the rescue gadget (its USB parent is 18d1:4ee7).
rescue_iface() {
    local n d
    for n in /sys/class/net/*; do
        [[ -e $n/device ]] || continue
        d=$(readlink -f "$n/device") || continue
        [[ -r $d/../idVendor && $(<"$d/../idVendor") == "${RESCUE_ID%:*}" \
            && $(<"$d/../idProduct") == "${RESCUE_ID#*:}" ]] || continue
        basename "$n"; return 0
    done
    return 1
}
host_has_ip() {
    local ifc
    ifc=$(rescue_iface) || return 1
    ip -br addr show dev "$ifc" 2>/dev/null | grep -F "$HOST_IP/" >/dev/null
}

if present "$RESCUE_ID"; then
    echo "== device is already in rescue ($RESCUE_ID)"
else
    if present "$LK_ID"; then
        echo "== LK fastboot ($LK_ID) already present; skipping the handshake"
    else
        [[ -x $MTK && -x $PY ]] || die "mtkclient venv missing: run tools/setup-host.sh"
        echo "== (a) arming the preloader handshake (mtk meta FASTBOOT)"
        # fresh scratch dir: a stale ./.state would make mtkclient skip the handshake
        rundir=$(mktemp -d "${TMPDIR:-/tmp}/y2-meta.XXXXXX")
        mtkpid=
        trap '[[ -z $mtkpid ]] || kill "$mtkpid" 2>/dev/null; rm -rf "$rundir"' EXIT
        (cd "$rundir" && exec env PYTHONUNBUFFERED=1 timeout 300 "$MTK" meta FASTBOOT) >"$rundir/mtk.log" 2>&1 &
        mtkpid=$!
        for ((i = 0; i < 30; i++)); do   # wait for mtk to be listening for the preloader
            grep -qi "waiting for preloader" "$rundir/mtk.log" && break
            kill -0 "$mtkpid" 2>/dev/null || break
            sleep 1
        done
        grep -qi "waiting for preloader" "$rundir/mtk.log" || { cat "$rundir/mtk.log"; die "mtk did not reach the preloader wait"; }
        echo "ARMED - now: power the Y2 off (hold power 10 s), unplug it, wait 5 s, plug it in with NO buttons held."
        tail -n +1 -f "$rundir/mtk.log" --pid="$mtkpid" &
        tailpid=$!
        # LK may take a while after the preloader window; mtk's own exit code is irrelevant once LK is up.
        echo "== (b) waiting for LK fastboot ($LK_ID)"
        ok=0
        for ((i = 0; i < 330; i++)); do   # up to 300 s for the human, then 30 s grace after mtk ends
            present "$LK_ID" && { ok=1; break; }
            if ! kill -0 "$mtkpid" 2>/dev/null; then
                wait_usb "$LK_ID" 30 && ok=1
                break
            fi
            sleep 1
        done
        kill "$tailpid" 2>/dev/null || true
        if kill -0 "$mtkpid" 2>/dev/null; then kill "$mtkpid" 2>/dev/null || true; fi
        wait "$mtkpid" 2>/dev/null || true
        mtkpid=
        ((ok)) || die "LK fastboot ($LK_ID) never appeared: retry"
    fi
    sleep 1
    echo "== (c) oem reboot-recovery"
    lkrc=0
    "$PY" "$LKBOOT" reboot-recovery || lkrc=$?
    ((lkrc == 0)) || echo "note: lkboot exited $lkrc; checking whether the device rebooted anyway"
    echo "== (d) waiting up to 60 s for rescue ($RESCUE_ID)"
    wait_usb "$RESCUE_ID" 60 || die "rescue did not enumerate (is the RECOVERY partition the y2-rescue image?)"
fi

echo "== (e) waiting for the host link to get $HOST_IP via DHCP (up to 40 s)"
ok=0
for ((i = 0; i < 40; i++)); do host_has_ip && { ok=1; break; }; sleep 1; done
ip -br addr
if ((ok)); then
    echo "== rescue log from http://$DEV_IP/rescue.log"
    curl -fsS --max-time 10 "http://$DEV_IP/rescue.log" | tail -n 20 || echo "(could not fetch the log yet; retry in a few seconds)"
else
    echo "warning: $HOST_IP not assigned. Fallback (needs sudo): find the new interface in the list above, then"
    echo "  sudo ip addr add $HOST_IP/24 dev <iface>; sudo ip link set <iface> up"
fi
cat <<EOT

Connect:
  telnet $DEV_IP        (unauthenticated root shell, USB link only)
  picocom -b 115200 /dev/serial/by-id/usb-neuPod_y2-rescue*
  http://$DEV_IP/rescue.log   http://$DEV_IP/diag.txt   (run diag.sh on the device first)
Or: $HERE/usb-console.sh
EOT
