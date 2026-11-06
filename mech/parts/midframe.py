"""Cast mid-frame, built from the Metro X scans (frame-new1-btm = front face,
1106_03frame_top = rear face).

Front face (board side): the outer walls and the dock strap stand FRAME_WALL_FRONT_H
proud of the rib front face, so the board, wheel and LCD sit in a shallow tray.
Rear face: the ribs are flush with the walls; the divider stands DIVIDER_REAR_PROUD
proud toward the back shell.  The vertical ribs step: ±LOWER_RIB_X below the screw
rib (ending at the dock strap, where the locating pins are), UPPER_RIB_X above it.
"""
from .. import params as p
from .common import Part, box, cyl, rounded_box

CAST = (0.62, 0.64, 0.66)
PIN = (0.9, 0.2, 0.2)


def _xr(x, w):
    return x - w / 2, x + w / 2


def pocket_grid():
    """name -> (x0, y0, x1, y1): pockets behind the board, clipped to the board outline."""
    bx = p.BOARD_W / 2
    hw = p.RIB_W / 2
    yl0 = max(p.BOTTOM_WALL_Y, p.BOARD_BOTTOM_Y)
    ys0, ys1 = p.SCREW_RIB_Y - p.SCREW_RIB_W / 2, p.SCREW_RIB_Y + p.SCREW_RIB_W / 2
    yc0, yc1 = p.CENTRE_SPLIT_Y - p.CROSS_RIB_W / 2, p.CENTRE_SPLIT_Y + p.CROSS_RIB_W / 2
    yt = min(p.DIVIDER_Y0, p.BOARD_TOP_Y)
    lx = p.LOWER_RIB_X
    ul, ur = p.UPPER_RIB_X
    return {
        "UL": (-bx, ys1, ul - hw, yt),
        "UC": (ul + hw, ys1, ur - hw, yt),
        "UR": (ur + hw, ys1, bx, yt),
        "LL": (-bx, yl0, -lx - hw, ys0),
        "LR": (lx + hw, yl0, bx, ys0),
        "LC_upper": (-lx + hw, yc1, lx - hw, ys0),
        "LC_lower": (-lx + hw, yl0, lx - hw, yc0),
    }


def midframe():
    xo, y0, y1 = p.FRAME_X, p.FRAME_Y0, p.FRAME_Y1
    wt = p.FRAME_WALL_T
    zr, zf_rib, zt = p.RIB_Z_BACK, p.RIB_Z_TOP, p.Z_FRONT_IN
    zw = p.FRAME_Z_BACK
    # outer walls with the scanned corner radius, open at the bottom between ±FRAME_DOCK_GAP_X
    R = p.FRAME_CORNER_R
    frame = rounded_box(-xo, y0, zw, xo, y1, zt, R).cut(
        rounded_box(-xo + wt, y0 + wt, zw - 1, xo - wt, y1 - wt, zt + 1, max(R - wt, 0.5)))
    frame = frame.cut(box(-p.FRAME_DOCK_GAP_X, y0 - 1, zw - 1, p.FRAME_DOCK_GAP_X, y0 + wt + 0.01, zt + 1))
    # bottom wall inner edge (scan) — fill between the outer wall and BOTTOM_WALL_Y outside the dock gap
    for sx in (-1, 1):
        x_a, x_b = sorted((sx * p.FRAME_DOCK_GAP_X, sx * (xo - wt)))
        frame = frame.union(box(x_a, y0 + wt, zw, x_b, p.BOTTOM_WALL_Y, zt).intersect(
            rounded_box(-xo, y0, zw - 1, xo, y1, zt + 1, R)))
    # dock strap at wall-top height over the dock connector
    dx0, dy0, dx1, dy1 = p.DOCK_STRAP
    frame = frame.union(box(dx0, dy0, zt - p.DOCK_STRAP_T, dx1, dy1, zt))
    # divider: from its rear face up to the rib front plane (flush with the ribs on the board side)
    frame = frame.union(box(-xo + wt, p.DIVIDER_Y0, p.FRAME_DIVIDER_Z_BACK, xo - wt, p.DIVIDER_Y1, zf_rib))
    # ribs
    xi = xo - wt
    ys0, ys1 = p.SCREW_RIB_Y - p.SCREW_RIB_W / 2, p.SCREW_RIB_Y + p.SCREW_RIB_W / 2
    ribs = box(-xi, ys0, zr, xi, ys1, zf_rib)                                       # screw rib
    for x in (-p.LOWER_RIB_X, p.LOWER_RIB_X):                                         # lower verticals
        a, b = _xr(x, p.RIB_W)
        ribs = ribs.union(box(a, p.BOTTOM_WALL_Y, zr, b, ys0, zf_rib))
    ribs = ribs.union(box(-p.LOWER_RIB_X, p.CENTRE_SPLIT_Y - p.CROSS_RIB_W / 2, zr,
                          p.LOWER_RIB_X, p.CENTRE_SPLIT_Y + p.CROSS_RIB_W / 2, zf_rib))  # cross rib
    for x in p.UPPER_RIB_X:                                                           # upper verticals
        a, b = _xr(x, p.RIB_W)
        ribs = ribs.union(box(a, ys1, zr, b, p.DIVIDER_Y0, zf_rib))
    # small fillets where the ribs meet (pocket corners), approximated by square-minus-circle blocks
    r = p.POCKET_CORNER_R
    if r > 0:
        def fil(cx, cy, sx, sy):
            sq = box(cx, cy, zr, cx + sx * r, cy + sy * r, zf_rib)
            return sq.cut(cyl(cx + sx * r, cy + sy * r, zr - 1, 2 * r, zf_rib - zr + 2))
        hw = p.RIB_W / 2
        for x in (-p.LOWER_RIB_X, p.LOWER_RIB_X):
            for side in (-1, 1):
                ribs = ribs.union(fil(x + side * hw, ys0, side, -1))
        for x in p.UPPER_RIB_X:
            for side in (-1, 1):
                ribs = ribs.union(fil(x + side * hw, ys1, side, 1))
    frame = frame.union(ribs)
    # screw bosses (rib front → board rear face) and holes
    for sx, sy in p.SCREW_POS:
        frame = frame.union(cyl(sx, sy, zf_rib, p.BOSS_D, p.RIB_GAP))
        frame = frame.cut(cyl(sx, sy, zr - 1, p.SCREW_D, 10))
    return Part("midframe", frame, CAST, "frame",
                notes="from the scans: stepped ribs, dock strap, divider proud at the rear")


def locating_pins():
    parts = []
    for i, sx in enumerate((-p.PIN_PITCH / 2, p.PIN_PITCH / 2)):
        parts.append(Part(f"pin_{'L' if i == 0 else 'R'}",
                          cyl(sx, 0.0, p.RIB_Z_TOP, p.PIN_D, p.RIB_GAP + p.PIN_H), PIN, "frame",
                          notes="datum pin"))
    return parts
