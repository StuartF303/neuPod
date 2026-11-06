"""Shared helpers for part builders."""
from dataclasses import dataclass, field
import cadquery as cq


@dataclass
class Part:
    name: str
    shape: cq.Workplane
    colour: tuple           # (r, g, b) 0..1
    group: str              # 'shell' | 'frame' | 'board' | 'front' | 'cavity' | 'candidate' | 'region'
    fixed: bool = True      # stock geometry (True) or a candidate placement (False)
    notes: str = ""
    owner: str = None       # board assembly this part belongs to (PCB + its components/connectors), or "link"
    bbox: tuple = field(default=None)

    def __post_init__(self):
        bb = self.shape.val().BoundingBox()
        self.bbox = (bb.xmin, bb.ymin, bb.zmin, bb.xmax, bb.ymax, bb.zmax)

    def volume(self):
        return self.shape.val().Volume()


def box(x0, y0, z0, x1, y1, z1):
    """Axis-aligned box between two corners (any order)."""
    xa, xb = sorted((x0, x1)); ya, yb = sorted((y0, y1)); za, zb = sorted((z0, z1))
    return (cq.Workplane("XY").box(xb - xa, yb - ya, zb - za, centered=False)
            .translate((xa, ya, za)))


def box_at(cx, cy, z0, size, h=None):
    """Box of footprint size=(X, Y[, H]) centred on (cx, cy), from z0 upward by h (negative h → downward)."""
    sx, sy = size[0], size[1]
    if h is None:
        h = size[2]
    return box(cx - sx / 2, cy - sy / 2, z0, cx + sx / 2, cy + sy / 2, z0 + h)


def cyl(cx, cy, z0, d, h):
    return cq.Workplane("XY").circle(d / 2).extrude(h).translate((cx, cy, z0))


def rounded_box(x0, y0, z0, x1, y1, z1, r):
    xa, xb = sorted((x0, x1)); ya, yb = sorted((y0, y1)); za, zb = sorted((z0, z1))
    return (cq.Workplane("XY").rect(xb - xa, yb - ya).extrude(zb - za)
            .edges("|Z").fillet(r).translate(((xa + xb) / 2, (ya + yb) / 2, za)))
