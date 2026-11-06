"""Greedy grid placer: puts circuit blocks into the allowed regions of their board.

A board face is a dict:
    face    'rear' (parts grow toward the shell, −Z) or 'front' (+Z)
    z       Z of that face
    regions [(x0, y0, x1, y1, hmax), ...]   areas where parts may sit, with their height limit
The placer keeps occupied rectangles per face, tries every 0.5 mm position in every region (both orientations),
and takes the one closest to the block's target point.  Unplaced blocks are reported, not hidden.
"""
import math

STEP = 0.5
GAP = 0.4          # spacing between blocks


def _overlap(a, b, gap=GAP):
    return not (a[2] + gap <= b[0] or b[2] + gap <= a[0] or a[3] + gap <= b[1] or b[3] + gap <= a[1])


def _inside(r, reg):
    return r[0] >= reg[0] - 1e-6 and r[1] >= reg[1] - 1e-6 and r[2] <= reg[2] + 1e-6 and r[3] <= reg[3] + 1e-6


def region_area(face):
    return sum((r[2] - r[0]) * (r[3] - r[1]) for r in face["regions"])


def place(face, occupied, name, fp, h, target, soft=()):
    """Returns (rect, rotated) or None. occupied: list of rects on this face.
    soft: [(rect, hlimit)] — obstacles under the face (cell, Taptic) that only block blocks taller than hlimit."""
    occupied = list(occupied) + [r for r, hl in soft if h > hl + 1e-6]
    best = None
    for (w, l), rot in (((fp[0], fp[1]), False), ((fp[1], fp[0]), True)):
        for reg in face["regions"]:
            x0, y0, x1, y1, hmax = reg
            if h > hmax + 1e-6 or x1 - x0 < w or y1 - y0 < l:
                continue
            nx = int((x1 - x0 - w) / STEP) + 1
            ny = int((y1 - y0 - l) / STEP) + 1
            for i in range(nx):
                for j in range(ny):
                    r = (x0 + i * STEP, y0 + j * STEP, x0 + i * STEP + w, y0 + j * STEP + l)
                    if not _inside(r, reg) or any(_overlap(r, o) for o in occupied):
                        continue
                    cx, cy = (r[0] + r[2]) / 2, (r[1] + r[3]) / 2
                    d = math.hypot(cx - target[0], cy - target[1])
                    if best is None or d < best[0] - 1e-9:
                        best = (d, r, rot)
    if best is None:
        return None
    return best[1], best[2]
