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
def layout_A():
    """Single 55x55 board in the stock position: switches on the wheel face, everything else on the rib face."""
    t = 1.0
    parts = [B.rect_board("main_board", -p.BOARD_W / 2, p.BOARD_BOTTOM_Y, p.BOARD_W / 2, p.BOARD_TOP_Y, 0.0, t,
                          notes="donor outline, stock position")]
    parts[0] = Part("main_board", B.board_outline(0.0, t, usbc=True), B.PCB_NEW, "board", fixed=False, notes="donor outline, stock position")
    parts += _switch_set(t)
    z = 0.0
    mname, msize = _mcu()
    ucx, ucy, w, l = _pc("UC")
    if OPTIONS["mcu"] == "ESP32S3":
        parts.append(comp(mname, (msize[1], msize[0], msize[2]), ucx, ucy - 3, z, "rear", "rotated to fit UC; antenna fully enclosed by metal"))
    else:
        parts.append(comp(mname, msize, ucx - 8, ucy - 3, z, "rear", "pocket UC"))
        parts.append(comp("microsd", p.MICROSD, ucx + 6.0, ucy - 2, z, "rear", "pocket UC — only UC/LC_upper are ≥14 mm wide"))
    rname, rsize = _radio()
    cx, cy, w, l = _pc("LC_upper")
    parts.append(comp(rname, rsize, cx, cy, z, "rear", "pocket LC_upper; antenna enclosed by metal → coax/flex antenna to the RF window"))
    parts.append(_usbc(0.0, t))
    lx, ly, _, _ = _pc("LL"); rx, ry, _, _ = _pc("LR"); ulx, uly, _, _ = _pc("UL")
    parts.append(comp("codec", p.QFN_SMALL, lx, ly + 13, z, "rear", "pocket LL"))
    parts.append(comp("haptic_drv", p.QFN_SMALL, lx, ly + 6, z, "rear", "pocket LL"))
    parts.append(comp("charger", p.QFN_SMALL, rx, ry + 11, z, "rear", "pocket LR"))
    parts.append(comp("gauge", p.QFN_SMALL, rx, ry + 4, z, "rear", "pocket LR"))
    parts.append(comp("inductor", p.INDUCTOR, rx, ry - 4, z, "rear", "pocket LR"))
    parts.append(comp("psram", p.PSRAM_FLASH, ulx, uly + 4, z, "rear", "pocket UL"))
    parts.append(comp("flash", p.PSRAM_FLASH, ulx, uly - 4, z, "rear", "pocket UL"))
    w, l, h = p.WHEEL_CONN_SIZE
    parts.append(comp("wheel_conn", (w, l, h), -22.0, 44.0, t, "front", "13P 0.3 mm FPC, wheel face top-left (near the flex exit)"))
    parts.append(comp("amoled_conn", (19.0, 4.5, 1.0), 0.0, 49.0, z, "rear", "30P 0.5 mm FPC, rear face top centre"))
    parts.append(comp("jack_conn", (5.0, 3.0, 1.2), 19.0, 2.5, z, "rear", "bottom-right, clear of the pin"))
    # battery: HDD cavity behind the ribs (passes under the divider), best catalogue cell that fits
    hw = p.HDD_SIZE[0] / 2
    batt_box = (-hw, p.HDD_Y0, p.Z_BACK_IN, hw, p.HDD_Y0 + p.HDD_SIZE[1], p.RIB_Z_BACK - p.CLEARANCE)
    bat = cell_in_box(batt_box, "HDD cavity, behind the ribs")
    if bat:
        parts.append(bat)
    # haptic in the freed shell pocket
    x0 = p.BATT_POCKET_POS[0]
    parts.append(haptic("taptic", x0 + 22.5, p.BATT_POCKET_Y0 + 10.0, p.Z_BACK_IN))
    meta = dict(
        boards={"main_board": "55x55 stock"},
        links=[], batt_box=batt_box,
        battery=bat.name if bat else None, haptic="haptic_taptic", antenna=rname,
        wheel_conn=(-22.0, 44.0, t), amoled_conn=(0.0, 49.0, 0.0), jack_conn=(19.0, 2.5, 0.0),
        zones={
            "rear face under ribs": p.RIB_GAP - p.CLEARANCE,
            "rear face in pockets (battery behind)": p.RIB_GAP + p.RIB_T - p.CLEARANCE,
            "rear face in pockets (no battery)": p.POCKET_T_FULL - p.CLEARANCE,
            "wheel face inside 37 mm ring": p.SWITCH_H,
            "wheel face outside wheel (design limit)": p.FRONT_FACE_LOWPROFILE_H,
        })
    return parts, meta


def layout_B():
    """A plus a second board in the HDD cavity, components on the cavity side, board-to-board through a pocket."""
    parts, meta = layout_A()
    # drop the big battery and the UC-only placements that move to board 2
    parts = [q for q in parts if not q.name.startswith("battery_") and q.name not in ("radio_rm2", "radio_bt1026d", "microsd", "psram", "flash", "haptic_taptic")]
    parts.append(haptic("piezo", -21.0, 9.5, 0.0 - p.HAPTIC_PIEZO[2]))   # pocket LL, rear face: the top strip is too short for an LRA once a cell is under the LCD
    t2 = 1.0
    z2_top = p.RIB_Z_BACK - p.CLEARANCE
    z2 = z2_top - t2
    hw = p.HDD_SIZE[0] / 2
    parts.append(B.rect_board("cavity_board", -hw, p.HDD_Y0, hw, p.BOARD_TOP_Y, z2, t2,
                              notes="in the HDD cavity behind the ribs; components toward the shell floor"))
    rname, rsize = _radio()
    parts.append(comp(rname, rsize, -12.0, 40.0, z2, "rear", "cavity side"))
    parts.append(comp("microsd", p.MICROSD, 14.0, 40.0, z2, "rear", "cavity side"))
    parts.append(comp("psram", p.PSRAM_FLASH, -18.0, 20.0, z2, "rear"))
    parts.append(comp("flash", p.PSRAM_FLASH, -18.0, 12.0, z2, "rear"))
    stack = 0.0 - z2_top  # main-board rear face to cavity-board front face
    bw, bl = p.B2B_CONN
    cx, cy, w, l = _pc("UL")
    parts.append(Part("b2b_link", box_at(cx, cy - 8.5, z2_top, (bw, bl), stack), LINK, "candidate", fixed=False,
                      notes=f"mezzanine pair, stack height {stack:.1f} mm, in pocket UL"))
    # battery under the LCD (full-width band below the jack)
    z_lim = (p.AMOLED_Z_BACK if OPTIONS["display"] == "amoled" else p.LCD_Z_BACK) - p.CLEARANCE
    batt_box = (-p.FRAME_IN_W / 2 + p.CLEARANCE, p.DIVIDER_Y1 + p.CLEARANCE, p.Z_BACK_IN,
                p.FRAME_IN_W / 2 - p.CLEARANCE, p.JACK_POS[1] - p.CLEARANCE, z_lim)
    bat = cell_in_box(batt_box, "under the LCD, below the jack")
    if bat:
        parts.append(bat)
    meta.update(boards={"main_board": "55x55 stock", "cavity_board": "54x51 behind the ribs"},
                links=[f"b2b mezzanine, stack {stack:.1f} mm"], battery=bat.name if bat else None, batt_box=batt_box, haptic="haptic_piezo")
    meta["zones"].update({"cavity board, shell side": (z2 - p.Z_BACK_IN) - p.CLEARANCE})
    return parts, meta


def layout_C():
    """L-shaped single rigid board: donor outline plus an extension up under the LCD backplate."""
    parts, meta = layout_A()
    parts = [q for q in parts if q.name not in ("main_board", "radio_rm2", "radio_bt1026d", "microsd", "haptic_taptic") and not q.name.startswith("battery_")]
    parts.append(haptic("piezo", -21.0, 9.5, 0.0 - p.HAPTIC_PIEZO[2]))
    t = 1.0
    ext_y1 = p.JACK_POS[1] - p.CLEARANCE
    parts.insert(0, B.l_board("main_board_L", 0.0, t, ext_y1, -20.0, 20.0,
                              notes="extension passes through the divider and behind the backplate"))
    rname, rsize = _radio()
    parts.append(comp(rname, rsize, -8.0, 70.0, 0.0, "rear", "on the extension, rear face"))
    parts.append(comp("microsd", p.MICROSD, 10.0, 62.0, 0.0, "rear", "on the extension"))
    z_lim = -max(rsize[2], p.MICROSD[2]) - p.CLEARANCE   # behind the extension's rear-face parts
    batt_box = (-p.FRAME_IN_W / 2 + p.CLEARANCE, p.DIVIDER_Y1 + p.CLEARANCE, p.Z_BACK_IN,
                p.FRAME_IN_W / 2 - p.CLEARANCE, p.JACK_POS[1] - p.CLEARANCE, z_lim)
    bat = cell_in_box(batt_box, "behind the extension (what is left under the LCD)")
    if bat:
        parts.append(bat)
    meta.update(boards={"main_board_L": "55x55 + 40x33 extension"}, links=[], battery=bat.name if bat else None, batt_box=batt_box, haptic="haptic_piezo")
    meta["zones"].update({"extension rear face": (0.0 - p.Z_BACK_IN) - p.CLEARANCE,
                          "extension front face (toward backplate)": max(0.0, p.LCD_Z_BACK - t - p.CLEARANCE)})
    return parts, meta


SW_CONN = (-20.0, 38.0)   # wheel connector on the switch board's top-left corner, outside the 40 mm wheel


def _switch_board(t=1.0):
    sb = B.switch_board("switch_board", 0.0, t, notes="five switches, wheel locating holes, wheel connector, USB-C slot, link only")
    parts = [sb] + _switch_set(t)
    w, l, h = p.WHEEL_CONN_SIZE
    parts.append(comp("wheel_conn", (w, l, h), *SW_CONN, t, "front", "13P FPC, front face, corner outside the wheel"))
    return parts


def layout_D1():
    """Switch board in the stock position; main board in the HDD cavity behind the ribs (and under the LCD up to the shell pocket)."""
    t = 1.0
    parts = _switch_board(t)
    z_top = p.RIB_Z_BACK - p.CLEARANCE
    z = z_top - t
    hw = p.HDD_SIZE[0] / 2
    y1 = p.BATT_POCKET_Y0 - 4.0   # leave a ~21.5 mm strip at the top for a cell beside the jack
    parts.append(B.rect_board("main_board", -hw, p.HDD_Y0, hw, y1, z, t, notes="behind the ribs and on under the LCD"))
    mname, msize = _mcu()
    rname, rsize = _radio()
    parts.append(comp(mname, msize, -12.0, 62.0, z, "rear"))
    parts.append(comp(rname, rsize, -14.0, 68.0 if OPTIONS["mcu"] == "ESP32S3" else 40.0, z, "rear", "nearest the RF window on this board"))
    parts.append(comp("microsd", p.MICROSD, 14.0, 62.0, z, "rear"))
    parts.append(comp("psram", p.PSRAM_FLASH, 8.0, 44.0, z, "rear"))
    parts.append(comp("flash", p.PSRAM_FLASH, 16.0, 44.0, z, "rear"))
    for nm, (cx, cy) in {"codec": (-20, 14), "charger": (18, 20), "gauge": (18, 10), "haptic_drv": (-20, 6)}.items():
        parts.append(comp(nm, p.QFN_SMALL, cx, cy, z, "rear"))
    parts.append(comp("inductor", p.INDUCTOR, 18.0, 28.0, z, "rear"))
    parts.append(comp("amoled_conn", (19.0, 4.5, 1.0), 0.0, 57.0, z + t, "front", "FPC folds straight back onto the board front face"))
    parts.append(comp("jack_conn", (5.0, 3.0, 1.2), -20.0, 56.0, z, "rear"))
    # USB-C: mid-mount cannot be on this board (it sits 2.9 mm behind the aperture) → on a tab/flex; show as a box at the aperture
    parts.append(_usbc(0.0, t))   # mid-mount in the switch board's slot; wired on by flex/B2B
    # flex link from the switch board rear face down behind the divider to the main board
    parts.append(Part("flex_link", box(-p.FLEX_LINK_W / 2, p.WHEEL_CY + 15.0, z_top, p.FLEX_LINK_W / 2, p.WHEEL_CY + 19.0, 0.0),
                      FLEX, "candidate", fixed=False, notes="switch board → main board, through pocket UC"))
    z_lim = (p.AMOLED_Z_BACK if OPTIONS["display"] == "amoled" else p.LCD_Z_BACK) - p.CLEARANCE
    x0 = p.BATT_POCKET_POS[0]
    batt_box = (x0 + p.CLEARANCE, y1 + p.CLEARANCE, p.Z_BACK_IN, p.FRAME_IN_W / 2 - p.CLEARANCE, p.INT_Y1 - p.FRAME_WALL_T - p.CLEARANCE, z_lim)
    bat = cell_in_box(batt_box, "shell pocket strip only (main board takes the cavity)")
    if bat:
        parts.append(bat)
    parts.append(haptic("piezo", -20.0, 26.0, z - p.HAPTIC_PIEZO[2]))
    meta = dict(boards={"switch_board": f"{p.SWITCH_BOARD_SQ[0]:.0f}x{p.SWITCH_BOARD_SQ[1]:.0f}", "main_board": f"54 x {y1 - p.HDD_Y0:.0f} behind ribs + under LCD"},
                links=["flex, switch board → main board (through pocket UC); carries USB-C from the switch board"],
                battery=bat.name if bat else None, batt_box=batt_box, haptic="haptic_piezo", antenna=rname,
                wheel_conn=(*SW_CONN, t), amoled_conn=(0.0, 57.0, z + t), jack_conn=(-20.0, 56.0, z),
                zones={"main board, shell side (lower half)": (z - p.Z_BACK_IN) - p.CLEARANCE,
                       "main board, front side under LCD": (p.AMOLED_Z_BACK if OPTIONS["display"] == "amoled" else p.LCD_Z_BACK) - z_top - p.CLEARANCE,
                       "switch board rear face": p.RIB_GAP - p.CLEARANCE,
                       "wheel face inside 37 mm ring": p.SWITCH_H})
    return parts, meta


def layout_D2():
    """Switch board in the stock position; main board under the LCD; battery behind the ribs."""
    t = 1.0
    parts = _switch_board(t)
    z_back_lim = p.AMOLED_Z_BACK if OPTIONS["display"] == "amoled" else p.LCD_Z_BACK
    z_top = z_back_lim - p.CLEARANCE
    z = z_top - t
    hw = p.FRAME_IN_W / 2 - p.CLEARANCE
    y0, y1 = p.DIVIDER_Y1 + p.CLEARANCE, p.JACK_POS[1] - p.CLEARANCE
    parts.append(B.rect_board("main_board", -hw, y0, hw, y1, z, t, notes="under the LCD, below the jack"))
    mname, msize = _mcu()
    rname, rsize = _radio()
    yc = (y0 + y1) / 2
    y_clear = p.HDD_Y0 + p.HDD_SIZE[1] - 8.0   # rear-face parts stay above the battery's top edge (y ≥ 64)
    parts.append(comp(rname, rsize, -17.0, 78.0, z, "rear", "top-left corner, nearest the RF window"))
    parts.append(comp(mname, msize, -3.0, 70.0, z, "rear"))
    parts.append(comp("psram", p.PSRAM_FLASH, -3.0, 79.0, z, "rear"))
    parts.append(comp("flash", p.PSRAM_FLASH, 4.0, 79.0, z, "rear"))
    parts.append(comp("microsd", p.MICROSD, 18.0, 75.0, z, "rear"))
    for nm, (cx, cy) in {"haptic_drv": (6, 66), "codec": (12, 66), "charger": (18, 66), "gauge": (24, 66)}.items():
        parts.append(comp(nm, p.QFN_SMALL, cx, cy, z, "rear"))
    parts.append(comp("inductor", p.INDUCTOR, -24.0, 67.0, z, "rear"))
    parts.append(comp("amoled_conn", (19.0, 4.5, 1.0), 0.0, 57.0, z, "rear", "rear face: no room on the front; the FPC wraps the board's bottom edge"))
    parts.append(comp("jack_conn", (5.0, 3.0, 1.2), 24.0, 84.0, z, "rear", "top-right; the donor flex reaches round"))
    parts.append(_usbc(0.0, t))   # mid-mount in the switch board's slot; USB lines ride the link flex
    fw = p.FLEX_LINK_W / 2
    zf = p.FRAME_DIVIDER_Z_BACK                     # flex (0.15 mm) lies directly behind the divider
    flex = (box(-fw, p.WHEEL_CY + 15.0, zf - 0.15, fw, p.WHEEL_CY + 19.0, 0.0)         # drop through pocket UC
            .union(box(-fw, p.WHEEL_CY + 15.0, zf - 0.15, fw, y0 + 4.0, zf))            # run behind the divider
            .union(box(-fw, y0 + 2.0, zf - 0.15, fw, y0 + 4.0, z)))                     # rise to the main board
    parts.append(Part("flex_link", flex, FLEX, "candidate", fixed=False, notes="switch board → main board, behind the divider"))
    hw2 = p.HDD_SIZE[0] / 2
    batt_box = (-hw2, p.HDD_Y0, p.Z_BACK_IN, hw2, y_clear - p.CLEARANCE, min(p.RIB_Z_BACK, zf - 0.15) - 0.25)
    bat = cell_in_box(batt_box, "behind the ribs, passing under the divider; 605060 if the cavity really is 7 mm")
    if bat:
        parts.append(bat)
    parts.append(haptic("lra", 20.0, p.INT_Y1 - p.FRAME_WALL_T - 5.3, p.Z_BACK_IN))
    meta = dict(boards={"switch_board": f"{p.SWITCH_BOARD_SQ[0]:.0f}x{p.SWITCH_BOARD_SQ[1]:.0f}", "main_board": f"{2*hw:.1f} x {y1-y0:.1f} under the LCD"},
                links=["flex, switch board → main board (behind the divider); carries USB-C from the switch board"],
                battery=bat.name if bat else None, batt_box=batt_box, haptic="haptic_lra", antenna=rname,
                wheel_conn=(*SW_CONN, t), amoled_conn=(0.0, 57.0, z), jack_conn=(24.0, 84.0, z),
                zones={"main board, shell side": (z - p.Z_BACK_IN) - p.CLEARANCE,
                       "main board, front side (toward backplate)": 0.0,
                       "switch board rear face": p.RIB_GAP - p.CLEARANCE,
                       "wheel face inside 37 mm ring": p.SWITCH_H})
    return parts, meta


LAYOUTS = {"A": layout_A, "B": layout_B, "C": layout_C, "D1": layout_D1, "D2": layout_D2}
