#!/usr/bin/env python3
"""Regenerate tests/assets/art/*.jpg (needs Pillow and the libjpeg `cjpeg` tool for the restart-marker file).

Every picture is four flat quadrants (top-left red, top-right green, bottom-left blue, bottom-right yellow) so the tests can
check the decoder's orientation, scaling and colour at the quadrant centres. Run from the repository root:
    python3 tests/tools/gen_art_assets.py
"""
import os
import subprocess
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "art")
COL = [(220, 40, 40), (40, 200, 60), (50, 60, 220), (230, 210, 40)]


def quadrants(w, h, gray=False):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            c = COL[(1 if x >= w // 2 else 0) + (2 if y >= h // 2 else 0)]
            # a gentle ramp so the files are not trivially tiny, small enough to keep the centres within tolerance
            d = (x * 7 + y * 3) % 17 - 8
            px[x, y] = tuple(max(0, min(255, v + d)) for v in c)
    return im.convert("L") if gray else im


def save(name, im, **kw):
    path = os.path.join(OUT, name)
    im.save(path, "JPEG", **kw)
    print(name, im.size, os.path.getsize(path), "bytes")


os.makedirs(OUT, exist_ok=True)
save("q420_600x600.jpg", quadrants(600, 600), quality=70, subsampling=2)  # 4:2:0
save("q420_500x333.jpg", quadrants(500, 333), quality=70, subsampling=2)
save("q444_128x128.jpg", quadrants(128, 128), quality=80, subsampling=0)  # 4:4:4
save("q422_517x389.jpg", quadrants(517, 389), quality=75, subsampling=1)  # 4:2:2, odd size
save("q420_1000x700.jpg", quadrants(1000, 700), quality=60, subsampling=2)
save("q420_40x30.jpg", quadrants(40, 30), quality=90, subsampling=2)
save("qgray_200x150.jpg", quadrants(200, 150, gray=True), quality=80)
save("qprog_300x300.jpg", quadrants(300, 300), quality=70, subsampling=2, progressive=True)  # must be refused
# restart markers every 8 MCUs, made with libjpeg's cjpeg
ppm = os.path.join(OUT, "_tmp.ppm")
quadrants(257, 193).save(ppm)
with open(os.path.join(OUT, "qrst_257x193.jpg"), "wb") as f:
    subprocess.run(["cjpeg", "-quality", "75", "-sample", "2x2", "-restart", "8", ppm], stdout=f, check=True)
os.remove(ppm)
print("qrst_257x193.jpg")
