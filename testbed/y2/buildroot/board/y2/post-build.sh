#!/usr/bin/env bash
# Buildroot post-build hook for the y2 rootfs. $1 = target dir.
#   - installs diag.sh from the rescue overlay (single source of truth)
#   - installs root's SSH key from $Y2_SSH_PUBKEY (generated into the image only; never committed)
#   - drops Buildroot's mdev daemon script (S00dev arms mdev as hotplug helper instead)
#   - makes /var/log and /etc/dropbear persistent real directories (skeleton symlinks them to tmpfs)
#   - writes /etc/y2-release
set -euo pipefail
TARGET=$1
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
Y2=$(cd "$HERE/../../.." && pwd)

install -D -m 0755 "$Y2/rescue/overlay/bin/diag.sh" "$TARGET/usr/bin/diag.sh"
# Boot-time runs must never remount the SD read-write (Y2_DIAG_NO_SD=1, set by S99diag). The rescue copy
# is not edited; patch the installed one and fail loudly if the rescue script changed shape.
sed -i 's/^if \[ -n "\$SDMOUNTED" \] && mount -o remount,rw/if [ -n "$SDMOUNTED" ] \&\& [ -z "$Y2_DIAG_NO_SD" ] \&\& mount -o remount,rw/' "$TARGET/usr/bin/diag.sh"
grep -q 'Y2_DIAG_NO_SD' "$TARGET/usr/bin/diag.sh" || { echo "y2 post-build: diag.sh SD-save line not found; cannot patch" >&2; exit 1; }
chmod 0755 "$TARGET"/etc/init.d/S* "$TARGET/usr/sbin/y2-console"
rm -f "$TARGET/etc/init.d/S10mdev"
# No syslog/klogd/cron/module loading/network script: nothing should write logs to eMMC at boot, there are no
# modules, and S01mounts brings lo up. S01seedrng stays; S02sysctl stays only if /etc/sysctl.conf has content.
rm -f "$TARGET"/etc/init.d/{S01syslogd,S02klogd,S50crond,S11modules,S40network}
grep -qv '^[[:space:]]*\(#\|$\)' "$TARGET/etc/sysctl.conf" 2>/dev/null || rm -f "$TARGET/etc/init.d/S02sysctl"

for d in var/log etc/dropbear; do
    [[ -L $TARGET/$d ]] && rm -f "$TARGET/$d"
    mkdir -p "$TARGET/$d"
done

# SSH key: $Y2_SSH_PUBKEY, else the first default that exists.
KEY=${Y2_SSH_PUBKEY:-}
if [[ -z $KEY ]]; then
    for c in "$HOME/.ssh/id_ed25519.pub" "$HOME/.ssh/id_rsa.pub"; do
        [[ -f $c ]] && { KEY=$c; break; }
    done
fi
rm -rf "$TARGET/root/.ssh"
if [[ -n $KEY && -f $KEY ]]; then
    if grep -q "PRIVATE KEY" "$KEY"; then
        echo "y2 post-build: $KEY contains a private key; refusing" >&2; exit 1
    fi
    ssh-keygen -l -f "$KEY" >/dev/null || { echo "y2 post-build: $KEY is not a valid public key" >&2; exit 1; }
    install -d -m 0700 "$TARGET/root" "$TARGET/root/.ssh"
    install -m 0600 "$KEY" "$TARGET/root/.ssh/authorized_keys"
    echo "y2 post-build: authorized_keys installed from $(basename "$KEY")"
else
    echo "y2 post-build: WARNING no SSH public key found (set Y2_SSH_PUBKEY); SSH login will be impossible" >&2
fi

# Version stamp (SOURCE_DATE_EPOCH-respecting).
EPOCH=${SOURCE_DATE_EPOCH:-$(date +%s)}
DESC=$(git -C "$Y2" describe --always --dirty --tags 2>/dev/null || echo unknown)
{
    echo "y2-rootfs $DESC"
    echo "source-date-epoch $(date -u -d "@$EPOCH" '+%Y-%m-%dT%H:%M:%SZ') ($EPOCH)"
} >"$TARGET/etc/y2-release"
