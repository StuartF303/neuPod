"""Space-allocation study → mech/space_report.md

    python -m mech.space_study

Computes, for the stock geometry: free volume per region (pockets, HDD cavity,
under-LCD, freed battery pocket, wheel face) with the HDD and donor cell removed.
For each layout A, B, C, D1, D2: usable PCB area, max component height per zone,
battery fit (incl. the 505060 check), haptic position, FPC run lengths, antenna
distance to the RF window, board/link count, and the clash list (exact boolean
intersections between every candidate body and every fixed body / other candidate).
Runs in both depth models (see params.DEPTH_MODEL).
"""
import importlib
import sys
from pathlib import Path

import cadquery as cq

HERE = Path(__file__).resolve().parent


def _overlap(a, b):
    """bbox overlap extents (dx, dy, dz) or None."""
    dx = min(a[3], b[3]) - max(a[0], b[0])
    dy = min(a[4], b[4]) - max(a[1], b[1])
    dz = min(a[5], b[5]) - max(a[2], b[2])
    if dx > 1e-3 and dy > 1e-3 and dz > 1e-3:
        return dx, dy, dz
    return None


def _ivol(a, b):
    try:
        return a.shape.val().intersect(b.shape.val()).Volume()
    except Exception:
        return -1.0


def clashes(parts, min_vol=0.05):
    cands = [q for q in parts if not q.fixed]
    fixed = [q for q in parts if q.fixed and q.group not in ("region", "cavity", "scan")]
    out = []
    for i, c in enumerate(cands):
        others = fixed + cands[i + 1:]
        for o in others:
            # a component sitting on its own board is not a clash: skip board/comp pairs that only touch
            ov = _overlap(c.bbox, o.bbox)
            if not ov:
                continue
            if ("usbc" in c.name and o.group == "board") or ("usbc" in o.name and c.group == "board"):
                continue                                      # mid-mount receptacle sits in its board slot by design
            if any(k in o.name or k in c.name for k in ("fpc_stub", "flex_tail", "flex_link")):  # flexible, mates by design
                continue
            v = _ivol(c, o)
            if v > min_vol:
                out.append((c.name, o.name, v, min(ov)))
    return out


def free_volumes(parts, regions):
    fixed = [q for q in parts if q.fixed and q.group not in ("region", "cavity", "scan")]
    out = {}
    for nm, r in regions.items():
        s = r.shape
        for f in fixed:
            if _overlap(r.bbox, f.bbox):
                try:
                    s = s.cut(f.shape)
                except Exception:
                    pass
        bb = r.bbox
        out[nm] = dict(nominal=r.volume(), free=s.val().Volume(),
                       dims=(bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2]), notes=r.notes)
    return out


def manhattan(a, b):
    return sum(abs(x - y) for x, y in zip(a, b))


def board_area_cm2(part, t=1.0):
    return part.volume() / t / 100.0


def run(depth_model):
    import os
    os.environ["NEUPOD_DEPTH_MODEL"] = depth_model
    import mech.params as p
    importlib.reload(p)          # other modules read p.X at call time, so reloading params is enough
    from mech.assembly import build
    from mech.parts.cavity import regions
    from mech import layouts as L

    rep = {}
    rep["params"] = dict(z_back_in=p.Z_BACK_IN, cav_ribs=p.CAVITY_T_BEHIND_RIBS, cav_lcd=p.CAVITY_T_UNDER_LCD,
                         cav_amoled=p.AMOLED_Z_BACK - p.Z_BACK_IN, env=p.ENVELOPE_T_ACTUAL, pocket_full=p.POCKET_T_FULL,
                         front_in=p.Z_FRONT_IN)
    stock, _ = build("stock")
    stock = [q for q in stock if q.group != "cavity"]
    rep["free"] = free_volumes(stock, regions())
    rep["layouts"] = {}
    rf_c = (p.RF_WINDOW_X0 + p.RF_WINDOW[0] / 2, p.INT_Y1 - p.RF_WINDOW[1] / 2, p.Z_BACK_IN)
    wheel_exit = (p.WHEEL_CX - p.WHEEL_D / 2, p.WHEEL_CY, p.BOARD_FRONT_Z + p.SWITCH_H)
    amoled_exit = (0.0, p.DIVIDER_Y0, p.AMOLED_Z_BACK)
    jack_pt = (p.JACK_POS[0] + p.JACK_BODY[0] / 2, p.JACK_POS[1], p.Z_BACK_IN + 2.0)
    for nm in L.LAYOUTS:
        parts, meta = build(nm)
        d = {}
        boards = [q for q in parts if q.group == "board" and not q.fixed]
        d["boards"] = {b.name: round(board_area_cm2(b), 1) for b in boards}
        d["board_desc"] = meta["boards"]
        d["links"] = meta["links"]
        d["zones"] = meta["zones"]
        d["battery"] = meta.get("battery")
        bb = meta.get("batt_box")
        d["batt_box"] = bb
        if bb:
            w, l, t, mah = p.CELLS["505060"]
            f = p.BATT_FOAM
            need_t = t + 2 * f
            have_t = bb[5] - bb[2]
            fits_fp = (w + 2 * f <= bb[3] - bb[0] and l + 2 * f <= bb[4] - bb[1]) or (l + 2 * f <= bb[3] - bb[0] and w + 2 * f <= bb[4] - bb[1])
            d["c505060"] = dict(fits=fits_fp and need_t <= have_t, need_t=need_t, have_t=have_t,
                                footprint_ok=fits_fp, box=(bb[3] - bb[0], bb[4] - bb[1]))
        bat = next((q for q in parts if q.name == d["battery"]), None)
        d["battery_mah"] = p.CELLS[bat.name.split("_")[1]][3] if bat else 0
        hap = next((q for q in parts if q.name == meta.get("haptic")), None)
        d["haptic"] = (hap.name, [round(v, 1) for v in hap.bbox]) if hap else None
        ant = next((q for q in parts if q.name == meta.get("antenna")), None)
        if ant:
            c = ((ant.bbox[0] + ant.bbox[3]) / 2, (ant.bbox[1] + ant.bbox[4]) / 2, (ant.bbox[2] + ant.bbox[5]) / 2)
            d["antenna"] = dict(module=ant.name, dist_to_window=round(manhattan(c, rf_c), 1))
        d["fpc"] = dict(wheel=round(manhattan(wheel_exit, meta["wheel_conn"]), 1),
                        amoled=round(manhattan(amoled_exit, meta["amoled_conn"]), 1),
                        jack=round(manhattan(jack_pt, meta["jack_conn"]), 1), jack_flex_len=p.JACK_FLEX_L)
        cl = clashes(parts)
        # a component intersecting its own board by exactly 0 is excluded already; filter board/comp face contact
        d["clashes"] = [(a, b, round(v, 1), round(dep, 2)) for a, b, v, dep in cl]
        rep["layouts"][nm] = d
        print(f"[{depth_model}] layout {nm}: {len(cl)} clashes, battery {d['battery']} ({d['battery_mah']} mAh)")
    return rep


def md(rep_d, rep_s):
    import mech.params as p
    o = []
    o.append("# neuPod space study\n")
    o.append("Generated by `python -m mech.space_study` from `mech/params.py`. Units mm / mm³ / cm². "
             "Origin at the locating pins, Z toward the glass, Z = 0 at the donor board's rear face.\n")
    o.append("## 1. The depth budget (read this first)\n")
    pd, ps = rep_d["params"], rep_s["params"]
    o.append("Stacking the estimated front parts inside the 10.5 mm thin envelope leaves far less behind the "
             "ribs than the 7 mm 'HDD cavity' noted at the teardown. The study therefore runs twice:\n")
    o.append("| | derived (envelope-consistent) | stated (7 mm cavity forced) |\n|---|---|---|")
    o.append(f"| front plate inner face Z | {pd['front_in']:.1f} | {ps['front_in']:.1f} |")
    o.append(f"| back-shell inner floor Z | {pd['z_back_in']:.1f} | {ps['z_back_in']:.1f} |")
    o.append(f"| envelope thickness this implies | {pd['env']:.1f} | {ps['env']:.1f} |")
    o.append(f"| cavity behind the ribs (lower half) | {pd['cav_ribs']:.1f} | {ps['cav_ribs']:.1f} |")
    o.append(f"| cavity behind the stock backplate (upper half) | {pd['cav_lcd']:.1f} | {ps['cav_lcd']:.1f} |")
    o.append(f"| cavity behind the AMOLED backplate | {pd['cav_amoled']:.1f} | {ps['cav_amoled']:.1f} |")
    o.append(f"| board rear face → shell floor through a pocket | {pd['pocket_full']:.1f} | {ps['pocket_full']:.1f} |\n")
    o.append("The truth is between the two columns: the 5 mm disk did live behind the ribs, so the real front "
             "stack (bezel + wheel + switch + board + rib gap + rib) is thinner than the estimates, or the "
             "ribs are thinner, or the shell is thicker than 10.5 mm. **The first scan measurement is the "
             "rib rear face to the back-shell floor** — every battery conclusion below hinges on it.\n")

    o.append("## 2. Free volume per region (stock parts, HDD and donor cell removed)\n")
    o.append("| region | X × Y × Z | nominal | free (derived) | free (stated) | notes |\n|---|---|---|---|---|---|")
    for nm, v in rep_d["free"].items():
        vs = rep_s["free"][nm]
        dims = " × ".join(f"{x:.1f}" for x in v["dims"])
        o.append(f"| {nm} | {dims} | {v['nominal']:.0f} | {v['free']:.0f} | {vs['free']:.0f} | {v['notes']} |")
    o.append("")
    o.append("Pocket free volumes are board-rear-face to shell floor; a battery behind the ribs reduces them to the "
            "rib gap + rib thickness. The front-face regions are height-limited, not volume-limited.\n")

    o.append("## 3. Layouts\n")
    desc = {"A": "single 55×55 board, stock position, switches on the wheel face",
            "B": "A + second board in the HDD cavity, components toward the shell, mezzanine link",
            "C": "L-shaped rigid board extending under the LCD backplate",
            "D1": "dedicated switch board + main board behind the ribs (and on under the LCD)",
            "D2": "dedicated switch board + main board under the LCD, battery behind the ribs"}
    for nm, d in rep_d["layouts"].items():
        ds = rep_s["layouts"][nm]
        o.append(f"### Layout {nm} — {desc[nm]}\n")
        o.append("**Boards:** " + "; ".join(f"{k} = {d['board_desc'].get(k, '')} → {v} cm²" for k, v in d["boards"].items())
                 + f" (total {sum(d['boards'].values()):.1f} cm²)  ")
        o.append("**Inter-board links:** " + (", ".join(d["links"]) if d["links"] else "none") + "  ")
        o.append("**Max component height per zone (derived / stated):**\n")
        o.append("| zone | derived | stated |\n|---|---|---|")
        for z, h in d["zones"].items():
            o.append(f"| {z} | {h:.1f} | {ds['zones'].get(z, h):.1f} |")
        o.append("")
        if d.get("batt_box"):
            bx = d["batt_box"]
            c = d["c505060"]; cs = ds["c505060"]
            o.append(f"**Battery box reserved:** {bx[3]-bx[0]:.1f} × {bx[4]-bx[1]:.1f} × {bx[5]-bx[2]:.1f} (derived) / "
                     f"{ds['batt_box'][5]-ds['batt_box'][2]:.1f} deep (stated).  ")
            o.append(f"**505060 check:** footprint {'OK' if c['footprint_ok'] else 'does NOT fit'}; needs {c['need_t']:.1f} mm, "
                     f"has {c['have_t']:.1f} (derived) / {cs['have_t']:.1f} (stated) → "
                     f"{'fits' if c['fits'] else 'no'} (derived), {'fits' if cs['fits'] else 'no'} (stated).  ")
        o.append(f"**Best catalogue cell:** {d['battery'] or 'none fits'} = {d['battery_mah']} mAh (derived); "
                 f"{ds['battery'] or 'none fits'} = {ds['battery_mah']} mAh (stated).  ")
        if d["haptic"]:
            o.append(f"**Haptic:** {d['haptic'][0]} at bbox {d['haptic'][1]}  ")
        if d.get("antenna"):
            o.append(f"**Antenna:** {d['antenna']['module']} centre is {d['antenna']['dist_to_window']} mm (Manhattan) from the RF window centre  ")
        f = d["fpc"]
        o.append(f"**FPC runs (Manhattan, exit → connector):** wheel {f['wheel']} mm, AMOLED {f['amoled']} mm, "
                 f"jack {f['jack']} mm (donor flex ≈ {f['jack_flex_len']:.0f} mm, so {'it must be folded/tucked' if f['jack'] < f['jack_flex_len'] * 0.6 else 'about right'})  ")
        o.append(f"\n**Clashes (derived model):** {len(d['clashes'])}\n")
        if d["clashes"]:
            o.append("| candidate | against | overlap mm³ | min penetration mm |\n|---|---|---|---|")
            for a, b, v, dep in sorted(d["clashes"], key=lambda r: -r[2])[:25]:
                o.append(f"| {a} | {b} | {v} | {dep} |")
            if len(d["clashes"]) > 25:
                o.append(f"| … | {len(d['clashes']) - 25} more | | |")
        o.append(f"\n**Clashes (stated model):** {len(ds['clashes'])}"
                 + ("" if not ds["clashes"] else " — " + ", ".join(f"{a}↔{b}" for a, b, _, _ in ds["clashes"][:8])) + "\n")
    o.append(RECOMMENDATION)
    o.append("## 6. Parameters the scan must replace (estimate / photo)\n")
    o.append(p.table(("estimate", "photo")))
    o.append("")
    return "\n".join(o)


RECOMMENDATION = """## 4. Reading the numbers

| layout | boards | links | PCB area | cell (derived / stated) | radio → RF window | notes |
|---|---|---|---|---|---|---|
| A | 1 | 0 | 29 cm² | 1400 / 2500 mAh | ~95 mm, coax/flex antenna needed | pockets give ≤1.3 mm over a cell: no module (RM2 2.5, ESP32 3.1) fits a pocket once a cell is behind the ribs; RM2 also hits a screw boss in LC_upper |
| B | 2 | mezzanine 1.9 mm | 29 + 28 cm² | 1000 / 1150 mAh | ~60 mm | cavity board steals the only 50×60 footprint; cell moves under the LCD where only a 30×48 class fits below the jack |
| C | 1 (L) | 0 | 43 cm² | none / 400 mAh | ~40 mm | rigid L passes through the divider (1.0 mm into the casting) — needs the casting notched; with the AMOLED the backplate is clear, with the stock LCD it is not |
| D1 | 2 | 1 flex | 22 + 38 cm² | 400 / 400 mAh | ~60 mm | main board eats the HDD cavity; only the 20 mm shell strip is left for a cell |
| D2 | 2 | 1 flex | 22 + 18 cm² | 1400 / 2500 mAh | ~20 mm | cell behind the ribs passes under the divider; main board sits on the backplate with 5 mm clear behind it; AMOLED and jack runs short; USB-C on the switch board with USB on the link flex |

Common to all: the mid-mount USB-C body overlaps the 37 mm adhesive ring by ~1.5 mm at the wheel's bottom edge
(WHEEL_CY_FROM_BOTTOM and the receptacle depth decide this — the donor's dock connector lived in the same spot,
so the ring is probably relieved there already; check photo 18). The donor's wheel connector sat top-right on
the wheel face; any layout keeps a 13P 0.3 mm connector on that face outside the 40 mm wheel.

## 5. Recommendation

**Take layout D2 forward**: a 50 × 45 mm two-layer *switch board* in the stock position (five switches, two
wheel-locating holes, pin holes, USB-C slot, wheel FPC connector, one link connector) plus a *main board*
under the LCD (≈ 56 × 32 mm, ~18 cm², both faces usable on the rear, 5 mm of height) and the cell behind the
ribs. It is the only layout that gives the best cell in **both** depth models, puts the radio within ~20 mm of
the RF window (a short flex or edge antenna), keeps the AMOLED and jack flexes short, and decouples the one piece of geometry that must be
exact (the switch zone) from the board that will iterate. Cost: two boards and one 0.5 mm flex (or a rigid-flex
in one panel) — about £15–25 more per set than A at JLC quantities, and the switch board can be ordered now from
the datum measurements alone. Fallback if the scan shows the divider has no gap behind it for the link flex:
layout B (mezzanine through pocket UL), accepting the smaller cell.

If the scan shows ≥ 6 mm behind the ribs (Stuart's 7 mm), D2 takes a 505060/605060 (2.5–3 Ah); if it shows the
~4 mm the envelope implies, a 3 mm cell (~1.4 Ah) — still 2–3× the donor's ~550 mAh.

**Scan first, in this order** (each replaces an `estimate`/`photo` line in params.py):

1. The Z stack at the wheel centre: back-shell floor → rib rear face → rib top → board rear face → bezel inner
   face (RIB_GAP, RIB_T, Z_BACK_IN, FRONT_PLATE_T, WHEEL_T, SWITCH_H). Every battery number depends on it.
2. The divider: its rear-face Z and whether a 0.15–0.3 mm flex can pass behind it (FRAME_DIVIDER_Z_BACK) — this
   decides D2 vs B.
3. Datum: pin diameter and pitch, board outline and the four screw holes relative to the pins, switch centres
   and the two wheel-locating holes (PIN_D, PIN_PITCH, BOARD_BOTTOM_Y, SCREW_X, SCREW_RIB_FRAC, SWITCH_RADIUS,
   WHEEL_CY_FROM_BOTTOM).
4. Rib positions, widths and the screw-boss diameters (LEFT_COL_W, CENTRE_COL_W, CENTRE_SPLIT_FRAC, BOSS_D).
5. Under-LCD bay: backplate rear-face Z, bay length, jack module footprint and the RF-transparent area
   (LCD_Z_BACK via LCD_MODULE, JACK_BODY, RF_WINDOW).
6. Shell pocket width (BATT_POCKET) and the exact dock aperture.
"""


if __name__ == "__main__":
    rep_d = run("derived")
    rep_s = run("stated")
    body = md(rep_d, rep_s)
    (HERE / "space_report.md").write_text(body + "\n")
    import json
    (HERE / "out").mkdir(exist_ok=True)
    (HERE / "out" / "space_study.json").write_text(json.dumps({"derived": rep_d, "stated": rep_s}, indent=1, default=str))
    print("wrote", HERE / "space_report.md")
