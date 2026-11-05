"""Click wheel assembly, stock LCD + backplate, AMOLED replacement, headphone jack flex."""
from .. import params as p
from .common import Part, box, box_at, cyl

WHEEL = (0.25, 0.25, 0.28)
LCD = (0.1, 0.1, 0.12)
PLATE = (0.6, 0.6, 0.62)
FLEX = (0.85, 0.55, 0.1)


def click_wheel():
    z0 = p.BOARD_FRONT_Z + p.SWITCH_H
    disc = cyl(p.WHEEL_CX, p.WHEEL_CY, z0, p.WHEEL_D, p.WHEEL_T)
    # flex tail leaving the left edge toward the wheel connector
    tail = box(p.WHEEL_CX - p.WHEEL_D / 2 - 2, p.WHEEL_CY - p.WHEEL_FLEX_W / 2, z0,
               p.WHEEL_CX - p.WHEEL_D / 2 + 3, p.WHEEL_CY + p.WHEEL_FLEX_W / 2, z0 + 0.15)
    return [Part("click_wheel", disc, WHEEL, "front", notes="821-0545-A capacitive ring + PSoC on flex"),
            Part("wheel_flex_tail", tail, FLEX, "front", notes="13P 0.3 mm flex, exits left")]


def stock_lcd():
    w, l, t = p.LCD_MODULE
    z1 = p.Z_FRONT_IN
    panel = box_at(0.0, p.LCD_Y0 + l / 2, z1 - t, (w, l), t)
    plate = box_at(0.0, p.LCD_Y0 + l / 2, z1 - t - p.LCD_BACKPLATE_T, (w + 1.0, l + 1.0), p.LCD_BACKPLATE_T)
    return [Part("lcd_stock", panel, LCD, "front", notes="2.5in TN module"),
            Part("lcd_backplate", plate, PLATE, "front", notes="steel backplate")]


def amoled():
    w, l, t = p.AMOLED_MODULE
    z1 = p.Z_FRONT_IN
    cy = p.LCD_Y0 + p.LCD_MODULE[1] / 2
    panel = box_at(0.0, cy, z1 - t, (w, l), t)
    plate = box_at(0.0, cy, p.AMOLED_Z_BACK, (p.LCD_MODULE[0] + 1.0, p.LCD_MODULE[1] + 1.0), p.LCD_BACKPLATE_T)
    # FPC leaving the bottom edge, folding down behind the divider
    fpc = box(-p.AMOLED_FPC_W / 2, p.DIVIDER_Y0 - 1.0, p.AMOLED_Z_BACK - 0.15, p.AMOLED_FPC_W / 2, cy - l / 2 + 1, p.AMOLED_Z_BACK)
    return [Part("amoled", panel, LCD, "front", notes="2.41in RM690B0, landscape"),
            Part("lcd_backplate", plate, PLATE, "front", notes="stock backplate reused under the AMOLED"),
            Part("amoled_fpc_stub", fpc, FLEX, "front", notes="30P 0.5 mm FPC exit (bottom edge)")]


def jack_flex():
    w, l, t = p.JACK_BODY
    x0, y0 = p.JACK_POS
    body = box(x0, y0, p.Z_BACK_IN, x0 + w, y0 + l, p.Z_BACK_IN + t)
    return Part("jack_hold_module", body, (0.3, 0.3, 0.3), "shell",
                notes="3.5 mm jack + hold switch on one flex, screwed to the shell top-left")
