"""Removed stock items kept as reference bodies (HDD, donor battery) and the
free-volume regions used by the space study."""
from .. import params as p
from .common import Part, box, box_at, cyl
from .midframe import pocket_grid

GHOST = (0.5, 0.5, 0.9)
REGION = (0.3, 0.9, 0.9)


def hdd():
    w, l, t = p.HDD_SIZE
    z1 = p.HDD_Z_FRONT - p.HDD_BUMPER_T
    s = box_at(0.0, p.HDD_Y0 + l / 2, z1 - t, (w, l), t)
    return Part("hdd_removed", s, GHOST, "cavity", notes="1.8in disk, removed; shown for reference")


def donor_battery():
    w, l, t = p.BATT_POCKET
    x0 = p.BATT_POCKET_POS[0]
    s = box(x0, p.BATT_POCKET_Y0, p.Z_BACK_IN, x0 + w, p.BATT_POCKET_Y1, p.Z_BACK_IN + t)
    return Part("battery_donor", s, GHOST, "cavity", notes="donor cell stuck in the shell top; removed")


def regions():
    """Nominal free-volume regions (before subtracting fixed parts). name -> Part."""
    out = {}
    zb = p.Z_BACK_IN
    # six pockets, from the board rear face down to the shell floor
    for nm, (x0, y0, x1, y1) in pocket_grid().items():
        out[f"pocket_{nm}"] = Part(f"pocket_{nm}", box(x0, y0, zb, x1, y1, 0.0), REGION, "region",
                                   notes="between ribs, board rear face to shell floor")
    # HDD cavity: behind ribs (lower half) and behind the backplate (upper half) over the HDD footprint
    w, l, _ = p.HDD_SIZE
    out["hdd_cavity"] = Part("hdd_cavity", box(-w / 2, p.HDD_Y0, zb, w / 2, p.HDD_Y0 + l, 0.0), REGION, "region",
                             notes="HDD footprint, shell floor to board plane (ribs subtracted)")
    # under-LCD: whole upper half behind the backplate
    out["under_lcd"] = Part("under_lcd", box(-p.INT_W / 2, p.DIVIDER_Y1, zb, p.INT_W / 2, p.INT_Y1, p.LCD_Z_BACK),
                            REGION, "region", notes="divider to shell top, behind the backplate")
    # freed donor battery pocket
    bw, bl, bt = p.BATT_POCKET
    x0 = p.BATT_POCKET_POS[0]
    out["battery_pocket"] = Part("battery_pocket", box(x0, p.BATT_POCKET_Y0, zb, x0 + bw, p.BATT_POCKET_Y1, zb + bt),
                                 REGION, "region", notes="donor battery pocket, freed")
    # front face of the board: inside the ring (switch height) and outside (to the bezel)
    cx, cy = p.WHEEL_CX, p.WHEEL_CY
    out["front_ring"] = Part("front_ring", cyl(cx, cy, p.BOARD_FRONT_Z, p.ADHESIVE_RING_D, p.SWITCH_H), REGION, "region",
                             notes="inside the 37 mm ring: nothing taller than the switches")
    outside = (box(-p.BOARD_W / 2, p.BOARD_BOTTOM_Y, p.BOARD_FRONT_Z, p.BOARD_W / 2, p.BOARD_TOP_Y, p.Z_FRONT_IN)
               .cut(cyl(cx, cy, p.BOARD_FRONT_Z - 1, p.WHEEL_D, 10)))
    out["front_outside_wheel"] = Part("front_outside_wheel", outside, REGION, "region",
                                      notes="wheel face outside the 40 mm wheel, up to the bezel")
    return out
