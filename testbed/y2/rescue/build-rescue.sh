#!/usr/bin/env bash
# Build the y2-rescue initramfs with Buildroot and repack it into Y2 boot images. Idempotent.
#
#   testbed/y2/rescue/build-rescue.sh [--no-pack]
#
# Everything lives OUTSIDE the repo under $Y2_BUILD (default ~/y2-build):
#   dl/                          Buildroot tarball (sha256-pinned below)
#   buildroot-2026.02.3/         extracted tree
#   out-rescue/                  Buildroot output; images/rootfs.cpio.gz is the initramfs
#   scratch-*/                   unpacked stock images
#   y2-rescue-recovery.img       stock RECOVERY kernel + our ramdisk (MTK name "RECOVERY")
#   y2-rescue-boot.img           stock BOOTIMG kernel + our ramdisk (MTK name "ROOTFS"), for later
# Reads (never writes) ~/y2-backup/<date>/partitions/{RECOVERY,BOOTIMG}.bin. Flashing is a separate,
# human-confirmed step: scripts/flash-recovery.sh.
set -euo pipefail

BR_VER=2026.02.3
# Published by upstream in buildroot-2026.02.3.tar.gz.sign (re-checked below) and observed on download.
BR_SHA256=65528a544f1e07c2f5ec487beca483bd380a6af8351a45f3649a19a0e8b63de2
BR_URL=https://buildroot.org/downloads/buildroot-$BR_VER.tar.gz
MAX_IMAGE=$((16 * 1024 * 1024)) MAX_RAMDISK=$((8 * 1024 * 1024))

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
Y2=$(cd "$HERE/.." && pwd)
BUILD=${Y2_BUILD:-$HOME/y2-build}
BACKUP=${Y2_BACKUP:-$HOME/y2-backup/2026-11-10}
TOOL=$Y2/tools/mtk-bootimg.py
DL=$BUILD/dl SRC=$BUILD/buildroot-$BR_VER OUT=$BUILD/out-rescue
PACK=1
[[ ${1:-} == --no-pack ]] && PACK=0

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
# Cross-check against the hash upstream publishes (best effort; offline is not fatal).
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

echo "== configuring"
make -C "$SRC" O="$OUT" BR2_EXTERNAL="$Y2/buildroot" y2_rescue_defconfig
echo "== building"
make -C "$OUT" -j"$(nproc)"

CPIO=$OUT/images/rootfs.cpio.gz
[[ -f $CPIO ]] || die "no $CPIO"
echo "rootfs.cpio.gz: $(stat -c %s "$CPIO") bytes"
((PACK)) || exit 0

# Repack: keep kernel, header fields and MTK headers of the stock image; swap the ramdisk only.
# `unpack` stores the zero padding after the Android image as 'trailer'; dropping it trims the
# image to the length its own header describes.
pack() { # stock.bin scratchdir out.img expected-ramdisk-MTK-name
    local stock=$1 scratch=$2 out=$3 name=$4
    [[ -f $stock ]] || die "missing $stock"
    rm -rf "$scratch"; mkdir -p "$scratch"
    python3 -I "$TOOL" unpack "$stock" "$scratch"
    [[ $(python3 -I "$TOOL" info "$stock" | grep -c "MTK name='$name'") -ge 1 ]] || die "$stock ramdisk MTK name is not $name"
    rm -f "$scratch/trailer"
    cp "$CPIO" "$scratch/ramdisk.cpio.gz"
    python3 -I "$TOOL" repack --force "$scratch" "$out"
    local isz rsz
    isz=$(stat -c %s "$out") rsz=$(stat -c %s "$scratch/ramdisk.cpio.gz")
    ((isz <= MAX_IMAGE)) || die "$out is $isz bytes, over 16 MiB"
    ((rsz <= MAX_RAMDISK)) || die "ramdisk is $rsz bytes, over 8 MiB"
    python3 -I "$TOOL" info "$out" | tee "$out.info.txt"
    grep "sha1 OK" "$out.info.txt" >/dev/null || die "$out: sha1 check failed"
    echo "$(sha "$out")  $out ($isz bytes)"
}

echo "== packing RECOVERY variant"
pack "$BACKUP/partitions/RECOVERY.bin" "$BUILD/scratch-recovery" "$BUILD/y2-rescue-recovery.img" RECOVERY
echo "== packing BOOTIMG variant"
pack "$BACKUP/partitions/BOOTIMG.bin" "$BUILD/scratch-boot" "$BUILD/y2-rescue-boot.img" ROOTFS
echo "== done"
