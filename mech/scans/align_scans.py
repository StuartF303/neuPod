"""Align the raw Metro X scans to the neuPod datum and write decimated reference meshes.

    python mech/scans/align_scans.py <raw-scan-folder> [--faces 12000]

Writes mech/scans/aligned/<name>.stl in the datum frame (origin midway between the
locating pins, +X right seen from the front, +Y toward the LCD, +Z toward the glass,
Z = 0 at the board's rear face), using the current mech/params.py.

Steps per scan: keep the largest connected piece → principal axes → fit the dominant
plane (robust SVD) and level it → square the outline to X/Y (min-area rectangle) →
a fixed per-scan placement (flips/rotations found by inspection, see PLACEMENT) →
translate onto the datum features → decimate.

Raw files (6 Nov 2026, Stuart's OneDrive neuPod folder):
    frame-new1-btm_mesh.stl        frame, FRONT face (board side)
    1106_03frame_top_new_mesh.stl  frame, REAR face
    pcb_top_mesh.stl               logic board, WHEEL face (holes filled by processing)
    back_in_mesh.stl               back shell inside (polished floor did not scan; rim, walls, jack, insert did)
    back-out_mesh.stl              back shell outside (standing on four feet; feet removed)
Needs: trimesh, scipy, numpy, fast-simplification (optional, for decimation).
"""
import sys
from pathlib import Path

import numpy as np
import trimesh
from scipy.spatial import ConvexHull

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1]))
from mech import params as p  # noqa: E402

RAW = {
    "frame_front": "frame-new1-btm_mesh.stl",
    "frame_rear": "1106_03frame_top_new_mesh.stl",
    "pcb_wheel_face": "pcb_top_mesh.stl",
    "back_shell_in": "back_in_mesh.stl",
    "back_shell_out": "back-out_mesh.stl",
}


def largest(m):
    return max(m.split(only_watertight=False), key=lambda q: len(q.faces))


def principal(m):
    m = m.copy()
    m.apply_transform(np.linalg.inv(m.bounding_box_oriented.primitive.transform))
    order = np.argsort(m.extents)                      # short, mid, long → Z, X, Y
    P = np.eye(4)[[order[1], order[2], order[0], 3]]
    if np.linalg.det(P[:3, :3]) < 0:
        P[0] *= -1
    m.apply_transform(P)
    return m


def level(m, keep=0.5):
    v = m.vertices
    idx = np.arange(len(v))
    for _ in range(4):
        c = v[idx].mean(0)
        n = np.linalg.svd(v[idx] - c, full_matrices=False)[2][2]
        idx = np.argsort(np.abs((v - c) @ n))[: int(len(v) * keep)]
    if n[2] < 0:
        n = -n
    R = trimesh.geometry.align_vectors(n, [0, 0, 1])
    m = m.copy()
    m.apply_translation(-c)
    m.apply_transform(R)
    h = m.vertices[ConvexHull(m.vertices[:, :2]).vertices, :2]
    best = (1e18, 0.0)
    for i in range(len(h)):
        e = h[(i + 1) % len(h)] - h[i]
        a = np.arctan2(e[1], e[0])
        q = h @ np.array([[np.cos(a), np.sin(a)], [-np.sin(a), np.cos(a)]]).T
        area = np.ptp(q[:, 0]) * np.ptp(q[:, 1])
        if area < best[0]:
            best = (area, a)
    m.apply_transform(trimesh.transformations.rotation_matrix(-best[1], [0, 0, 1]))
    if m.extents[0] > m.extents[1]:
        m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [0, 0, 1]))
    b = m.bounds
    m.apply_translation([-(b[0, 0] + b[1, 0]) / 2, -(b[0, 1] + b[1, 1]) / 2, 0])
    return m


def rot(axis, deg):
    return trimesh.transformations.rotation_matrix(np.radians(deg), axis)


# Per-scan placement after level(): (rotations, translation) chosen by inspection of the height maps.
# Scan Z values quoted are in the levelled scan frame.
def placement(name):
    fy = p.FRAME_CY
    if name == "frame_front":      # rib front face at z −1.5 → RIB_Z_TOP
        return [], (0.0, fy, p.RIB_Z_TOP + 1.5)
    if name == "frame_rear":       # seen from behind: flip about Y; rib rear face then at z −1.5 → RIB_Z_BACK
        return [rot([0, 1, 0], 180)], (0.0, fy, p.RIB_Z_BACK + 1.5)
    if name == "pcb_wheel_face":   # long side is X; wheel nearer the TOP edge; centre switch (+0.53, +5.24 from the
        # levelled scan centre after the two rotations) → (WHEEL_CX, WHEEL_CY); board plane z 0 → BOARD_FRONT_Z
        return [rot([0, 0, 1], 90), rot([0, 0, 1], 180)], (p.WHEEL_CX - 0.53, p.WHEEL_CY - 5.24, p.BOARD_FRONT_Z)
    if name == "back_shell_in":    # front view; rim at z +2.0 → Z_RIM
        return [], (0.0, fy, p.Z_RIM - 2.0)
    if name == "back_shell_out":   # seen from behind: flip about Y; back face then at z ≈ −0.3 → Z_BACK_OUT
        return [rot([0, 1, 0], 180)], (0.0, fy, p.Z_BACK_OUT + 0.3)
    raise KeyError(name)


def main(raw_dir, faces=12000):
    out = HERE / "aligned"
    out.mkdir(exist_ok=True)
    for name, fn in RAW.items():
        f = Path(raw_dir) / fn
        if not f.exists():
            print("missing", f)
            continue
        m = largest(trimesh.load(f))
        if name == "back_shell_out":                  # drop the four feet it stood on
            m = level(principal(m), keep=0.4)
            m.update_faces(m.triangles_center[:, 2] > -3.5)
            m.remove_unreferenced_vertices()
            m = largest(m)
            m = level(m, keep=0.4)
        else:
            m = level(principal(m), keep=0.3 if name != "pcb_wheel_face" else 0.5)
        rots, t = placement(name)
        for R in rots:
            m.apply_transform(R)
        m.apply_translation(t)
        if faces and len(m.faces) > faces:
            try:
                m = m.simplify_quadric_decimation(face_count=faces)
            except Exception as e:  # fast-simplification missing
                print("  (no decimation:", e, ")")
        m.export(out / f"{name}.stl")
        print(f"{name:16s} {len(m.faces):6d} faces  bounds {np.round(m.bounds, 1).tolist()}")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    nf = 12000
    if "--faces" in sys.argv:
        nf = int(sys.argv[sys.argv.index("--faces") + 1])
    main(args[0] if args else ".", nf)
