"""Cast mid-frame: outer walls, LCD window divider, ribbed grid behind the board,
locating pins, screw bosses, dock bar.  The grid is recessed: the board sits in
FRONT of the ribs (rib top at -RIB_GAP), components in the pockets between them."""
import cadquery as cq
from .. import params as p
from .common import Part, box, cyl

CAST = (0.62, 0.64, 0.66)
PIN = (0.9, 0.2, 0.2)


def _rib_y(y):
    return box(-p.BOARD_W / 2, y - p.RIB_W / 2, p.RIB_Z_BACK, p.BOARD_W / 2, y + p.RIB_W / 2, p.RIB_Z_TOP)


def _rib_x(x, y0, y1):
    return box(x - p.RIB_W / 2, y0, p.RIB_Z_BACK, x + p.RIB_W / 2, y1, p.RIB_Z_TOP)


def pocket_grid():
    """Return dict name -> (x0, y0, x1, y1) of the six pockets (plan view, board rear face)."""
    xl0 = -p.BOARD_W / 2
    xl1 = p.VRIB_L_X - p.RIB_W / 2
    xc0 = p.VRIB_L_X + p.RIB_W / 2
    xc1 = p.VRIB_R_X - p.RIB_W / 2
    xr0 = p.VRIB_R_X + p.RIB_W / 2
    xr1 = p.BOARD_W / 2
    yb = p.BOARD_BOTTOM_Y
    ys0, ys1 = p.SCREW_RIB_Y - p.RIB_W / 2, p.SCREW_RIB_Y + p.RIB_W / 2
    yc0, yc1 = p.CENTRE_SPLIT_Y - p.RIB_W / 2, p.CENTRE_SPLIT_Y + p.RIB_W / 2
    yt = p.BOARD_TOP_Y
    return {
        "UL": (xl0, ys1, xl1, yt),
        "UC": (xc0, ys1, xc1, yt),
        "UR": (xr0, ys1, xr1, yt),
        "LL": (xl0, yb, xl1, ys0),
        "LR": (xr0, yb, xr1, ys0),
        "LC_upper": (xc0, yc1, xc1, ys0),
        "LC_lower": (xc0, yb, xc1, yc0),
    }


def midframe():
    xo = p.INT_W / 2
    y0, y1 = p.INT_Y0, p.INT_Y1
    zb, zf = p.RIB_Z_BACK, p.Z_FRONT_IN
    zw = p.FRAME_Z_BACK
    # outer walls (full depth to the shell floor if FRAME_WALLS_TO_FLOOR)
    frame = box(-xo, y0, zw, xo, y1, zf).cut(
        box(-xo + p.FRAME_WALL_T, y0 + p.FRAME_WALL_T, zw - 1, xo - p.FRAME_WALL_T, y1 - p.FRAME_WALL_T, zf + 1))
    # bottom bar carrying the dock cut-out
    bar = box(-xo, y0, zb, xo, p.BOARD_BOTTOM_Y, zf).cut(
        box(-p.DOCK_AP_W / 2, y0 - 1, zb, p.DOCK_AP_W / 2, p.BOARD_BOTTOM_Y + 1, zb + p.DOCK_AP_H))
    frame = frame.union(bar)
    # divider between grid and LCD window (full depth assumed)
    frame = frame.union(box(-xo, p.DIVIDER_Y0, p.FRAME_DIVIDER_Z_BACK, xo, p.DIVIDER_Y1, zf))
    # ribbed grid behind the board
    ribs = (_rib_y(p.SCREW_RIB_Y)
            .union(_rib_x(p.VRIB_L_X, p.BOARD_BOTTOM_Y, p.BOARD_TOP_Y))
            .union(_rib_x(p.VRIB_R_X, p.BOARD_BOTTOM_Y, p.BOARD_TOP_Y))
            .union(box(p.VRIB_L_X, p.CENTRE_SPLIT_Y - p.RIB_W / 2, zb, p.VRIB_R_X,
                       p.CENTRE_SPLIT_Y + p.RIB_W / 2, p.RIB_Z_TOP)))
    # perimeter ledge of the grid (board edge support), thin
    ledge = (box(-p.BOARD_W / 2 - 0.8, p.BOARD_BOTTOM_Y, zb, p.BOARD_W / 2 + 0.8, p.BOARD_TOP_Y, p.RIB_Z_TOP)
             .cut(box(-p.BOARD_W / 2, p.BOARD_BOTTOM_Y - 1, zb - 1, p.BOARD_W / 2, p.BOARD_TOP_Y + 1, zf)))
    frame = frame.union(ribs).union(ledge)
    # screw bosses from rib top to the board rear face
    for sx in (-p.SCREW_X, p.SCREW_X):
        frame = frame.union(cyl(sx, p.SCREW_RIB_Y, p.RIB_Z_TOP, p.BOSS_D, p.RIB_GAP))
        frame = frame.cut(cyl(sx, p.SCREW_RIB_Y, zb - 1, p.SCREW_D, 10))
    return Part("midframe", frame, CAST, "frame",
                notes="walls + divider + recessed ribbed grid; board sits in front of the ribs")


def locating_pins():
    parts = []
    for i, sx in enumerate((-p.PIN_PITCH / 2, p.PIN_PITCH / 2)):
        parts.append(Part(f"pin_{'L' if i == 0 else 'R'}",
                          cyl(sx, 0.0, p.RIB_Z_TOP, p.PIN_D, p.RIB_GAP + p.PIN_H), PIN, "frame",
                          notes="datum pin"))
    return parts
