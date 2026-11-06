"""Export stock + every layout as compact mesh JSON for the web viewer.

    python -m mech.export_web out.json

Shared (fixed) parts are stored once; each layout lists the part keys it uses.
Vertices are rounded to 0.01 mm.
"""
import json
import sys

from .assembly import build
from . import layouts as L
from . import params as p


def mesh(shape, tol=0.05, ang=0.4):
    verts, tris = shape.val().tessellate(tol, ang)
    v = []
    for q in verts:
        v += [round(q.x, 2), round(q.y, 2), round(q.z, 2)]
    i = [k for t in tris for k in t]
    return v, i


def main(out):
    parts_db, layouts = {}, {}
    for nm in ["stock"] + list(L.LAYOUTS):
        parts, meta = build(nm)
        keys = []
        for pt in parts:
            key = pt.name if pt.fixed else f"{nm}:{pt.name}"
            if key not in parts_db:
                v, i = mesh(pt.shape)
                if not i:
                    continue
                parts_db[key] = dict(name=pt.name, c=[round(x, 3) for x in pt.colour], g=pt.group,
                                     fixed=pt.fixed, notes=pt.notes, v=v, i=i)
            keys.append(key)
        layouts[nm] = dict(parts=keys, battery=meta.get("battery"), batt_where=meta.get("batt_where"), boards=meta.get("boards", {}),
                           links=meta.get("links", []))
    # datum-aligned scans as reference meshes (shared by every layout)
    import trimesh
    from pathlib import Path
    scans = {}
    sdir = Path(__file__).resolve().parent / "scans" / "aligned"
    for f in (sorted(sdir.glob("*.stl")) if p.DEPTH_MODEL == "derived" else []):   # aligned with the derived stack
        m = trimesh.load(f)
        if len(m.faces) > 6000:
            try:
                m = m.simplify_quadric_decimation(face_count=6000)
            except Exception:
                pass
        scans[f.stem] = dict(name="scan " + f.stem, c=[0.85, 0.45, 0.85], g="scan", fixed=True,
                             notes="Metro X scan, datum-aligned (reference)",
                             v=[round(float(x), 2) for x in m.vertices.ravel()], i=[int(k) for k in m.faces.ravel()])
    data = dict(parts=parts_db, layouts=layouts, scans=scans, depth_model=p.DEPTH_MODEL,
                z=dict(rib_back=p.RIB_Z_BACK, back_in=p.Z_BACK_IN, front_in=p.Z_FRONT_IN))
    with open(out, "w") as f:
        json.dump(data, f, separators=(",", ":"))
    print(out, len(parts_db), "meshes")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "mech/out/neupod_web.json")
