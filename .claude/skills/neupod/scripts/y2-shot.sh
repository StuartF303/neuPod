#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Screenshot the Y2's screen: reads the visible page of /dev/fb0 (XRGB8888, page-flipped via pan) and writes a PNG.
# Usage: y2-shot.sh [out.png]   (default: y2-<time>.png in the current directory). Needs python3 + Pillow on the host.
set -euo pipefail
Y2_HOST=${Y2_HOST:-root@192.168.7.2}
out=${1:-y2-$(date +%H%M%S).png}
raw=$(mktemp); trap 'rm -f "$raw"' EXIT
geo=$(ssh -o ConnectTimeout=3 "$Y2_HOST" 'cat /sys/class/graphics/fb0/virtual_size /sys/class/graphics/fb0/stride /sys/class/graphics/fb0/pan')
w=$(echo "$geo" | sed -n 1p | cut -d, -f1)
stride=$(echo "$geo" | sed -n 2p)
yoff=$(echo "$geo" | sed -n 3p | cut -d, -f2)
h=${Y2_FB_H:-360}
ssh "$Y2_HOST" "dd if=/dev/fb0 bs=$stride skip=$yoff count=$h 2>/dev/null" >"$raw"
python3 -I - "$raw" "$out" "$w" "$h" "$stride" <<'PY'
import sys
from PIL import Image
raw, out, w, h, stride = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
data = open(raw, 'rb').read()
Image.frombuffer('RGBX', (w, h), data, 'raw', 'BGRX', stride, 1).convert('RGB').save(out)
PY
echo "$out"
