import sys, json
import numpy as np, cv2
from scipy import ndimage
import cadquery as cq
from pathlib import Path
HERE = Path(__file__).resolve().parent
d = np.load(HERE / 'quant.npz')
occ, QF, QR, lf, lr, xe, ye = [d[k] for k in ('occ', 'QF', 'QR', 'lf', 'lr', 'xe', 'ye')]
res = xe[1] - xe[0]
# per-region medians snap each region to its own measured height (keeps true values, not cluster centres)
HF, HR = d['HFf'], d['HRf']
ZF = np.full(QF.shape, np.nan); ZR = np.full(QR.shape, np.nan)
for Q, H, Z in ((QF, HF, ZF), (QR, HR, ZR)):
    for c in np.unique(Q[occ]):
        lab, n = ndimage.label(occ & (Q == c))
        for i in range(1, n + 1):
            m = lab == i
            if m.sum() < 30:
                Z[m] = np.median(H[occ & (Q == c)])
            else:
                z = float(np.median(H[m]))
                lv = lf if Q is QF else lr
                j = np.argmin(np.abs(lv - z))
                Z[m] = lv[j] if abs(lv[j] - z) < 0.15 else np.round(z / 0.05) * 0.05   # snap to a measured level
# small-region cleanup on the (ZF, ZR) pair
levels = np.unique(np.concatenate([ZF[occ], ZR[occ]]))
# merge levels closer than 0.05 mm (front/rear clusters landing 0.01 apart) and snap the maps to them
merged = []
for z in levels:
    if merged and z - merged[-1][-1] < 0.05:
        merged[-1].append(z)
    else:
        merged.append([z])
snap = {z: float(np.mean(g)) for g in merged for z in g}
for Z in (ZF, ZR):
    Z[occ] = np.vectorize(snap.get)(Z[occ])
levels = np.unique(np.concatenate([ZF[occ], ZR[occ]]))
print('distinct Z values', len(levels))
def to_xy(cnt):
    pts = cnt[:, 0, :].astype(float)
    return np.stack([xe[0] + (pts[:, 0] + 0.5) * res, ye[0] + (pts[:, 1] + 0.5) * res], 1)
from shapely.geometry import Polygon, MultiPolygon
from shapely.ops import unary_union


def mask_polys(m):
    """raster mask -> list of valid shapely polygons (simplified, topology preserved)."""
    img = m.astype(np.uint8) * 255
    cnts, hier = cv2.findContours(img, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    if hier is None:
        return []
    hier = hier[0]
    polys = []
    for k, cn in enumerate(cnts):
        if hier[k][3] != -1 or len(cn) < 3:
            continue
        holes = []
        ch = hier[k][2]
        while ch != -1:
            if len(cnts[ch]) >= 3:
                holes.append(to_xy(cnts[ch]))
            ch = hier[ch][0]
        pg = Polygon(to_xy(cn), holes).buffer(0)
        polys.append(pg)
    g = unary_union(polys).simplify(0.08, preserve_topology=True).buffer(0)
    geoms = list(g.geoms) if isinstance(g, MultiPolygon) else [g]
    return [q for q in geoms if q.area > 0.4]


def prism(pg, z0, z1):
    ow = cq.Wire.makePolygon([cq.Vector(x, y, 0) for x, y in list(pg.exterior.coords)[:-1]], close=True)
    hs = [cq.Wire.makePolygon([cq.Vector(x, y, 0) for x, y in list(r.coords)[:-1]], close=True) for r in pg.interiors if Polygon(r).area > 0.3]
    f = cq.Face.makeFromWires(ow, hs)
    return cq.Solid.extrudeLinear(f, cq.Vector(0, 0, z1 - z0)).translate(cq.Vector(0, 0, z0))


slabs = []
OV = 0.02
for a, b in zip(levels[:-1], levels[1:]):
    if b - a < 0.02:
        continue
    zm = (a + b) / 2
    m = occ & (ZR < zm) & (ZF > zm)
    m = ndimage.binary_opening(m, iterations=1)
    if m.sum() < 20:
        continue
    for pg in mask_polys(m):
        try:
            sld = prism(pg, a - OV, b + OV)
            if sld.isValid():
                slabs.append(sld)
            else:
                print('invalid prism at', round(a, 2))
        except Exception as e:
            print('prism fail', e)
print('prisms', len(slabs))
fused = slabs[0].fuse(*slabs[1:])
try:
    fused = fused.clean()
except Exception:
    pass
print('valid', fused.isValid(), 'solids', len(fused.Solids()), 'volume', round(fused.Volume(), 1))
bb = fused.BoundingBox(); print('bbox', [round(v, 2) for v in (bb.xmin, bb.ymin, bb.zmin, bb.xmax, bb.ymax, bb.zmax)])
cq.exporters.export(cq.Workplane().add(fused), str(HERE / 'frame_from_scan.step'))
cq.exporters.export(cq.Workplane().add(fused), str(HERE / 'frame_from_scan.stl'), tolerance=0.02)
