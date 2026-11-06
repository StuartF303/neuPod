"""Back shell (stainless tray) and front plate (bezel + glass)."""
import cadquery as cq
from .. import params as p
from .common import Part, box, cyl, rounded_box

STEEL = (0.72, 0.74, 0.76)
ALU = (0.85, 0.86, 0.88)
GLASS = (0.55, 0.75, 0.95)


def back_shell():
    """Tray: floor at Z_BACK_IN, walls up BACK_SHELL_SIDE_H."""
    x0, x1 = -p.SHELL_W / 2, p.SHELL_W / 2
    y0, y1 = p.INT_Y0 - p.BACK_SHELL_WALL, p.INT_Y1 + p.BACK_SHELL_WALL
    z0 = p.Z_BACK_OUT
    z1 = p.Z_BACK_IN + p.BACK_SHELL_SIDE_H
    outer = rounded_box(x0, y0, z0, x1, y1, z1, p.SHELL_CORNER_R)
    inner = rounded_box(x0 + p.BACK_SHELL_WALL, y0 + p.BACK_SHELL_WALL, p.Z_BACK_IN,
                        x1 - p.BACK_SHELL_WALL, y1 - p.BACK_SHELL_WALL, z1 + 1,
                        p.SHELL_CORNER_R - p.BACK_SHELL_WALL)
    shell = outer.cut(inner)
    # dock aperture in the bottom end wall
    zc = p.BOARD_T / 2
    w, h = p.DOCK_AP_W, p.DOCK_AP_H
    slot = (cq.Workplane("XZ").slot2D(w, h).extrude(-(p.BACK_SHELL_WALL + 2.0))
            .translate((0, y0 - 1.0, zc)))
    shell = shell.cut(slot)
    return Part("back_shell", shell, STEEL, "shell",
                notes="stainless tray; dock aperture in the bottom end wall")


def rf_window():
    """RF-transparent plastic window region on the inner face of the back shell (keep-out marker)."""
    w, l = p.RF_WINDOW
    s = box(p.RF_WINDOW_X0, p.INSERT_Y0, p.Z_BACK_IN, p.RF_WINDOW_X0 + w, p.INT_Y1, p.Z_RIM)
    return Part("rf_window", s, (0.2, 0.8, 0.3), "shell", notes="plastic top insert beside the jack = the RF-transparent window (scan)")


def front_plate():
    """Bezel + glass as one slab with the LCD window and the wheel hole."""
    x0, x1 = -p.SHELL_W / 2, p.SHELL_W / 2
    y0, y1 = p.INT_Y0 - p.BACK_SHELL_WALL, p.INT_Y1 + p.BACK_SHELL_WALL
    plate = rounded_box(x0, y0, p.Z_FRONT_IN, x1, y1, p.Z_FRONT_OUT, p.SHELL_CORNER_R)
    lw, lh = p.LCD_ACTIVE
    ly0 = p.LCD_Y0 + (p.LCD_MODULE[1] - lh) / 2
    plate = plate.cut(box(-lw / 2, ly0, p.Z_FRONT_IN - 1, lw / 2, ly0 + lh, p.Z_FRONT_OUT + 1))
    plate = plate.cut(cyl(p.WHEEL_CX, p.WHEEL_CY, p.Z_FRONT_IN - 1, p.WHEEL_D, p.FRONT_PLATE_T + 2))
    return Part("front_plate", plate, ALU, "front", notes="bezel + glass as one slab")
