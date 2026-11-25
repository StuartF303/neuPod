#!/usr/bin/env bash
# Y2 testbed host setup (bench box "tiny"). Idempotent; safe to re-run.
#
#   sudo testbed/y2/tools/setup-host.sh --system   # apt packages + udev rule (needs root)
#   testbed/y2/tools/setup-host.sh                 # venv + pinned mtkclient (as your user)
#
# mtkclient is not published on PyPI; it is installed from the upstream git repo
# pinned to an exact commit so every run gets identical code.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
Y2="$(cd "$HERE/.." && pwd)"
VENV="$Y2/.venv"

MTKCLIENT_REPO="https://github.com/bkerler/mtkclient"
MTKCLIENT_TAG="v2.1.4.1"
MTKCLIENT_COMMIT="5a863eece86fcaa97cb8325cf747e0aae3c307e4"

APT_PKGS=(android-tools-adb android-tools-fastboot evtest picocom device-tree-compiler
          cpio gzip python3-pip python3-venv python3-dev libusb-1.0-0 git binwalk usbutils)
UDEV_SRC="$HERE/udev/51-mediatek.rules"
UDEV_DST="/etc/udev/rules.d/51-mediatek.rules"
UDEV_OLD="/etc/udev/rules.d/60-y2-mtk.rules"   # name used before the rename; removed if present

system_setup() {
    [[ $EUID -eq 0 ]] || { echo "--system needs root: sudo $0 --system" >&2; exit 1; }
    missing=()
    for p in "${APT_PKGS[@]}"; do
        dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' || missing+=("$p")
    done
    if ((${#missing[@]})); then
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y "${missing[@]}"
    else
        echo "apt: all packages present"
    fi
    if [[ -e $UDEV_OLD ]]; then
        rm -f "$UDEV_OLD"
        echo "udev: removed old $UDEV_OLD"
    fi
    if ! cmp -s "$UDEV_SRC" "$UDEV_DST"; then
        install -m 0644 "$UDEV_SRC" "$UDEV_DST"
        udevadm control --reload-rules
        udevadm trigger --subsystem-match=usb
        udevadm trigger --subsystem-match=tty
        echo "udev: installed $UDEV_DST"
    else
        echo "udev: rule up to date"
    fi
    user="${SUDO_USER:-}"
    if [[ -n $user ]]; then
        for g in plugdev dialout; do
            id -nG "$user" | tr ' ' '\n' | grep -qx "$g" || { usermod -aG "$g" "$user"; echo "added $user to $g (re-login needed)"; }
        done
    else
        echo "notice: SUDO_USER is empty; skipped adding a user to plugdev/dialout (add yourself manually)"
    fi
}

user_setup() {
    [[ $EUID -ne 0 ]] || { echo "run the venv step as your normal user, not root" >&2; exit 1; }
    python3 -c 'import ensurepip' 2>/dev/null || { echo "python3-venv missing: run 'sudo $0 --system' first" >&2; exit 1; }
    if [[ ! -x $VENV/bin/python ]]; then
        python3 -m venv "$VENV"
        "$VENV/bin/pip" install -q --upgrade pip wheel
    fi
    want="mtkclient @ git+$MTKCLIENT_REPO@$MTKCLIENT_COMMIT"
    stamp="$VENV/.mtkclient-commit"
    if [[ "$(cat "$stamp" 2>/dev/null)" != "$MTKCLIENT_COMMIT" ]]; then
        "$VENV/bin/pip" install -q "$want"
        echo "$MTKCLIENT_COMMIT" > "$stamp"
    fi
    echo "mtkclient $MTKCLIENT_TAG ($MTKCLIENT_COMMIT) in $VENV"
    "$VENV/bin/python" -c 'import mtkclient' && echo "import ok"
    [[ -x $VENV/bin/mtk ]] || { echo "error: $VENV/bin/mtk missing after install" >&2; exit 1; }
    echo "entry point: $VENV/bin/mtk"
}

case "${1:-}" in
    --system) system_setup ;;
    "")       user_setup ;;
    *)        echo "usage: $0 [--system]" >&2; exit 2 ;;
esac
