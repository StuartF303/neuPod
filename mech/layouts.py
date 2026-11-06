"""Candidate layouts A–D.  Each builder returns (parts, meta).

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
    return [("behind the ribs, below the divider", (-hw, p.HDD_Y0, p.Z_BACK_IN, hw, p.DIVIDER_Y0 - c, p.RIB_Z_BACK - c)),
            ("behind the ribs, crossing under the divider", (-hw, p.HDD_Y0, p.Z_BACK_IN, hw, y_hi - c, p.FRAME_DIVIDER_Z_BACK - c))]


def lcd_z_back():
    return p.AMOLED_Z_BACK if OPTIONS["display"] == "amoled" else p.LCD_Z_BACK


def under_lcd_boxes(z_top):
    """Battery boxes under the LCD: full width below the jack module, or left of the jack up to the plastic insert."""
    c = p.CLEARANCE
    xi = p.FRAME_IN_W / 2 - c
    return [("under the LCD, below the jack", (-xi, p.DIVIDER_Y1 + c, p.Z_BACK_IN, xi, p.JACK_POS[1] - c, z_top)),
            ("under the LCD, left of the jack up to the insert", (-xi, p.DIVIDER_Y1 + c, p.Z_BACK_IN, p.JACK_POS[0] - c, p.INSERT_Y0 - c, z_top))]


def haptic(kind, cx, cy, z_floor, orient="XY"):
    size = {"taptic": p.HAPTIC_TAPTIC, "lra": p.HAPTIC_LRA, "piezo": p.HAPTIC_PIEZO}[kind]
    w, l, t = size
    if orient == "YX":
        w, l = l, w
    return Part(f"haptic_{kind}", box_at(cx, cy, z_floor, (w, l), t), HAPT, "candidate", fixed=False,
                notes=f"{kind} {size}")


def _pc(name):
    x0, y0, x1, y1 = pocket_grid()[name]
    return (x0 + x1) / 2, (y0 + y1) / 2, x1 - x0, y1 - y0


def _mcu():
    return {"RP2350B": ("mcu_rp2350b", p.MCU_RP2350B), "ESP32S3": ("mcu_esp32s3", p.MCU_ESP32S3)}[OPTIONS["mcu"]]


def _radio():
    return {"RM2": ("radio_rm2", p.RADIO_RM2), "BT1026D": ("radio_bt1026d", p.RADIO_BT1026D)}[OPTIONS["radio"]]


def _usbc(z_board_rear, t):
    """Mid-mount receptacle straddling the board at the dock aperture, mating face at the board bottom edge."""
    w, l, h = p.USBC
    zc = z_board_rear + t / 2
    return Part("usbc_midmount", box_at(0.0, p.BOARD_BOTTOM_Y + l / 2, zc - h / 2, (w, l), h), COMP, "candidate",
                fixed=False, notes="centred in the dock aperture")


def _donor_conns(z_rear=0.0):
    """Wheel and jack/hold connectors at the donor positions on the stock-position board's rear face (reused flexes)."""
    return [comp("wheel_conn", p.WHEEL_CONN_SIZE, *p.WHEEL_CONN_POS, z_rear, "rear", "13P 0.3 mm FPC — donor position, fixed (reused wheel flex)"),
            comp("jack_conn", p.JACK_CONN_SIZE, *p.JACK_CONN_POS, z_rear, "rear", "jack/hold FPC — donor position, fixed (reused flex)")]


def _switch_set(z_face):
    return B.switch_zone(z_face, "sw")


def _common_small(z, face, pocket_for):
    """Codec, charger, gauge, haptic driver, inductor, PSRAM+flash in named pockets."""
    out = []
    q = p.QFN_SMALL
    cx, cy, w, l = _pc(pocket_for["codec"]); out.append(comp("codec", q, cx, cy + 6, z, face))
    cx, cy, w, l = _pc(pocket_for["charger"]); out.append(comp("charger", q, cx, cy - 6, z, face))
    cx, cy, w, l = _pc(pocket_for["gauge"]); out.append(comp("gauge", q, cx, cy + 0, z, face))
    cx, cy, w, l = _pc(pocket_for["haptic_drv"]); out.append(comp("haptic_drv", q, cx, cy - 10, z, face))
    cx, cy, w, l = _pc(pocket_for["inductor"]); out.append(comp("inductor", p.INDUCTOR, cx, cy + 8, z, face))
    cx, cy, w, l = _pc(pocket_for["mem"]); out.append(comp("psram", p.PSRAM_FLASH, cx - 4, cy - 7, z, face))
    out.append(comp("flash", p.PSRAM_FLASH, cx + 4, cy - 7, z, face))
    return out


# --------------------------------------------------------------------------- #
AMOLED_CONN = (19.0, 4.5, 1.0)
AMOLED_CONN_Y = 38.9   # rear face, top of pocket UC: the tab above sits on the divider (no rear-face height there)


def _front_board_rear_common(z, with_radio=True, with_sd=True):
    """Rear-face parts on the stock-position board, placed in the scanned pockets."""
    out = []
    ucx, ucy, _, _ = _pc("UC")
    mname, msize = _mcu()
    if OPTIONS["mcu"] == "ESP32S3":
        out.append(comp(mname, (msize[1], msize[0], msize[2]), ucx, ucy, z, "rear", "pocket UC, rotated; antenna inside metal"))
    else:
        out.append(comp(mname, msize, ucx - 8.5, ucy - 1.45, z, "rear", "pocket UC, above the left screw boss"))
        if with_sd:
            out.append(comp("microsd", p.MICROSD, ucx + 6.5, ucy - 1.0, z, "rear", "pocket UC — the only pocket ≥ 14 mm both ways"))
    if with_radio:
        rname, rsize = _radio()
        rx, ry, _, _ = _pc("LR")
        out.append(comp(rname, (rsize[1], rsize[0], rsize[2]), rx, ry + 1.0, z, "rear", "pocket LR, rotated; antenna inside metal → flex antenna to the RF window"))
    lx = _pc("LL")[0]
    out.append(comp("codec", p.QFN_SMALL, lx, 17.5, z, "rear", "pocket LL"))
    out.append(comp("haptic_drv", p.QFN_SMALL, lx, 11.5, z, "rear", "pocket LL"))
    out.append(comp("charger", p.QFN_SMALL, -6.0, 16.5, z, "rear", "pocket LC_upper"))
    out.append(comp("gauge", p.QFN_SMALL, 0.0, 16.5, z, "rear", "pocket LC_upper"))
    out.append(comp("inductor", p.INDUCTOR, 6.5, 16.5, z, "rear", "pocket LC_upper"))
    ulx = _pc("UL")[0]
    out.append(comp("psram", p.PSRAM_FLASH, ulx, 28.5, z, "rear", "pocket UL"))
    out.append(comp("flash", p.PSRAM_FLASH, ulx, 34.0, z, "rear", "pocket UL"))
    return out


def layout_A():
    """Single board, donor outline, stock position: switches on the wheel face, everything else in the pockets."""
    t = 1.0
    parts = [Part("main_board", B.board_outline(0.0, t, usbc=True), B.PCB_NEW, "board", fixed=False,
                  notes="donor outline (scan): 55 × 45 + top tab")]
    parts += _switch_set(t)
    z = 0.0
    parts += _front_board_rear_common(z)
    parts.append(_usbc(0.0, t))
    parts += _donor_conns(z)
    amo = (0.0, AMOLED_CONN_Y)
    parts.append(comp("amoled_conn", AMOLED_CONN, *amo, z, "rear", "30P 0.5 mm FPC, rear face, top of pocket UC (where the LCD ZIF was)"))
    bat, batt_box, blabel = best_cell(cavity_boxes())
    if bat:
        parts.append(bat)
    x0 = p.BATT_POCKET_POS[0]
    parts.append(haptic("taptic", x0 + 22.5, p.BATT_POCKET_Y0 + 10.0, p.Z_BACK_IN))
    rname = _radio()[0]
    meta = dict(
        boards={"main_board": "55x45 donor outline"},
        links=[], batt_box=batt_box, batt_where=blabel,
        battery=bat.name if bat else None, haptic="haptic_taptic", antenna=rname,
        wheel_conn=(*p.WHEEL_CONN_POS, 0.0), amoled_conn=(*amo, 0.0), jack_conn=(*p.JACK_CONN_POS, 0.0),
        zones={
            "rear face under ribs": p.RIB_GAP - p.CLEARANCE,
            "rear face in pockets (cell behind)": p.RIB_GAP + p.RIB_T - p.CLEARANCE,
            "rear face in pockets (no cell behind)": p.POCKET_T_FULL - p.CLEARANCE,
            "wheel face inside 37 mm ring": p.SWITCH_H,
            "wheel face outside wheel (design limit)": p.FRONT_FACE_LOWPROFILE_H,
        })
    return parts, meta


def layout_B():
    """A plus a second board in the HDD cavity, components toward the shell, mezzanine through pocket LL."""
    parts, meta = layout_A()
    parts = [q for q in parts if not q.name.startswith("battery_") and q.name not in ("radio_rm2", "radio_bt1026d", "microsd", "psram", "flash", "haptic_taptic")]
    t2 = 1.0
    z2_top = p.RIB_Z_BACK - p.CLEARANCE
    z2 = z2_top - t2
    hw = p.HDD_SIZE[0] / 2
    y_top = p.DIVIDER_Y0 - p.CLEARANCE            # stops below the divider (it stands proud at the rear)
    parts.append(B.rect_board("cavity_board", -hw, p.HDD_Y0, hw, y_top, z2, t2,
                              notes="behind the ribs, below the divider; components toward the shell floor"))
    rname, rsize = _radio()
    parts.append(comp(rname, rsize, -12.0, 31.0, z2, "rear", "cavity side"))
    parts.append(comp("microsd", p.MICROSD, 12.0, 31.0, z2, "rear", "cavity side"))
    parts.append(comp("psram", p.PSRAM_FLASH, -18.0, 14.0, z2, "rear"))
    parts.append(comp("flash", p.PSRAM_FLASH, -18.0, 7.0, z2, "rear"))
    parts.append(haptic("piezo", 16.0, 12.0, z2 - p.HAPTIC_PIEZO[2]))
    stack = 0.0 - z2_top
    bw, bl = p.B2B_CONN
    lx = _pc("LL")[0]
    parts.append(Part("b2b_link", box_at(lx, 6.0, z2_top, (bw, bl), stack), LINK, "candidate", fixed=False,
                      notes=f"mezzanine pair, stack {stack:.1f} mm, pocket LL"))
    bat, batt_box, blabel = best_cell(under_lcd_boxes(lcd_z_back() - p.CLEARANCE))
    if bat:
        parts.append(bat)
    meta.update(boards={"main_board": "55x45 donor outline", "cavity_board": f"54x{y_top - p.HDD_Y0:.0f} behind the ribs"},
                links=[f"b2b mezzanine, stack {stack:.1f} mm"], battery=bat.name if bat else None, batt_box=batt_box,
                batt_where=blabel, haptic="haptic_piezo")
    meta["zones"].update({"cavity board, shell side": (z2 - p.Z_BACK_IN) - p.CLEARANCE})
    return parts, meta


def layout_C():
    """Single rigid board: donor outline plus an extension over the divider and under the LCD backplate.
    The divider is flush with the ribs on the board side, so the board can run straight over it."""
    parts, meta = layout_A()
    parts = [q for q in parts if q.name not in ("main_board", "radio_rm2", "radio_bt1026d", "microsd") and not q.name.startswith("battery_")]
    t = 1.0
    ext_y1 = p.JACK_POS[1] - p.CLEARANCE
    parts.insert(0, B.l_board("main_board_T", 0.0, t, ext_y1, -p.FRAME_IN_W / 2 + p.CLEARANCE, p.FRAME_IN_W / 2 - p.CLEARANCE,
                              notes="extension runs over the divider front and behind the backplate"))
    rname, rsize = _radio()
    parts.append(comp(rname, rsize, -15.0, 64.0, 0.0, "rear", "on the extension, rear face, left — nearest the insert"))
    parts.append(comp("microsd", p.MICROSD, 14.0, 60.0, 0.0, "rear", "on the extension"))
    z_lim = -max(rsize[2], p.MICROSD[2]) - p.CLEARANCE
    bat, batt_box, blabel = best_cell(under_lcd_boxes(z_lim) + cavity_boxes())
    if bat:
        parts.append(bat)
    meta.update(boards={"main_board_T": f"55x45 + {p.FRAME_IN_W:.0f}x{ext_y1 - p.BOARD_TOP_Y:.0f} extension"}, links=[],
                battery=bat.name if bat else None, batt_box=batt_box, batt_where=blabel)
    meta["zones"].update({"extension rear face": (0.0 - p.Z_BACK_IN) - p.CLEARANCE,
                          "extension front face (toward backplate)": max(0.0, lcd_z_back() - t - p.CLEARANCE)})
    return parts, meta


def _sb_desc():
    if p.SWITCH_BOARD_SHAPE == "full":
        return f"{p.BOARD_W:.0f}x{p.BOARD_L:.0f} full donor outline"
    if p.SWITCH_BOARD_SHAPE == "round":
        return f"Ø{p.SWITCH_BOARD_D:.0f}"
    return f"{p.SWITCH_BOARD_SQ[0]:.0f}x{p.SWITCH_BOARD_SQ[1]:.0f}"


def _switch_board(t=1.0):
    sb = B.switch_board("front_board", 0.0, t, notes="full donor outline: five switches, screw/locating holes, wheel and jack connectors, USB-C")
    return [sb] + _switch_set(t) + _donor_conns(0.0)


def layout_D1():
    """Front board (donor outline) + main board in the HDD cavity behind the ribs, below the divider."""
    t = 1.0
    parts = _switch_board(t)
    z_top = p.RIB_Z_BACK - p.CLEARANCE
    z = z_top - t
    hw = p.HDD_SIZE[0] / 2
    y1 = p.DIVIDER_Y0 - p.CLEARANCE
    parts.append(B.rect_board("main_board", -hw, p.HDD_Y0, hw, y1, z, t, notes="behind the ribs, below the divider"))
    mname, msize = _mcu()
    rname, rsize = _radio()
    parts.append(comp(mname, msize, -12.0, 30.0, z, "rear"))
    parts.append(comp(rname, rsize, 12.0, 32.0, z, "rear"))
    parts.append(comp("microsd", p.MICROSD, 12.0, 14.0, z, "rear"))
    parts.append(comp("psram", p.PSRAM_FLASH, -14.0, 18.0, z, "rear"))
    parts.append(comp("flash", p.PSRAM_FLASH, -6.0, 18.0, z, "rear"))
    for nm, (cx, cy) in {"codec": (-20, 10), "charger": (-12, 6), "gauge": (-6, 6), "haptic_drv": (-20, 4)}.items():
        parts.append(comp(nm, p.QFN_SMALL, cx, cy, z, "rear"))
    parts.append(comp("inductor", p.INDUCTOR, 0.0, 6.0, z, "rear"))
    parts.append(_usbc(0.0, t))
    fy = AMOLED_CONN_Y - 9.0
    parts.append(Part("flex_link", box(-p.FLEX_LINK_W / 2, fy, z_top, p.FLEX_LINK_W / 2, fy + 4.0, 0.0),
                      FLEX, "candidate", fixed=False, notes="front board → main board, through pocket UC"))
    parts.append(comp("amoled_conn", AMOLED_CONN, 0.0, AMOLED_CONN_Y, 0.0, "rear", "on the front board, top of pocket UC; display lines ride the link"))
    bat, batt_box, blabel = best_cell(under_lcd_boxes(lcd_z_back() - p.CLEARANCE))
    if bat:
        parts.append(bat)
    parts.append(haptic("lra", 23.0, 66.0, p.Z_BACK_IN))
    meta = dict(boards={"front_board": _sb_desc(), "main_board": f"54 x {y1 - p.HDD_Y0:.0f} behind the ribs"},
                links=["flex, front board → main board (through pocket UC); USB-C, display and jack lines ride it"],
                battery=bat.name if bat else None, batt_box=batt_box, batt_where=blabel, haptic="haptic_lra", antenna=rname,
                wheel_conn=(*p.WHEEL_CONN_POS, 0.0), amoled_conn=(0.0, AMOLED_CONN_Y, 0.0), jack_conn=(*p.JACK_CONN_POS, 0.0),
                zones={"main board, shell side": (z - p.Z_BACK_IN) - p.CLEARANCE,
                       "front board rear face": p.RIB_GAP - p.CLEARANCE,
                       "wheel face inside 37 mm ring": p.SWITCH_H})
    return parts, meta


def layout_D2():
    """Front board (donor outline) + main board under the LCD (T-shape round the jack), cell behind the ribs."""
    t = 1.0
    parts = _switch_board(t)
    z_top = lcd_z_back() - p.CLEARANCE
    z = z_top - t
    c = p.CLEARANCE
    xi = p.FRAME_IN_W / 2 - c
    y0, y1 = p.DIVIDER_Y1 + c, p.JACK_POS[1] - c
    y2 = p.INSERT_Y0 - c
    xj = p.JACK_POS[0] - c
    mb = B.rect_board("main_board", -xi, y0, xi, y1, z, t).shape.union(B.rect_board("x", -xi, y1 - 2.0, xj, y2, z, t).shape)
    parts.append(Part("main_board", mb, B.PCB_NEW, "board", fixed=False, notes="under the LCD; runs up beside the jack to the plastic insert"))
    mname, msize = _mcu()
    rname, rsize = _radio()
    parts.append(comp(rname, rsize, -17.0, 83.0, z, "rear", "top-left, against the plastic insert (RF window)"))
    parts.append(comp(mname, msize, -1.0, 62.0, z, "rear"))
    parts.append(comp("psram", p.PSRAM_FLASH, -14.0, 54.0, z, "rear"))
    parts.append(comp("flash", p.PSRAM_FLASH, -7.0, 54.0, z, "rear"))
    parts.append(comp("microsd", p.MICROSD, 17.0, 62.0, z, "rear"))
    for nm, (cx, cy) in {"haptic_drv": (-24, 58), "codec": (-24, 64), "charger": (14, 50), "gauge": (20, 50)}.items():
        parts.append(comp(nm, p.QFN_SMALL, cx, cy, z, "rear"))
    parts.append(comp("inductor", p.INDUCTOR, -24.0, 70.0, z, "rear"))
    parts.append(comp("amoled_conn", AMOLED_CONN, 0.0, y0 + 2.6, z, "rear", "rear face at the bottom edge; FPC wraps the edge"))
    parts.append(_usbc(0.0, t))
    fw = p.FLEX_LINK_W / 2
    parts.append(Part("flex_link", box(-fw, p.BOARD_TOP_Y - 2.0, 1.0, fw, y0 + 2.0, 1.15), FLEX, "candidate", fixed=False,
                      notes="front board → main board, a 4 mm hop across the divider front face"))
    bat, batt_box, blabel = best_cell(cavity_boxes())
    if bat:
        parts.append(bat)
    parts.append(haptic("lra", 3.0, 72.6, p.Z_BACK_IN))
    meta = dict(boards={"front_board": _sb_desc(), "main_board": "T under the LCD"},
                links=["short flex over the divider front face; USB-C, wheel and jack lines ride it"],
                battery=bat.name if bat else None, batt_box=batt_box, batt_where=blabel, haptic="haptic_lra", antenna=rname,
                wheel_conn=(*p.WHEEL_CONN_POS, 0.0), amoled_conn=(0.0, y0 + 2.6, z), jack_conn=(*p.JACK_CONN_POS, 0.0),
                zones={"main board, shell side": (z - p.Z_BACK_IN) - p.CLEARANCE,
                       "main board, front side (toward backplate)": 0.0,
                       "front board rear face": p.RIB_GAP - p.CLEARANCE,
                       "wheel face inside 37 mm ring": p.SWITCH_H})
    return parts, meta


LAYOUTS = {"A": layout_A, "B": layout_B, "C": layout_C, "D1": layout_D1, "D2": layout_D2}
