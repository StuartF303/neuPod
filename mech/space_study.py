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
            if c.owner and c.owner == o.owner and ("link_conn" in c.name or "link_conn" in o.name):
                continue                                      # connector halves mate by design
            if any(k in o.name or k in c.name for k in ("fpc_stub", "flex_tail", "flex_link", "link_flex")):  # flexible, mates by design
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
    rep["params"] = dict(z_back_in=p.Z_BACK_IN, cav_ribs=p.CAVITY_T_BEHIND_RIBS, cav_lcd=p.CAVITY_T_UNDER_LCD, cav_div=p.CAVITY_T_UNDER_DIVIDER,
                         cav_amoled=p.AMOLED_Z_BACK - p.Z_BACK_IN, env=p.ENVELOPE_T_ACTUAL, pocket_full=p.POCKET_T_FULL,
                         front_in=p.Z_FRONT_IN)
    stock, _ = build("stock")
    stock = [q for q in stock if q.group != "cavity"]
    rep["free"] = free_volumes(stock, regions())
    rep["layouts"] = {}
    rf_c = (p.RF_WINDOW_X0 + p.RF_WINDOW[0] / 2, p.INT_Y1 - p.RF_WINDOW[1] / 2, p.Z_BACK_IN)
    wheel_exit = (p.WHEEL_CX - p.WHEEL_D / 2, p.WHEEL_CY, p.BOARD_FRONT_Z + p.SWITCH_H)
    amoled_exit = (0.0, p.DIVIDER_Y0, p.AMOLED_Z_BACK)
    jack_pt = ((p.JACK_BLOCK_F[0] + p.JACK_BLOCK_F[2]) / 2, p.YF(p.JACK_BLOCK_F[1]), p.Z_BACK_IN + 2.0)
    for nm in L.LAYOUTS:
        parts, meta = build(nm)
        d = {}
        boards = [q for q in parts if q.group == "board" and not q.fixed]
        d["boards"] = {b.name: round(board_area_cm2(b), 1) for b in boards}
        d["board_desc"] = meta["boards"]
        d["links"] = meta["links"]
        d["zones"] = meta["zones"]
        d["assemblies"] = meta.get("assemblies", {})
        d["haptic_lead"] = meta.get("haptic_lead")
        d["unplaced"] = meta.get("unplaced", [])
        d["link"] = meta.get("link", {})
        d["battery"] = meta.get("battery")
        d["batt_where"] = meta.get("batt_where")
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


def split_section():
    from mech import circuits as C
    o = ["## 3a. Circuit split — which circuits go on which board\n",
         "The split lives in `mech/circuits.py` (`SPLIT`); the placer packs each board from it, and the board fill, "
         "link pin count and viewer follow. Footprints include each block's local passives.\n",
         "| block | footprint mm | height | what | default board |", "|---|---|---|---|---|"]
    for k, v in C.BLOCKS.items():
        fp = "fixed" if v.get("fixed") else f"{v['fp'][0]}×{v['fp'][1]}"
        side = "front" if k in C.SPLIT_DEFAULT["front"] else "second"
        if k == "amoled_if":
            side += " (D2: second)"
        o.append(f"| {k} | {fp} | {v['h'] or '—'} | {v['desc']} | {side} |")
    o.append("""
**Proposed default (for discussion):** keep the MCU with everything fast or analog it talks to — QSPI memory,
display (except in D2), codec/headphone amp, wheel and buttons — on the stock-position *front* board, so no
high-speed or audio net crosses the link. The *second* board takes the tall and power parts: microSD (1.85),
radio (2.5), charger + rails, gauge and the battery connector. That crosses ~36 pins (B/D1, board-to-board pair)
or ~48 (D2, flex — the display connector has to live on the main board under the LCD).

**Decisions needed:**
1. **Radio height.** The RM2 (2.5 mm) does not fit on a second board behind the ribs in the 10.5 mm model (2.2 mm
   there). Either chip-down CYW43439 + chip antenna (~1 mm), or D2, where it sits by the RF window with 6 mm.
2. **Display connector**: front board (top of pocket UC, where the LCD ZIF was) in A/B/C/D1; in D2 it moves to the
   main board, adding 9 QSPI/control lines to the link.
3. **Charger side.** On the second board VBUS/CC cross the link (6 power pins); on the front board VBAT/VSYS cross
   instead. Follows where the cell ends up.
4. **Buttons**: read by the wheel's PSoC (over the 13-pin flex) or by our MCU? Probe with the analyser; it only
   matters if the MCU ever moves off the front board (5 lines).
5. **Haptic connector board.** Decided (6 Nov): iPhone 7/8 Taptic Engine on the back-shell floor at the top, beside the
   jack/hold module, driven from a *new* connector (not the piezo contacts). In D2 the main board runs right over it
   (lead < 5 mm), so connector + driver go there; in A/B/C/D1 the connector is on the front board and the lead
   runs ≈ 43 mm up under the LCD.
""")
    return "\n".join(o)


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
    o.append(f"| cell crossing under the divider (it stands 1.3 proud at the rear) | {pd['cav_div']:.1f} | {ps['cav_div']:.1f} |")
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
            "D1": "full-size front board (switches) + main board behind the ribs (mechanically = B)",
            "D2": "full-size front board (switches) + main board under the LCD, battery behind the ribs"}
    for nm, d in rep_d["layouts"].items():
        ds = rep_s["layouts"][nm]
        o.append(f"### Layout {nm} — {desc[nm]}\n")
        o.append("**Boards:** " + "; ".join(f"{k} = {d['board_desc'].get(k, '')} → {v} cm²" for k, v in d["boards"].items())
                 + f" (total {sum(d['boards'].values()):.1f} cm²)  ")
        o.append("**Inter-board links:** " + (", ".join(d["links"]) if d["links"] else "none") + "  ")
        o.append("\n**Circuit split and board fill (10.5 mm model):**\n")
        o.append("| board | role | area | face | max h | used / usable mm² | fill | circuits |\n|---|---|---|---|---|---|---|---|")
        for bn, a in d["assemblies"].items():
            for f in a["faces"]:
                if not f["avail"]:
                    continue
                o.append(f"| {bn} | {a['role']} | {a['area_cm2']} cm² | {f['face']} | {f['hmax']} | {f['used']} / {f['avail']} | {f['util']} % | "
                         + ", ".join(a["blocks"]) + " |")
        if d["unplaced"]:
            o.append("\n**Does not fit:** " + "; ".join(f"{b} — {why}" for b, why in d["unplaced"])
                     + ("  (stated model: " + ("all fit" if not ds["unplaced"] else ", ".join(b for b, _ in ds["unplaced"])) + ")"))
        lk = d["link"]
        if lk.get("pins"):
            o.append(f"\n**Link:** {lk['desc']}. {lk['totals']['signals']} signals + {lk['totals']['power']} power + "
                     f"{lk['totals']['ground']} ground = **{lk['pins']} pins**. Crossing: "
                     + "; ".join(f"{r[0]} ({r[1]})" for r in lk["rows"]) + ".")
        o.append("\n**Max component height per zone (derived / stated):**\n")
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
        o.append(f"**Best catalogue cell:** {d['battery'] or 'none fits'} = {d['battery_mah']} mAh, {d.get('batt_where') or '—'} (derived); "
                 f"{ds['battery'] or 'none fits'} = {ds['battery_mah']} mAh, {ds.get('batt_where') or '—'} (stated).  ")
        if d["haptic"]:
            o.append(f"**Haptic:** {d['haptic'][0]} at bbox {d['haptic'][1]}  ")
        if d.get("antenna"):
            o.append(f"**Antenna:** {d['antenna']['module']} centre is {d['antenna']['dist_to_window']} mm (Manhattan) from the RF window centre  ")
        f = d["fpc"]
        if d.get("haptic_lead"):
            o.append(f"**Taptic lead:** connector on {d['haptic_lead']['board']}, ≈ {d['haptic_lead']['length']} mm to the actuator  ")
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
    o.append(split_section())
    o.append(RECOMMENDATION)
    o.append("## 6. Parameters the scan must replace (estimate / photo)\n")
    o.append(p.table(("estimate", "photo")))
    o.append("")
    return "\n".join(o)


RECOMMENDATION = """## 4. What the scans changed (6 Nov 2026)

- **Board is 55 × ~43.5 mm** (+ a 20 × 1.9 tab on the top-edge centre), not 55 × 55, with a ~19.5 mm notch in the
  dock edge for the 30-pin connector. Switch radius 14.1 mm; switch tops ≈ 0.8 mm (centre 1.2).
  Photo 18 is upside down; the wheel sits nearer the LCD end.
- **The two holes either side of the centre switch are the board screws** into the middle (screw) rib (confirmed by
  Stuart), so the wheel centre sits exactly on that rib: 25.6 mm above the pins, 26.3 above the dock edge, 17.2
  below the top edge. The dock edge rests against the frame's bottom wall.
- **Ribs step**: lower verticals at ±12.4 (bottom wall → screw rib), upper at −16.0 / +15.9 (screw rib → divider);
  cross rib 15.4 above the pins.
- **Locating pins stand where the lower ribs meet the bottom wall**, just outside each rib, either side of the dock
  port: pitch ≈ 26.4 (photo 16 scaled by the scanned frame; the pins are too thin to scan). On the board they land
  0.7 mm from the dock edge, so they read as edge notches — as the teardown noted.
- **Frame outer corners R ≈ 4.8** (both scans), small fillets in the pocket corners; the back-shell dock aperture is
  a 21 × 3 slot with fully rounded ends.
- **Divider is flush with the ribs on the board side** and stands 1.3 mm proud at the rear. A board can run straight
  over it into the LCD bay (layout C and the D2 link); a cell behind the ribs that crosses it loses 1.3 mm.
- **Frame front is a 4 mm tray**: walls and the dock strap stand 4.0 mm in front of the rib front face; the board,
  wheel and LCD live in it. The dock connector tucks under the strap.
- **Jack/hold module is top-RIGHT seen from the front** (10 × 13 block + Ø7 boss); the plastic top insert (the RF
  window) runs across the rest of the top end, X −28.5…15. The old cell pocket (45 wide) is exactly the space left
  of the jack.
- Back shell: rim 7.8 mm above the inner floor (outer scan 8.3 to the back face). The polished floor did not scan.
- **Taptic Engine (decided):** 35 × 11 × 4.5 on the floor along the top end, against the jack/hold module, just inside
  the plastic insert. It takes the top 11 mm of the under-LCD bay: in B/D1 the cell there drops from 40 × 40
  (~1.1 Ah) to 30 × 40 (~650 mAh). Its steel body sits next to the RF window — keep the radio antenna at the
  insert's left end and check detuning once the antenna is chosen.

| layout | boards (cm²) | link | cell, 10.5 mm model | cell, 7 mm model | does not fit (10.5 mm) |
|---|---|---|---|---|---|
| A | 23 | — | none | 405060 2.0 Ah behind the ribs | radio, microSD |
| B / D1 | 23 + 23 | 36-pin board-to-board, 2.2 mm stack | 503040 650 mAh under the LCD | 503040 650 mAh | RM2 radio (2.5 > 2.2) |
| C | 41 (T over the divider) | — | none | 405060 2.0 Ah behind the ribs | — |
| D2 | 23 + 24 | 50-pin 0.5 mm flex behind the divider | none (2.0 mm behind the ribs) | 405060 2.0 Ah behind the ribs | — |

Fixed in every layout: front board = donor outline with the five switches, the screw/locating holes, the wheel
13P and jack/hold connectors at the donor positions (rear face), USB-C under the dock strap (overhanging the dock edge to clear the wheel ring), and the AMOLED
connector at the top of pocket UC (the tab above sits on the divider, so nothing taller than ~0.2 mm goes behind it).

## 5. Recommendation

**Lay out the front board now** — its outline, holes, switch pattern, both reused connectors and the USB-C position
are all set by the scan and do not depend on the open depth question. Outline and fixed parts are the same in B, D1
and D2; with the default split it also carries the MCU, memory, audio and haptic driver (and the display connector,
except in D2).

**The second board and the cell hinge on one number: rib rear face → back-shell floor.**
- If it is ≈ 4 mm (what the 10.5 mm envelope implies with these front-tray numbers): **D1** — main board behind the
  ribs below the divider (54 × 40), cell under the LCD left of the jack, below the Taptic (≈ 30 × 40 × 5, ~650 mAh — the Taptic costs the 40 × 40 cell here).
  The main board has only 2.2 mm on its shell side, so the RM2 (2.5) does not fit: use a chip-down CYW43439
  (~1 mm) with a flex/coax antenna to the plastic insert. Link: 36-pin board-to-board pair through pocket LR.
- If it is ≥ 6 mm (the 7 mm noted at the teardown): **D2** — main board under the LCD with the radio against the
  plastic insert, cell behind the ribs (405060 ~2 Ah), linked to the front board by a 4 mm flex over the divider.
  C (one T-shaped rigid board, same placement) is the cheaper variant if the AMOLED backplate clearance holds.

**Caliper these next (in this order):**
1. Rib thickness, and frame total depth at a side wall (RIB_T, FRAME_WALL_FRONT_H) — and whether the board sits
   directly on the ribs (RIB_GAP).
2. Back-shell inside depth, floor to rim (BACK_SHELL_SIDE_H — the floor did not scan), and the assembled thickness
   at the centre (SHELL_T).
3. Front plate + wheel stack: bezel inner face to glass (FRONT_PLATE_T), wheel sub-assembly thickness.
4. Pin centre-to-centre and diameter, and pin centre to the frame's bottom wall (PIN_PITCH, PIN_D, FRAME_PIN_YF).
5. Board length dock edge → top edge (≈ 43.5 derived) and thickness; centre switch to top edge (17.2); switch heights.
6. Dock strap thickness (DOCK_STRAP_T), and the AMOLED module outline from the datasheet.
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
