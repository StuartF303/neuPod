#!/usr/bin/env bash
# Connect to the y2-rescue console over USB (host side).
#
#   testbed/y2/scripts/usb-console.sh [--telnet] [--no-connect] [--wait SECONDS]
#     (default)     wait for the ACM port (18d1:4ee7), set up the RNDIS link, open picocom on the port
#     --telnet      after the link is up, `telnet 192.168.7.2` instead of picocom
#     --no-connect  only wait and configure the link, then print how to connect
#     --wait N      give up after N seconds (default 120; 0 = wait forever)
#
# Link: host 192.168.7.1/24 <-> device 192.168.7.2/24 (usb0). Configured with NetworkManager (nmcli,
# no sudo); if that is refused the exact `sudo ip` commands are printed instead.
# Leave picocom with Ctrl-A Ctrl-X. The telnet shell is unauthenticated root, USB link only.
set -euo pipefail

VID=18d1 PID=4ee7
HOST_IP=192.168.7.1 DEV_IP=192.168.7.2 CON=y2-rescue
MODE=picocom WAIT=120

die() { echo "error: $*" >&2; exit 1; }
usage() { sed -n '2,12p' "$0"; }

while (($#)); do
    case $1 in
        --telnet) MODE=telnet ;;
        --no-connect) MODE=none ;;
        --wait) shift; WAIT=${1:?--wait needs seconds}; [[ $WAIT =~ ^[0-9]+$ ]] || die "--wait needs a number" ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown option $1" ;;
    esac
    shift
done

# /dev/ttyACMx whose USB parent is VID:PID (udev properties; no sudo needed).
find_tty() {
    local t p
    for t in /dev/ttyACM*; do
        [[ -c $t ]] || continue
        p=$(udevadm info -q property -n "$t" 2>/dev/null) || continue
        if grep -x "ID_VENDOR_ID=$VID" <<<"$p" >/dev/null && grep -x "ID_MODEL_ID=$PID" <<<"$p" >/dev/null; then
            echo "$t"; return 0
        fi
    done
    return 1
}

# Network interface bound by rndis_host whose USB device is VID:PID.
find_iface() {
    local n d usb
    for n in /sys/class/net/*; do
        [[ -e $n/device/driver ]] || continue
        [[ $(basename "$(readlink -f "$n/device/driver")") == rndis_host ]] || continue
        d=$(readlink -f "$n/device")
        usb=$d/..
        [[ -r $usb/idVendor && $(<"$usb/idVendor") == "$VID" && $(<"$usb/idProduct") == "$PID" ]] || continue
        basename "$n"; return 0
    done
    return 1
}

wait_for() { # description command...
    local what=$1 start=$SECONDS out; shift
    echo "waiting for $what (Ctrl-C to stop) ..." >&2
    until out=$("$@"); do
        ((WAIT == 0 || SECONDS - start < WAIT)) || die "timed out waiting for $what"
        sleep 1
    done
    echo "$out"
}

print_manual() {
    cat >&2 <<EOF
Could not configure $1 automatically. Run:
  sudo ip addr add $HOST_IP/24 dev $1
  sudo ip link set $1 up
EOF
}

configure_link() {
    local ifc=$1
    if ip -4 addr show dev "$ifc" | grep "inet $HOST_IP/24" >/dev/null; then
        echo "$ifc already has $HOST_IP/24" >&2; return 0
    fi
    command -v nmcli >/dev/null || { print_manual "$ifc"; return 1; }
    if nmcli -t -f NAME connection show | grep -x "$CON" >/dev/null; then
        nmcli connection modify "$CON" connection.interface-name "$ifc" \
            ipv4.method manual ipv4.addresses "$HOST_IP/24" ipv6.method ignore || { print_manual "$ifc"; return 1; }
    else
        nmcli connection add type ethernet ifname "$ifc" con-name "$CON" \
            ipv4.method manual ipv4.addresses "$HOST_IP/24" ipv6.method ignore >/dev/null || { print_manual "$ifc"; return 1; }
    fi
    nmcli connection up "$CON" ifname "$ifc" >/dev/null || { print_manual "$ifc"; return 1; }
    echo "$ifc configured as $HOST_IP/24 (NetworkManager connection '$CON')" >&2
}

TTY=$(wait_for "ACM port $VID:$PID" find_tty)
echo "console port: $TTY" >&2

# RNDIS may enumerate a moment after ACM; its absence (ACM-only fallback) is not fatal.
WAIT_SAVE=$WAIT WAIT=10
IFC=$(wait_for "RNDIS interface" find_iface || true)
WAIT=$WAIT_SAVE
LINK=0
if [[ -n $IFC ]]; then
    if configure_link "$IFC"; then
        LINK=1
        ping -c1 -W2 "$DEV_IP" >/dev/null 2>&1 && echo "device answers at $DEV_IP" >&2 || echo "no ping reply from $DEV_IP yet (telnetd may still be starting)" >&2
    fi
else
    echo "no RNDIS interface (ACM-only gadget?): serial console only" >&2
fi

case $MODE in
    picocom)
        command -v picocom >/dev/null || die "picocom not installed (sudo apt install picocom), or use --telnet"
        [[ -r $TTY && -w $TTY ]] || die "cannot open $TTY (add yourself to the 'dialout' group, or check the udev rules)"
        exec picocom -b 115200 "$TTY" ;;
    telnet)
        command -v telnet >/dev/null || die "telnet client not installed (sudo apt install telnet), or use the serial console"
        ((LINK)) || die "no usable network link; use the serial console (run without --telnet)"
        exec telnet "$DEV_IP" ;;
    none)
        echo "serial: picocom -b 115200 $TTY"
        ((LINK)) && echo "network: telnet $DEV_IP   (logs: http://$DEV_IP/rescue.log)" ;;
esac
