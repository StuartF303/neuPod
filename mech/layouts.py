"""Candidate layouts A–D2.  Each builder returns (parts, meta).

Circuits are no longer hand-placed: mech/circuits.py says which board each circuit block goes on,
and mech/placer.py packs the blocks into the usable regions of that board.  Every part carries
`owner` = the board assembly it belongs to (PCB + its components and connectors), or "link".

parts : list of Part (fixed=False) — boards, component boxes, battery, haptic, links.
meta  : dict with boards, links, fpc endpoints, antenna module, battery, haptic,
        and zone height limits used by the space study.

Component boxes are placed by (cx, cy) on a board face: face="rear" grows toward
-Z (the ribs / shell floor), face="front" grows toward +Z (the glass).
Toggle options with the OPTIONS dict (MCU, radio, cell, haptic, display).
"""
from . import params as p
from .parts.common import Part, box, box_at, cyl
from .parts import board as B
from .parts.midframe import pocket_grid
from . import circuits as C
from . import placer

OPTIONS = dict(mcu="RP2350B", radio="RM2", display="amoled")

COMP = (0.85, 0.35, 0.2)
BATT = (0.95, 0.8, 0.2)
HAPT = (0.6, 0.2, 0.7)
LINK = (0.9, 0.9, 0.2)
FLEX = (0.85, 0.55, 0.1)


def comp(name, size, cx, cy, z_face, face="rear", notes=""):
    h = size[2]
    z0 = z_face if face == "front" else z_face - h
    return Part(name, box_at(cx, cy, z0, size, h), COMP, "candidate", fixed=False, notes=notes)


def cell(cell_name, cx, cy, z_floor, orient="XY", notes=""):
    """LiPo with foam: sits on z_floor (its rear face at z_floor + foam)."""
    w, l, t, mah = p.CELLS[cell_name]
    if orient == "YX":
        w, l = l, w
    f = p.BATT_FOAM
    s = box_at(cx, cy, z_floor, (w + 2 * f, l + 2 * f), t + 2 * f)
    return Part(f"battery_{cell_name}", s, BATT, "candidate", fixed=False,
                notes=f"{cell_name} {mah} mAh incl. {f} mm foam each side; {notes}")


def fit_cell(bx, cells=None, foam=None):
    """Largest-capacity catalogue cell that fits box bx=(x0,y0,z0,x1,y1,z1) with foam, either orientation.
    Returns (name, orient) or (None, None)."""
    cells = cells or p.CELLS
    f = p.BATT_FOAM if foam is None else foam
    x0, y0, z0, x1, y1, z1 = bx
    W, Lg, T = x1 - x0, y1 - y0, z1 - z0
    best = (None, None, -1)
    for nm, (w, l, t, mah) in cells.items():
        if t + 2 * f > T:
            continue
        if w + 2 * f <= W and l + 2 * f <= Lg:
            o = "XY"
        elif l + 2 * f <= W and w + 2 * f <= Lg:
            o = "YX"
        else:
            continue
        if mah > best[2]:
            best = (nm, o, mah)
    return best[0], best[1]


def cell_in_box(bx, notes="", prefer=None):
    """Place the best-fitting cell centred in the box footprint, resting on the box floor."""
    nm, o = fit_cell(bx)
    if nm is None:
        return None
    x0, y0, z0, x1, y1, z1 = bx
    return cell(nm, (x0 + x1) / 2, (y0 + y1) / 2, z0, o, notes)


def best_cell(boxes):
    """boxes: list of (label, box). Returns (Part or None, box, label) for the highest-capacity fit."""
    best = (None, None, None, -1)
    for label, bx in boxes:
        nm, o = fit_cell(bx)
        if nm and p.CELLS[nm][3] > best[3]:
            x0, y0, z0, x1, y1, z1 = bx
            best = (cell(nm, (x0 + x1) / 2, (y0 + y1) / 2, z0, o, label), bx, label, p.CELLS[nm][3])
    return best[:3]


def cavity_boxes(y_top_clear=None):
    """Candidate battery boxes behind the board: below the divider (rib-rear limited) and crossing it
    (divider-rear limited). y_top_clear trims the crossing box where rear-face parts above sit lower."""
    hw = p.HDD_SIZE[0] / 2
    c = p.CLEARANCE
    y_hi = p.HDD_Y0 + p.HDD_SIZE[1] if y_top_clear is None else y_top_clear
    y_lo = max(p.HDD_Y0, usb_back_y() + c)
    return [("behind the ribs, below the divider", (-hw, y_lo, p.Z_BACK_IN, hw, p.DIVIDER_Y0 - c, p.RIB_Z_BACK - c)),
            ("behind the ribs, crossing under the divider", (-hw, y_lo, p.Z_BACK_IN, hw, y_hi - c, p.FRAME_DIVIDER_Z_BACK - c))]


def usb_back_y():
    """Inner end of the USB-C receptacle (it reaches past the board's dock edge into the cavity)."""
    return p.INT_Y0 + p.CLEARANCE + p.USBC[1]


def lcd_z_back():
    return p.AMOLED_Z_BACK if OPTIONS["display"] == "amoled" else p.LCD_Z_BACK


def under_lcd_boxes(z_top):
    """Battery boxes under the LCD: full width below the jack module, or left of the jack up to the plastic insert."""
    c = p.CLEARANCE
    xi = p.FRAME_IN_W / 2 - c
    return [("under the LCD, below the jack", (-xi, p.DIVIDER_Y1 + c, p.Z_BACK_IN, xi, p.JACK_POS[1] - c, z_top)),
            ("under the LCD, left of the jack, below the Taptic", (-xi, p.DIVIDER_Y1 + c, p.Z_BACK_IN, p.JACK_POS[0] - c,
                                                                 p.TAPTIC_POS[1] - p.HAPTIC_TAPTIC[1] / 2 - c, z_top))]


def taptic():
    """The chosen haptic: iPhone 7/8 Taptic Engine on the back-shell floor at the top, beside the jack/hold module."""
    return haptic("taptic", *p.TAPTIC_POS, p.Z_BACK_IN)


def haptic(kind, cx, cy, z_floor, orient="XY"):
    size = {"taptic": p.HAPTIC_TAPTIC, "lra": p.HAPTIC_LRA, "piezo": p.HAPTIC_PIEZO}[kind]
    w, l, t = size
    if orient == "YX":
        w, l = l, w
    return Part(f"haptic_{kind}", box_at(cx, cy, z_floor, (w, l), t), HAPT, "candidate", fixed=False,
                notes=f"{kind} actuator {size}")


def _pc(name):
    x0, y0, x1, y1 = pocket_grid()[name]
    return (x0 + x1) / 2, (y0 + y1) / 2, x1 - x0, y1 - y0


AMOLED_CONN = (19.0, 4.5, 1.0)
BOARD_T = 1.0


def amoled_conn_y():
    """Rear face, top of pocket UC: the tab above sits on the divider (no rear-face height there)."""
    return _pc("UC")[1] + _pc("UC")[3] / 2 - AMOLED_CONN[1] / 2 - 0.35


def _rect_of(part):
    b = part.bbox
    return (b[0], b[1], b[3], b[4])


def _owned(part, owner):
    part.owner = owner
    return part


def usbc_mount(z_rear, t=BOARD_T):
    """Best way to mount the receptacle on a board whose rear face is at z_rear so it centres on the shell hole.
    Returns dict(mount, centre, residual, face)."""
    h = p.USBC[2]
    cands = {"top-mount, rear face": (z_rear - h / 2, "rear"),
             "mid-mount": (z_rear + t / 2, None),
             "top-mount, front face": (z_rear + t + h / 2, "front")}
    best = min(cands.items(), key=lambda kv: abs(kv[1][0] - p.DOCK_AP_ZC))
    centre, face = best[1]
    return dict(mount=best[0], centre=round(centre, 2), residual=round(centre - p.DOCK_AP_ZC, 2), face=face,
                ok=abs(centre - p.DOCK_AP_ZC) <= p.USBC_MOUNT_RESIDUAL_OK)


def _usbc_part(z_rear, owner):
    """Receptacle on a tab reaching the shell wall, mating face just inside the dock hole, X on the hole centre."""
    w, l, h = p.USBC
    m = usbc_mount(z_rear)
    y0 = p.INT_Y0 + p.CLEARANCE
    part = Part("usb_c", box_at(p.DOCK_AP_CX, y0 + l / 2, m["centre"] - h / 2, (w, l), h), COMP, "candidate", fixed=False,
                notes=f"USB-C {m['mount']} on {owner}; centre {m['centre']:+.2f} vs hole {p.DOCK_AP_ZC:+.2f} "
                      f"({'OK' if m['ok'] else 'needs an offset/sink mount or spacer'})", owner=owner)
    return part, m


def _front_faces(hmax):
    """Rear-face regions of the stock-position board: the pockets (clipped to the board, 0.3 in from ribs)."""
    regs = []
    for nm, (x0, y0, x1, y1) in pocket_grid().items():
        regs.append((x0 + 0.3, y0 + 0.3, x1 - 0.3, y1 - 0.3, hmax))
    return dict(face="rear", z=0.0, regions=regs)


def _front_fixed(front_name, where, amoled_xy=None):
    """Fixed items on the stock-position board. Returns (parts, occupied rects on its rear face)."""
    t = BOARD_T
    parts, occ = [], []
    for sw in B.switch_zone(t, "sw"):
        parts.append(_owned(sw, front_name))
    for name, size, pos, note in (("wheel_if", p.WHEEL_CONN_SIZE, p.WHEEL_CONN_POS, "13P 0.3 mm wheel FPC — donor position (reused flex)"),
                                  ("jack_if", p.JACK_CONN_SIZE, p.JACK_CONN_POS, "jack/hold FPC — donor position (reused flex)")):
        c = _owned(comp(name, size, *pos, 0.0, "rear", note), front_name)
        parts.append(c)
        occ.append(_rect_of(c))
    if where["usb_c"] == "front":
        u, _ = _usbc_part(0.0, front_name)
        parts.append(u)
        occ.append(_rect_of(u))
    for sx, sy in p.SCREW_POS:                                     # screw bosses
        r = p.BOSS_D / 2 + 0.2
        occ.append((sx - r, sy - r, sx + r, sy + r))
    if where["amoled_if"] == "front":
        xy = amoled_xy or (0.0, amoled_conn_y())
        c = _owned(comp("amoled_if", AMOLED_CONN, *xy, 0.0, "rear", "30P 0.5 mm AMOLED FPC, top of pocket UC (where the LCD ZIF was)"), front_name)
        parts.append(c)
        occ.append(_rect_of(c))
    return parts, occ


def _loose_occupancy(face, loose):
    """Battery/haptic parts under a rear face → (footprint, height still available above them)."""
    out = []
    for q in loose:
        if face["face"] == "rear" and q.bbox[5] < face["z"]:
            out.append((_rect_of(q), face["z"] - q.bbox[5] - p.CLEARANCE))
    return out


ORDER = ["radio", "microsd", "mcu", "memory", "haptic_conn", "haptic_drv", "batt_conn", "charger", "gauge", "power", "audio"]   # big blocks first


def _populate(layout, boards, occ, loose, targets):
    """Place every movable block of the split on its board. boards: role -> dict(name, part, faces).
    occ: (role, face_index) -> list of occupied rects. Returns parts, placed (block -> (role, rect)), unplaced."""
    where = C.board_of(layout)
    parts, placed, unplaced = [], {}, []
    for blk in ORDER:
        spec = C.BLOCKS[blk]
        role = where[blk]
        bd = boards.get(role)
        if bd is None:
            unplaced.append((blk, "no such board in this layout"))
            continue
        tgt_key = spec.get("near")
        if tgt_key == "board_centre" or tgt_key is None:
            b = bd["part"].bbox
            tgt = ((b[0] + b[3]) / 2, (b[1] + b[4]) / 2)
        elif tgt_key in placed and placed[tgt_key][0] == role:
            r = placed[tgt_key][1]
            tgt = ((r[0] + r[2]) / 2, (r[1] + r[3]) / 2)
        else:
            tgt = targets.get(tgt_key) or ((bd["part"].bbox[0] + bd["part"].bbox[3]) / 2, (bd["part"].bbox[1] + bd["part"].bbox[4]) / 2)
        done = None
        for fi, face in enumerate(bd["faces"]):
            key = (role, fi)
            occ.setdefault(key, [])
            res = placer.place(face, occ[key], blk, spec["fp"], spec["h"], tgt, soft=_loose_occupancy(face, loose))
            if res:
                rect, rot = res
                occ[key].append(rect)
                done = (role, fi, rect)
                break
        if not done:
            unplaced.append((blk, f"no room ≥ {spec['fp'][0]}×{spec['fp'][1]} with {spec['h']} mm height on the {role} board"))
            continue
        role, fi, rect = done
        face = bd["faces"][fi]
        cx, cy = (rect[0] + rect[2]) / 2, (rect[1] + rect[3]) / 2
        size = (rect[2] - rect[0], rect[3] - rect[1], spec["h"])
        parts.append(_owned(comp(blk, size, cx, cy, face["z"], face["face"], spec["desc"]), bd["name"]))
        placed[blk] = (role, rect)
    return parts, placed, unplaced


def _assembly_meta(layout, boards, occ, parts, unplaced):
    out = {}
    where = C.board_of(layout)
    for role, bd in boards.items():
        faces = []
        for fi, face in enumerate(bd["faces"]):
            avail = placer.region_area(face)
            used = 0.0
            for r in occ.get((role, fi), []):
                used += (r[2] - r[0]) * (r[3] - r[1])
            faces.append(dict(face=face["face"], avail=round(avail), used=round(used),
                              util=round(100 * used / avail) if avail else 0,
                              hmax=round(max((rg[4] for rg in face["regions"]), default=0), 1)))
        out[bd["name"]] = dict(role=role, area_cm2=round(bd["part"].volume() / BOARD_T / 100, 1),
                               blocks=[b for b, r in where.items() if r == role],
                               parts=[q.name for q in parts if q.owner == bd["name"]],
                               faces=faces, desc=bd.get("desc", ""))
    return out


def _finish(layout, boards, fixed_parts, occ, loose, targets, link, extra_meta):
    parts = [bd["part"] for bd in boards.values()] + fixed_parts + loose
    placed_parts, placed, unplaced = _populate(layout, boards, occ, loose, targets)
    parts += placed_parts
    if link:
        parts += link["parts"]
    rows, tot = C.crossing(layout)
    meta = dict(extra_meta)
    meta["assemblies"] = _assembly_meta(layout, boards, occ, parts, unplaced)
    meta["unplaced"] = unplaced
    meta["link"] = dict(kind=link["kind"] if link else None, pins=tot["total"], rows=rows, totals=tot,
                        length=link.get("length") if link else 0, desc=link.get("desc") if link else "none")
    meta["boards"] = {bd["name"]: bd.get("desc", "") for bd in boards.values()}
    meta["links"] = [link["desc"]] if link else []
    meta["antenna"] = "radio" if "radio" in placed else None
    u = next((q for q in parts if q.name == "usb_c"), None)
    if u:
        bd = next(b for b in boards.values() if b["name"] == u.owner)
        z_rear = bd["part"].bbox[2]
        m = usbc_mount(z_rear)
        meta["usb"] = dict(board=u.owner, **m, hole_zc=p.DOCK_AP_ZC)
    meta["wheel_conn"] = (*p.WHEEL_CONN_POS, 0.0)
    meta["jack_conn"] = (*p.JACK_CONN_POS, 0.0)
    hc = next((q for q in parts if q.name == "haptic_conn"), None)
    hp = next((q for q in parts if q.name.startswith("haptic_") and q.owner is None), None)
    if hc and hp:
        cx, cy = (hc.bbox[0] + hc.bbox[3]) / 2, (hc.bbox[1] + hc.bbox[4]) / 2
        nx = min(max(cx, hp.bbox[0]), hp.bbox[3]); ny = min(max(cy, hp.bbox[1]), hp.bbox[4])
        meta["haptic_lead"] = dict(board=hc.owner, length=round(abs(cx - nx) + abs(cy - ny) + abs(hc.bbox[2] - hp.bbox[5]), 1))
    am = next((q for q in parts if q.name == "amoled_if"), None)
    meta["amoled_conn"] = ((am.bbox[0] + am.bbox[3]) / 2, (am.bbox[1] + am.bbox[4]) / 2, am.bbox[5]) if am else (0, 0, 0)
    return parts, meta


def _targets(battery, hap):
    t = {"rf_window": (p.RF_WINDOW_X0 + p.RF_WINDOW[0] / 2, p.INSERT_Y0), "jack_if": p.JACK_CONN_POS,
         "usb_c": (0.0, p.BOARD_BOTTOM_Y)}
    if battery is not None:
        b = battery.bbox
        t["battery"] = ((b[0] + b[3]) / 2, (b[1] + b[4]) / 2)
    if hap is not None:
        b = hap.bbox
        t["haptic"] = ((b[0] + b[3]) / 2, (b[1] + b[4]) / 2)
    return t


def _mezzanine(pins, front_name, second_name, z_front_rear, z_second_front, xy):
    con = C.link_connector(pins, "mezzanine")
    stack = z_front_rear - z_second_front
    a = _owned(Part("link_conn_front", box_at(*xy, z_front_rear - stack / 2, (con["w"], con["l"]), stack / 2), LINK, "candidate",
                    fixed=False, notes=con["desc"] + " (plug)"), front_name)
    b = _owned(Part("link_conn_second", box_at(*xy, z_second_front, (con["w"], con["l"]), stack / 2), LINK, "candidate",
                    fixed=False, notes=con["desc"] + " (receptacle)"), second_name)
    rect = (xy[0] - con["w"] / 2, xy[1] - con["l"] / 2, xy[0] + con["w"] / 2, xy[1] + con["l"] / 2)
    return dict(kind="mezzanine", parts=[a, b], rect=rect, length=0.0,
                desc=f"{con['desc']}, stack {stack:.1f} mm, at ({xy[0]:.1f}, {xy[1]:.1f})")


def _front_hmax(behind_top):
    """Height available on the front board rear face in the pockets, given the Z of whatever is behind them."""
    return round((0.0 - behind_top) - p.CLEARANCE, 2)


# --------------------------------------------------------------------------- #
def layout_A():
    """Single board, donor outline, stock position: switches on the wheel face, everything else in the pockets."""
    where = C.board_of("A")
    front = Part("main_board", B.board_outline(0.0, BOARD_T, usbc=where["usb_c"] == "front", usbc_mount=usbc_mount(0.0)["mount"]), B.PCB_NEW, "board", fixed=False,
                 notes="donor outline (scan): 55 × 43.5 + top tab", owner="main_board")
    bat, batt_box, blabel = best_cell(cavity_boxes())
    hmax = _front_hmax(bat.bbox[5] if bat else p.Z_BACK_IN)
    boards = {"front": dict(name="main_board", part=front, faces=[_front_faces(hmax)], desc="55×43.5 donor outline")}
    fixed, occ0 = _front_fixed("main_board", where)
    hap = taptic()
    loose = [q for q in (bat, hap) if q]
    occ = {("front", 0): occ0}
    meta = dict(batt_box=batt_box, batt_where=blabel, battery=bat.name if bat else None, haptic=hap.name,
                zones={"rear face under ribs": p.RIB_GAP - p.CLEARANCE,
                       "rear face in pockets": hmax,
                       "wheel face inside 37 mm ring": p.SWITCH_H,
                       "wheel face outside wheel (design limit)": p.FRONT_FACE_LOWPROFILE_H})
    return _finish("A", boards, fixed, occ, loose, _targets(bat, hap), None, meta)


def _second_behind_ribs(name, with_usb=False):
    t = BOARD_T
    z_top = p.RIB_Z_BACK - p.CLEARANCE
    z = z_top - t
    hw = p.HDD_SIZE[0] / 2
    y1 = p.DIVIDER_Y0 - p.CLEARANCE
    y0 = p.HDD_Y0 if with_usb else max(p.HDD_Y0, usb_back_y() + p.CLEARANCE)   # clear of a USB-C hung from the front board
    part = B.rect_board(name, -hw, y0, hw, y1, z, t, notes="behind the ribs, below the divider")
    if with_usb:                                                     # tab out to the shell wall under the dock gap
        tw = p.USBC_TAB_W
        part = Part(name, part.shape.union(B.rect_board("tab", p.DOCK_AP_CX - tw / 2, p.INT_Y0 + p.CLEARANCE,
                                                         p.DOCK_AP_CX + tw / 2, p.HDD_Y0 + 1.0, z, t).shape),
                    B.PCB_NEW, "board", fixed=False, notes="behind the ribs, with a tab to the dock hole")
    part.owner = name
    face = dict(face="rear", z=z, regions=[(-hw + 0.5, y0 + 0.5, hw - 0.5, y1 - 0.5, round(z - p.Z_BACK_IN - p.CLEARANCE, 2))])
    front_face = dict(face="front", z=z_top, regions=[])          # touches the ribs: link connector only
    return part, z, z_top, [face, front_face], f"54×{y1 - y0:.0f} behind the ribs"


def _layout_behind_ribs(layout, front_name, second_name):
    """B and D1: stock-position board + a second board behind the ribs, joined by a board-to-board pair."""
    where = C.board_of(layout)
    front = Part(front_name, B.board_outline(0.0, BOARD_T, usbc=where["usb_c"] == "front", usbc_mount=usbc_mount(0.0)["mount"]), B.PCB_NEW, "board", fixed=False,
                 notes="donor outline: switches + front-side circuits", owner=front_name)
    second, z2, z2_top, faces2, desc2 = _second_behind_ribs(second_name, with_usb=where["usb_c"] == "second")
    if where["usb_c"] == "second":                                # receptacle pokes up through the front board plane: notch it
        w, l, _ = p.USBC
        c = p.CLEARANCE
        notch = box(p.DOCK_AP_CX - w / 2 - c, p.INT_Y0 - 1, -1, p.DOCK_AP_CX + w / 2 + c, usb_back_y() + c, 2)
        front = Part(front_name, front.shape.cut(notch), B.PCB_NEW, "board", fixed=False,
                     notes="donor outline, notched for the USB-C on the lower board", owner=front_name)
    hmax = _front_hmax(z2_top)
    boards = {"front": dict(name=front_name, part=front, faces=[_front_faces(hmax)], desc="55×43.5 donor outline"),
              "second": dict(name=second_name, part=second, faces=faces2, desc=desc2)}
    fixed, occ0 = _front_fixed(front_name, where)
    rx, ry, _, _ = _pc("LR")
    rows, tot = C.crossing(layout)
    link = _mezzanine(tot["total"], front_name, second_name, 0.0, z2_top, (rx, ry + 4.0))
    occ = {("front", 0): occ0 + [link["rect"]], ("second", 1): [link["rect"]]}
    if where["usb_c"] == "second":
        u, m = _usbc_part(z2, second_name)
        fixed.append(u)
        occ[("second", 0 if m["face"] == "rear" else 1)].append(_rect_of(u))
        occ[("front", 0)].append(_rect_of(u))                     # nothing on the front board's rear face over it
    bat, batt_box, blabel = best_cell(under_lcd_boxes(lcd_z_back() - p.CLEARANCE))
    hap = taptic()
    loose = [q for q in (bat, hap) if q]
    meta = dict(batt_box=batt_box, batt_where=blabel, battery=bat.name if bat else None, haptic=hap.name,
                zones={"front board rear face, pockets": hmax,
                       "front board rear face under ribs": p.RIB_GAP - p.CLEARANCE,
                       "second board, shell side": faces2[0]["regions"][0][4],
                       "wheel face inside 37 mm ring": p.SWITCH_H})
    return _finish(layout, boards, fixed, occ, loose, _targets(bat, hap), link, meta)


def layout_B():
    """A plus a second board in the HDD cavity behind the ribs, board-to-board pair through pocket LR."""
    return _layout_behind_ribs("B", "main_board", "cavity_board")


def layout_D1():
    """Front board (donor outline) + main board behind the ribs. With the full-size front board this is
    mechanically the same as B; kept so the D naming from the brief still resolves."""
    return _layout_behind_ribs("D1", "front_board", "main_board")


def layout_C():
    """Single rigid board: donor outline plus an extension over the divider and under the LCD backplate."""
    where = C.board_of("C")
    ext_y1 = p.JACK_POS[1] - p.CLEARANCE
    xi = p.FRAME_IN_W / 2 - p.CLEARANCE
    front = B.l_board("main_board_T", 0.0, BOARD_T, ext_y1, -xi, xi,
                      notes="extension runs over the divider front and behind the backplate")
    front.owner = "main_board_T"
    bat, batt_box, blabel = best_cell(cavity_boxes())
    hmax = _front_hmax(bat.bbox[5] if bat else p.Z_BACK_IN)
    ext_h = round(0.0 - p.Z_BACK_IN - p.CLEARANCE, 2)
    y_bat = bat.bbox[4] if bat else p.DIVIDER_Y1
    regs = _front_faces(hmax)["regions"]
    y_ext0 = p.DIVIDER_Y1 + 0.3
    if bat and y_bat > y_ext0:
        regs.append((-xi + 0.5, y_ext0, xi - 0.5, y_bat, hmax))
        regs.append((-xi + 0.5, y_bat, xi - 0.5, ext_y1 - 0.5, ext_h))
    else:
        regs.append((-xi + 0.5, y_ext0, xi - 0.5, ext_y1 - 0.5, ext_h))
    face = dict(face="rear", z=0.0, regions=regs)
    boards = {"front": dict(name="main_board_T", part=front, faces=[face], desc="55×43.5 + extension under the LCD")}
    fixed, occ0 = _front_fixed("main_board_T", where)
    hap = taptic()
    loose = [q for q in (bat, hap) if q]
    meta = dict(batt_box=batt_box, batt_where=blabel, battery=bat.name if bat else None, haptic=hap.name,
                zones={"rear face in pockets": hmax, "extension rear face": ext_h,
                       "wheel face inside 37 mm ring": p.SWITCH_H})
    return _finish("C", boards, fixed, {("front", 0): occ0}, loose, _targets(bat, hap), None, meta)


def layout_D2():
    """Front board (donor outline) + main board under the LCD (T round the jack), cell behind the ribs,
    joined by a flex that runs behind the divider."""
    where = C.board_of("D2")
    t = BOARD_T
    front = Part("front_board", B.board_outline(0.0, t, usbc=where["usb_c"] == "front", usbc_mount=usbc_mount(0.0)["mount"]), B.PCB_NEW, "board", fixed=False,
                 notes="donor outline: switches + front-side circuits", owner="front_board")
    z_top = lcd_z_back() - p.CLEARANCE
    z = z_top - t
    c = p.CLEARANCE
    xi = p.FRAME_IN_W / 2 - c
    y0, y1 = p.DIVIDER_Y1 + c, p.JACK_POS[1] - c
    y2 = p.INSERT_Y0 - c
    xj = p.JACK_POS[0] - c
    mb = B.rect_board("main_board", -xi, y0, xi, y1, z, t).shape.union(B.rect_board("x", -xi, y1 - 2.0, xj, y2, z, t).shape)
    second = Part("main_board", mb, B.PCB_NEW, "board", fixed=False, notes="under the LCD; runs up beside the jack to the plastic insert",
                  owner="main_board")
    rows, tot = C.crossing("D2")
    con = C.link_connector(tot["total"], "fpc")
    # flex: front-board rear face (top of pocket UC) → down → behind the divider → up to the main board rear face
    yf = amoled_conn_y()
    y_m = y0 + 9.5
    zf = p.FRAME_DIVIDER_Z_BACK - 0.15
    fw = con["w"] - 2.0
    cf = _owned(comp("link_conn_front", (con["w"], con["l"], con["h"]), 0.0, yf, 0.0, "rear", con["desc"]), "front_board")
    cm = _owned(comp("link_conn_second", (con["w"], con["l"], con["h"]), 0.0, y_m, z, "rear", con["desc"]), "main_board")
    ya = yf + con["l"] / 2 + 0.15
    flex = (box(-fw / 2, ya, zf - 0.12, fw / 2, ya + 0.12, -con["h"])
            .union(box(-fw / 2, ya, zf - 0.12, fw / 2, y_m - con["l"] / 2, zf))
            .union(box(-fw / 2, y_m - con["l"] / 2 - 0.12, zf - 0.12, fw / 2, y_m - con["l"] / 2, z - con["h"])))
    length = (-con["h"] - zf) + (y_m - con["l"] / 2 - ya) + ((z - con["h"]) - zf)
    link = dict(kind="fpc", parts=[cf, cm, Part("link_flex", flex, FLEX, "candidate", fixed=False, owner="link",
                                                notes=f"{tot['total']}-way flex behind the divider, ≈{length:.0f} mm")],
                length=round(length, 1), desc=f"{con['desc']}, flex ≈{length:.0f} mm behind the divider")
    # battery behind the ribs, under the divider and the flex
    hw2 = p.HDD_SIZE[0] / 2
    y_clear = p.HDD_Y0 + p.HDD_SIZE[1] - 8.0
    batt_box = (-hw2, p.HDD_Y0, p.Z_BACK_IN, hw2, y_clear - c, min(p.RIB_Z_BACK, zf - 0.12) - 0.25)
    nm, o = fit_cell(batt_box)
    bat = cell(nm, 0.0, (batt_box[1] + batt_box[4]) / 2, p.Z_BACK_IN, o, "behind the ribs, under the divider") if nm else None
    hmax = _front_hmax(bat.bbox[5] if bat else p.Z_BACK_IN)
    hap = taptic()
    loose = [q for q in (bat, hap) if q]
    hm = round(z - p.Z_BACK_IN - c, 2)
    face2 = dict(face="rear", z=z, regions=[(-xi + 0.5, y0 + 0.5, xi - 0.5, y1 - 0.5, hm),
                                            (-xi + 0.5, y1 - 0.5, xj - 0.5, y2 - 0.5, hm)])
    boards = {"front": dict(name="front_board", part=front, faces=[_front_faces(hmax)], desc="55×43.5 donor outline"),
              "second": dict(name="main_board", part=second, faces=[face2], desc="T under the LCD")}
    fixed, occ0 = _front_fixed("front_board", where)
    occ = {("front", 0): occ0 + [_rect_of(cf)], ("second", 0): [_rect_of(cm)]}
    if where["amoled_if"] == "second":
        a = _owned(comp("amoled_if", AMOLED_CONN, 0.0, y0 + 2.6, z, "rear", "30P 0.5 mm AMOLED FPC; wraps the main board's bottom edge"), "main_board")
        fixed.append(a)
        occ[("second", 0)].append(_rect_of(a))
    meta = dict(batt_box=batt_box, batt_where="behind the ribs, under the divider" if bat else None,
                battery=bat.name if bat else None, haptic=hap.name,
                zones={"front board rear face, pockets": hmax,
                       "main board, shell side": hm,
                       "main board, front side (toward backplate)": 0.0,
                       "wheel face inside 37 mm ring": p.SWITCH_H})
    return _finish("D2", boards, fixed, occ, loose, _targets(bat, hap), link, meta)


def layout_D1_sd_lower():
    """D1 with the USB-C, USB mux, SD bridge and card on the lower board behind the ribs."""
    return _layout_behind_ribs("D1-sd-lower", "front_board", "main_board")


LAYOUTS = {"A": layout_A, "B": layout_B, "C": layout_C, "D1": layout_D1, "D1-sd-lower": layout_D1_sd_lower, "D2": layout_D2}
