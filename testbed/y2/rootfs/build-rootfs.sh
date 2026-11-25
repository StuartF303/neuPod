#!/usr/bin/env bash
# Build the y2 glibc rootfs (ext4 image for USRDATA) with Buildroot. Idempotent.
#
#   testbed/y2/rootfs/build-rootfs.sh
#
# Outside the repo, under $Y2_BUILD (default ~/y2-build):
#   dl/, buildroot-<ver>/   shared with rescue/build-rescue.sh (version and sha256 are read from it)
#   out-rootfs/             Buildroot output (separate from out-rescue/)
#   y2-rootfs.ext4          the image (copy of out-rootfs/images/rootfs.ext4) + y2-rootfs.ext4.sha256
# SSH key: $Y2_SSH_PUBKEY (pubkey file), else ~/.ssh/id_ed25519.pub, else ~/.ssh/id_rsa.pub. The key only
# ever goes into the image. Flashing is a separate step, not done here.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
Y2=$(cd "$HERE/.." && pwd)
BUILD=${Y2_BUILD:-$HOME/y2-build}

# Single source of truth for the Buildroot pin: the rescue build script.
pin() { sed -n "s/^$1=//p" "$Y2/rescue/build-rescue.sh" | head -1; }
BR_VER=$(pin BR_VER)
BR_SHA256=$(pin BR_SHA256)
[[ -n $BR_VER && -n $BR_SHA256 ]] || { echo "error: cannot read the Buildroot pin from rescue/build-rescue.sh" >&2; exit 1; }
BR_URL=https://buildroot.org/downloads/buildroot-$BR_VER.tar.gz
MAX_IMAGE=$((0x31f00000))
# Features the stock 3.4.67 kernel cannot mount (see rootfs/README.md).
FORBIDDEN_FEATURES="metadata_csum 64bit orphan_file metadata_csum_seed inline_data encrypt casefold ea_inode large_dir project quota verity stable_inodes bigalloc"

DL=$BUILD/dl SRC=$BUILD/buildroot-$BR_VER OUT=$BUILD/out-rootfs
IMG=$BUILD/y2-rootfs.ext4

die() { echo "error: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

mkdir -p "$DL"
TARBALL=$DL/buildroot-$BR_VER.tar.gz
if [[ ! -f $TARBALL || $(sha "$TARBALL") != "$BR_SHA256" ]]; then
    echo "== downloading $BR_URL"
    rm -f "$TARBALL"
    curl -fSL --retry 3 -o "$TARBALL.part" "$BR_URL"
    mv "$TARBALL.part" "$TARBALL"
fi
[[ $(sha "$TARBALL") == "$BR_SHA256" ]] || die "sha256 of $TARBALL does not match the pinned value"
if SIGN=$(curl -fsS --max-time 20 "$BR_URL.sign" 2>/dev/null); then
    grep "SHA256: $BR_SHA256  buildroot-$BR_VER.tar.gz" <<<"$SIGN" >/dev/null \
        || die "upstream .sign file does not list the pinned sha256"
    echo "== tarball sha256 matches upstream's published hash"
else
    echo "== (could not fetch upstream .sign; relying on the pinned sha256)"
fi
if [[ ! -f $SRC/Makefile ]]; then
    echo "== extracting"
    tar -xf "$TARBALL" -C "$BUILD"
fi
# The rescue build downloaded its sources into the tree's own dl/; share them via $DL (hardlinks, no clobber).
if [[ -d $SRC/dl && ! -L $SRC/dl ]]; then
    cp -aln "$SRC"/dl/. "$DL"/ 2>/dev/null || true
fi

START=$(date +%s)
echo "== configuring"
make -C "$SRC" O="$OUT" BR2_EXTERNAL="$Y2/buildroot" BR2_DL_DIR="$DL" y2_defconfig
# Buildroot rsyncs a local-source package (SITE_METHOD = local) only on its first build, so later edits to our own
# packages would be silently ignored (2026-11-11: a stale fbtool shipped without its new `image` command). Clean them
# every time; they are tiny.
for mk in "$Y2"/buildroot/package/*/*.mk; do
    grep -q '_SITE_METHOD *= *local' "$mk" || continue
    pkg=$(basename "$mk" .mk)
    echo "== forcing a rebuild of local package $pkg"
    make -C "$OUT" "$pkg-dirclean"
done
echo "== building"
make -C "$OUT" BR2_DL_DIR="$DL" -j"$(nproc)"
echo "== build took $(($(date +%s) - START)) s"

SRCIMG=$OUT/images/rootfs.ext4
[[ -f $SRCIMG ]] || die "no $SRCIMG"
cp -fL "$SRCIMG" "$IMG.part" && mv -f "$IMG.part" "$IMG"
(cd "$BUILD" && sha256sum "$(basename "$IMG")" >"$IMG.sha256")

echo "== checking $IMG"
SZ=$(stat -c %s "$IMG")
echo "size: $SZ bytes (limit $MAX_IMAGE)"
((SZ <= MAX_IMAGE)) || die "image is $SZ bytes, over USRDATA's usable $MAX_IMAGE"
cat "$IMG.sha256"

HDR=$(dumpe2fs -h "$IMG" 2>/dev/null)
FEATURES=$(sed -n 's/^Filesystem features: *//p' <<<"$HDR")
echo "label: $(sed -n 's/^Filesystem volume name: *//p' <<<"$HDR")"
echo "features: $FEATURES"
for f in $FORBIDDEN_FEATURES; do
    [[ " $FEATURES " == *" $f "* ]] && die "feature '$f' is not supported by kernel 3.4"
done
e2fsck -fn "$IMG" || die "e2fsck found problems"

dbg() { debugfs -R "$1" "$IMG" 2>/dev/null; }
for p in /sbin/init /dev /proc /sys /etc/init.d/S10usb-gadget /usr/bin/diag.sh /etc/y2-release; do
    [[ -n $(dbg "stat $p") ]] || die "$p missing from the image"
done
echo "-- /etc/y2-release:"
dbg "cat /etc/y2-release"
if [[ -n $(dbg "stat /root/.ssh/authorized_keys") ]]; then
    echo "ssh: authorized_keys present ($(dbg "cat /root/.ssh/authorized_keys" | wc -l) key line(s))"
else
    echo "ssh: WARNING no authorized_keys in the image; SSH login impossible (set Y2_SSH_PUBKEY)"
fi
[[ -n $(dbg "stat /usr/sbin/dropbear") ]] && echo "dropbear: installed (key-only, -s)" || die "dropbear missing"
echo "== done: $IMG"
