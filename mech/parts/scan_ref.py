"""Load a Metro X scan (STL/OBJ) as a reference body next to the parametric part.

Usage:
    from mech.parts.scan_ref import load_scan
    ref = load_scan("midframe")          # mech/scans/midframe.stl (or .obj)

Alignment: edit SCAN_ALIGN below once you have fitted the scan to the datum
(the two locating pins define X and the origin; the board rear face is Z=0).
Values are (dx, dy, dz, rx, ry, rz) — translation in mm, then rotations in
degrees about the global axes.  The scan is treated as a mesh shell, so it is
for visual overlay and bounding-box checks, not for booleans.
"""
from pathlib import Path
import cadquery as cq
from .common import Part

SCAN_DIR = Path(__file__).resolve().parents[1] / "scans"

SCAN_ALIGN = {
    "midframe": (0, 0, 0, 0, 0, 0),
    "back-shell": (0, 0, 0, 0, 0, 0),
    "front-assembly": (0, 0, 0, 0, 0, 0),
    "logic-board-top": (0, 0, 0, 0, 0, 0),
    "logic-board-bottom": (0, 0, 0, 0, 0, 0),
}


def _import_mesh(path: Path):
    """STL via OCP RWStl; OBJ via a tiny parser → cq.Shell of faces (triangles)."""
    from OCP.RWStl import RWStl
    from OCP.TopoDS import TopoDS_Face
    from OCP.BRep import BRep_Builder
    from OCP.TCollection import TCollection_AsciiString
    if path.suffix.lower() == ".stl":
        tri = RWStl.ReadFile_s(TCollection_AsciiString(str(path)))
        face = TopoDS_Face()
        BRep_Builder().MakeFace(face, tri)
        return cq.Shape.cast(face)
    raise ValueError("convert OBJ to STL first (meshlab / trimesh); only STL is wired up")


def load_scan(name: str):
    for ext in (".stl", ".STL"):
        path = SCAN_DIR / f"{name}{ext}"
        if path.exists():
            break
    else:
        return None
    shape = _import_mesh(path)
    dx, dy, dz, rx, ry, rz = SCAN_ALIGN.get(name, (0,) * 6)
    wp = cq.Workplane("XY").add(shape)
    for axis, ang in (((1, 0, 0), rx), ((0, 1, 0), ry), ((0, 0, 1), rz)):
        if ang:
            wp = wp.rotate((0, 0, 0), axis, ang)
    wp = wp.translate((dx, dy, dz))
    return Part(f"scan_{name}", wp, (0.9, 0.5, 0.9), "scan", notes=f"reference mesh {path.name}")
