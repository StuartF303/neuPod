import numpy as np
from scipy import ndimage

def cluster_levels(v, bw=0.25, min_frac=0.01):
    """1-D mean-shift on Z values -> sorted level centres."""
    v = v[np.isfinite(v)]
    h, e = np.histogram(v, bins=np.arange(v.min() - 0.05, v.max() + 0.1, 0.05))
    c = (e[:-1] + e[1:]) / 2
    seeds = c[h > 0]
    for _ in range(30):
        new = []
        for s in seeds:
            w = h * (np.abs(c - s) < bw)
            new.append((w * c).sum() / max(w.sum(), 1e-9))
        seeds = np.array(new)
    lv = []
    for s in np.sort(seeds):
        if not lv or abs(s - lv[-1]) > bw * 0.8:
            lv.append(s)
    lv = np.array(lv)
    # drop levels with tiny support
    lab = np.argmin(np.abs(v[:, None] - lv[None]), 1)
    keep = [i for i in range(len(lv)) if (lab == i).mean() > min_frac]
    return lv[keep]

def quantize(H, lv):
    Q = np.full(H.shape, -1, int)
    m = np.isfinite(H)
    Q[m] = np.argmin(np.abs(H[m][:, None] - lv[None]), 1)
    return Q

def fill_nan_nearest(H, mask):
    """fill NaNs inside mask from the nearest valid cell"""
    valid = np.isfinite(H)
    idx = ndimage.distance_transform_edt(~valid, return_distances=False, return_indices=True)
    out = H[tuple(idx)]
    out[~mask] = np.nan
    return out

def mode_filter(Q, mask, size=5):
    out = Q.copy()
    labels = np.unique(Q[mask])
    counts = np.stack([ndimage.uniform_filter((Q == l).astype(float), size) for l in labels])
    best = labels[np.argmax(counts, 0)]
    out[mask] = best[mask]
    return out
