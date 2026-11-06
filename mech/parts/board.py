"""Donor logic board 820-2437 (outline + holes), its switch zone, and the
generic board builders used by the candidate layouts."""
import cadquery as cq
from .. import params as p
from .common import Part, box, box_at, cyl, rounded_box

PCB = (0.15, 0.45, 0.25)
PCB_NEW = (0.2, 0.3, 0.7)
SWITCH = (0.9, 0.75, 0.2)


def usbc_cutout(z0, t):
    """Slot for the mid-mount receptacle at the dock aperture."""
    w, l, _ = p.USBC
    c = p.CLEARANCE
    return box(-w / 2 - c, p.BOARD_BOTTOM_Y - 1, z0 - 1, w / 2 + c, p.BOARD_BOTTOM_Y + l + c, z0 + t + 1)


def board_outline(z0=0.0, t=None, usbc=False):
    """Donor outline at the stock position (from the scan): 55 × 45 with the top-centre tab,
    pin holes, screw holes (shared with the wheel locating holes), top screw holes."""
    t = p.BOARD_T if t is None else t
    x0, x1 = -p.BOARD_W / 2, p.BOARD_W / 2
    y0, y1 = p.BOARD_BOTTOM_Y, p.BOARD_TOP_Y
    b = rounded_box(x0, y0, z0, x1, y1, z0 + t, p.BOARD_CORNER_R)
    tw, th = p.BOARD_TAB
    b = b.union(rounded_box(-tw / 2, y1 - 1.0, z0, tw / 2, y1 + th, z0 + t, 0.8))
    for sx in (-p.PIN_PITCH / 2, p.PIN_PITCH / 2):
        b = b.cut(cyl(sx, 0.0, z0 - 1, p.PIN_D + 0.2, t + 2))
    holes = list(p.SCREW_POS)
    if not p.SCREWS_ARE_WHEEL_HOLES:
        holes += [(p.WHEEL_CX - p.WHEEL_LOC_HOLE_X, p.WHEEL_CY), (p.WHEEL_CX + p.WHEEL_LOC_HOLE_X, p.WHEEL_CY)]
    tx, ty = p.TOP_SCREW_POS
    holes += [(-tx, ty), (tx, ty)]
    for hx, hy in holes:
        b = b.cut(cyl(hx, hy, z0 - 1, p.SCREW_D, t + 2))
    if usbc:
        b = b.cut(usbc_cutout(z0, t))
    else:
        nw, nd = p.BOARD_DOCK_NOTCH
        b = b.cut(box(-nw / 2, y0 - 1, z0 - 1, nw / 2, y0 + nd, z0 + t + 1))
    return b


def donor_board():
    return Part("logic_board_donor", board_outline(), PCB, "board",
                notes="820-2437 outline; rear face at Z=0 faces the ribs")


def switch_zone(z_face=None, name_prefix="sw"):
    """Five 6x6 tact switches on the wheel-facing face (face at z_face, default donor front)."""
    z = p.BOARD_FRONT_Z if z_face is None else z_face
    cx, cy, r = p.WHEEL_CX, p.WHEEL_CY, p.SWITCH_RADIUS
    out = []
    for nm, (dx, dy) in {"C": (0, 0), "N": (0, r), "S": (0, -r), "E": (r, 0), "W": (-r, 0)}.items():
        h = p.SWITCH_H_C if nm == "C" else p.SWITCH_H
        out.append(Part(f"{name_prefix}_{nm}", box_at(cx + dx, cy + dy, z, (p.SWITCH_BODY, p.SWITCH_BODY), h),
                        SWITCH, "front", notes="6x6 SMD tact; must match donor centres"))
    ring = (cyl(cx, cy, z, p.ADHESIVE_RING_D, p.RING_TO_BOARD_GAP)
            .cut(cyl(cx, cy, z - 1, p.ADHESIVE_RING_D - 2.0, p.RING_TO_BOARD_GAP + 2)))
    out.append(Part(f"{name_prefix}_adhesive_ring", ring, (0.8, 0.6, 0.3), "front", notes="37 mm adhesive ring"))
    return out


def stock_connectors():
    """Donor connector bodies on the rear face (for reference only)."""
    out = []
    w, l, h = p.WHEEL_CONN_SIZE
    out.append(Part("donor_wheel_conn", box_at(*p.WHEEL_CONN_POS, 0.0, (w, l), -h), (0.3, 0.3, 0.3),
                    "board", notes="13P wheel FPC connector, rear face (flex wraps the left board edge)"))
    out.append(Part("donor_lcd_zif", box_at(*p.LCD_ZIF_POS, 0.0, (22.0, 4.0), -1.5), (0.3, 0.3, 0.3), "board",
                    notes="wide LCD ZIF, rear face, top centre"))
    out.append(Part("donor_jack_conn", box_at(*p.JACK_CONN_POS, 0.0, p.JACK_CONN_SIZE[:2], -p.JACK_CONN_SIZE[2]), (0.3, 0.3, 0.3), "board",
                    notes="headphone/hold flex connector, rear face, beside the dock"))
    return out


def rect_board(name, x0, y0, x1, y1, z0, t=1.0, colour=PCB_NEW, notes=""):
    return Part(name, rounded_box(x0, y0, z0, x1, y1, z0 + t, 1.0), colour, "board", fixed=False, notes=notes)


def switch_board(name, z0, t=1.0, notes=""):
    """Layout D switch board: round (SWITCH_BOARD_D) or square (SWITCH_BOARD_SQ) with the pin holes,
    wheel locating holes and the USB-C slot."""
    cx, cy = p.WHEEL_CX, p.WHEEL_CY
    if p.SWITCH_BOARD_SHAPE == "full":
        return Part(name, board_outline(z0, t, usbc=True), PCB_NEW, "board", fixed=False, notes=notes)
    if p.SWITCH_BOARD_SHAPE == "round":
        b = cyl(cx, cy, z0, p.SWITCH_BOARD_D, t)
    else:
        w, l = p.SWITCH_BOARD_SQ
        b = rounded_box(-w / 2, p.BOARD_BOTTOM_Y, z0, w / 2, p.BOARD_BOTTOM_Y + l, z0 + t, p.BOARD_CORNER_R)
    for sx in (-p.WHEEL_LOC_HOLE_X, p.WHEEL_LOC_HOLE_X):
        b = b.cut(cyl(cx + sx, cy, z0 - 1, p.WHEEL_LOC_HOLE_D, t + 2))
    for sx in (-p.PIN_PITCH / 2, p.PIN_PITCH / 2):
        b = b.cut(cyl(sx, 0.0, z0 - 1, p.PIN_D + 0.2, t + 2))
    b = b.cut(usbc_cutout(z0, t))
    return Part(name, b, PCB_NEW, "board", fixed=False, notes=notes)


def l_board(name, z0, t, ext_y1, ext_x0, ext_x1, notes=""):
    """Donor outline plus a rectangular extension up to ext_y1 (layout C)."""
    main = board_outline(z0, t, usbc=True)
    ext = rounded_box(ext_x0, p.BOARD_TOP_Y - 2, z0, ext_x1, ext_y1, z0 + t, 1.0)
    return Part(name, main.union(ext), PCB_NEW, "board", fixed=False, notes=notes)
