"""Merge the front and rear Metro X scans of the mid-frame into one registered pair,
levelled on the rib (board-seat) face, and write the height maps build_slabs.py needs.

    python mech/frame/merge_frame.py <raw-scan-folder>     # then: python mech/frame/build_slabs.py

Steps (6 Nov scans: frame-new1-btm_mesh.stl = front face, 1106_03frame_top_new_mesh.stl = rear face):
1. Largest connected piece → principal axes → robust plane level → square to X/Y (mech/scans/align_scans.py).
2. Flip the rear scan about Y (it was scanned from behind), put each scan's outermost face at Z = 0.
3. Rigid ICP on the side-wall samples both halves see (≈5 mm of overlap). From starts of 6.6–8.5 mm it
   converges to a 6.97 mm overall depth, mean residual 0.14 mm.
4. Re-level both on a robust plane fit to the rib grid's front face (the board seat), Z = 0 at the
   front-most face.
5. Front height map (max Z of surfaces facing +Z) and rear map (min Z of surfaces facing −Z) on a
   0.15 mm grid → hmaps.npz.  levels.py clusters each into flat levels → quant.npz.
"""
import sys
from pathlib import Path
import numpy as np
import trimesh
from scipy import ndimage
from scipy.stats import binned_statistic_2d

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE)); sys.path.insert(0, str(HERE.parent / "scans"))
from align_scans import largest, principal, level          # noqa: E402
from levels import cluster_levels, quantize, fill_nan_nearest, mode_filter  # noqa: E402


def main(raw):
    F = level(principal(largest(trimesh.load(Path(raw) / "frame-new1-btm_mesh.stl"))), keep=0.3)
    R = level(principal(largest(trimesh.load(Path(raw) / "1106_03frame_top_new_mesh.stl"))), keep=0.3)
    R.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [0, 1, 0]))
    F.apply_translation([0, 0, -F.bounds[1, 2]]); R.apply_translation([0, 0, -R.bounds[0, 2]])
    side = lambda m, n: (lambda p, fi: p[np.abs(m.face_normals[fi, 2]) < 0.35])(*trimesh.sample.sample_surface(m, n))
    init = np.eye(4); init[2, 3] = -7.0
    M, _, cost = trimesh.registration.icp(side(R, 15000), side(F, 600000), initial=init, threshold=1e-6,
                                          max_iterations=80, reflection=False, scale=False)
    R.apply_transform(M)
    print(f"registered: overall depth {-M[2, 3]:.2f} mm, residual {cost:.3f}")
    c = trimesh.util.concatenate([F, R]).bounds.mean(0)
    for m in (F, R):
        m.apply_translation([-c[0], -c[1], 0])
    res = 0.15
    xe = np.arange(-30.4, 30.4 + res, res); ye = np.arange(-50.9, 50.9 + res, res)

    def hmap(m, stat):
        p, fi = trimesh.sample.sample_surface(m, 1500000)
        up = m.face_normals[fi, 2] > 0.2 if stat == "max" else m.face_normals[fi, 2] < -0.2
        H, _, _, _ = binned_statistic_2d(p[up, 0], p[up, 1], p[up, 2], statistic=stat, bins=[xe, ye])
        return H.T
    HF = hmap(F, "max")
    xc = (xe[:-1] + xe[1:]) / 2; yc = (ye[:-1] + ye[1:]) / 2; X, Y = np.meshgrid(xc, yc)
    # level on the rib grid front face (lower half, central band of Z values)
    m = np.isfinite(HF) & (Y < -3) & (Y > -44) & (np.abs(X) < 26)
    z = HF[m]; band = np.abs(z - np.median(z)) < 0.6
    A = np.c_[X[m][band], Y[m][band], np.ones(band.sum())]
    coef, *_ = np.linalg.lstsq(A, z[band], rcond=None)
    Rz = trimesh.geometry.align_vectors(np.array([-coef[0], -coef[1], 1.0]) / np.linalg.norm([-coef[0], -coef[1], 1.0]), [0, 0, 1])
    for mm in (F, R):
        mm.apply_transform(Rz)
    top = max(F.bounds[1, 2], R.bounds[1, 2])
    for mm in (F, R):
        mm.apply_translation([0, 0, -top])
    F.export(HERE / "front_lev2.stl"); R.export(HERE / "rear_lev2.stl")
    HF, HR = hmap(F, "max"), hmap(R, "min")
    occ = np.isfinite(HF) | np.isfinite(HR)
    occ = ndimage.binary_opening(ndimage.binary_closing(occ, iterations=2), iterations=1)
    lab, n = ndimage.label(occ); sizes = ndimage.sum(occ, lab, range(1, n + 1)); occ = np.isin(lab, 1 + np.where(sizes > 200)[0])
    HFf, HRf = fill_nan_nearest(HF, occ), fill_nan_nearest(HR, occ)
    lf, lr = cluster_levels(HFf[occ], bw=0.2), cluster_levels(HRf[occ], bw=0.2)
    QF, QR = mode_filter(quantize(HFf, lf), occ, 5), mode_filter(quantize(HRf, lr), occ, 5)
    np.savez(HERE / "quant.npz", occ=occ, QF=QF, QR=QR, lf=lf, lr=lr, xe=xe, ye=ye, HFf=HFf, HRf=HRf)
    print("front levels", np.round(lf, 2), "\nrear levels", np.round(lr, 2))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
