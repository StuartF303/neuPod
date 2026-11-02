#!/usr/bin/env python3
"""Generate the display test image: app/tests/assets/test_image_640x480.png.

A smooth photo-like sky/ground gradient with a vignette, a colour wheel (left) and an asymmetric
arrow + corner marks so mirroring or rotation is obvious. Pillow only (no numpy).
Then convert with:
  python3 tools/png2rgb565.py app/tests/assets/test_image_640x480.png \\
      -o app/tests/assets/test_image.c --name test_img --downscale2 --width 320 --height 240
"""
import colorsys
import math
import os

from PIL import Image, ImageDraw

W, H = 640, 480
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "app", "tests", "assets", "test_image_640x480.png")


def lerp(a, b, t):
    return a + (b - a) * t


def main():
    im = Image.new("RGB", (W, H))
    px = im.load()
    cx, cy = W / 2, H / 2
    wheel_c, wheel_r = (170, 250), 130
    for y in range(H):
        t = y / (H - 1)
        horizon = 0.55
        if t < horizon:
            k = t / horizon
            base = (lerp(30, 150, k), lerp(70, 200, k), lerp(160, 245, k))
        else:
            k = (t - horizon) / (1 - horizon)
            base = (lerp(70, 20, k), lerp(130, 55, k), lerp(60, 30, k))
        for x in range(W):
            dx, dy = (x - cx) / cx, (y - cy) / cy
            vig = 1.0 - 0.55 * min(1.0, (dx * dx + dy * dy) / 2.0)
            r, g, b = (c * vig for c in base)
            ddx, ddy = x - wheel_c[0], y - wheel_c[1]
            d = math.hypot(ddx, ddy)
            if d <= wheel_r:
                hue = (math.atan2(ddy, ddx) / (2 * math.pi)) % 1.0
                val = 1.0 - 0.35 * (1 - d / wheel_r) * 0  # full brightness
                sat = d / wheel_r
                wr, wg, wb = colorsys.hsv_to_rgb(hue, sat, val)
                r, g, b = wr * 255, wg * 255, wb * 255
            px[x, y] = (int(r), int(g), int(b))
    d = ImageDraw.Draw(im)
    # Asymmetric arrow pointing right (right half) -- mirror/rotation check.
    d.polygon([(380, 200), (560, 200), (560, 160), (620, 250), (560, 340), (560, 300), (380, 300)],
              fill=(255, 255, 255), outline=(0, 0, 0))
    d.rectangle([380, 380, 470, 440], fill=(255, 0, 0))     # red
    d.rectangle([480, 380, 570, 440], fill=(0, 255, 0))     # green
    d.rectangle([580, 380, 630, 440], fill=(0, 0, 255))     # blue (narrow)
    d.rectangle([0, 0, 40, 40], fill=(255, 255, 0))          # yellow top-left mark
    d.rectangle([W - 20, 0, W - 1, 20], fill=(255, 0, 255))  # magenta top-right mark
    d.rectangle([0, 0, W - 1, H - 1], outline=(255, 255, 255))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    im.save(OUT, optimize=True)
    print("wrote", OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
