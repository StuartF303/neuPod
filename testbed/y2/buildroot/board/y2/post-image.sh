#!/usr/bin/env bash
# Buildroot post-image hook: refuse an ext4 image larger than what LK reports usable for USRDATA.
set -euo pipefail
IMG=$1/rootfs.ext4
MAX=$((0x31f00000))
[[ -f $IMG ]] || { echo "y2 post-image: $IMG missing" >&2; exit 1; }
SZ=$(stat -L -c %s "$IMG")
((SZ <= MAX)) || { echo "y2 post-image: $IMG is $SZ bytes, over USRDATA's $MAX" >&2; exit 1; }
echo "y2 post-image: rootfs.ext4 $SZ bytes (limit $MAX)"
