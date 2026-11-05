"""neuPod mechanical model — every dimension in one place.

Units: mm.  Frame: origin at the midpoint of the two mid-frame locating pins,
X across the device (+X = right when viewed from the front), Y along the device
(+Y toward the LCD / headphone-jack end), Z toward the front glass.  Z = 0 is the
REAR (component) face of the donor logic board, i.e. the face that looks at the ribs.

Every value is registered with P(name, value, source, note).  `source` is one of
    measured   — caliper/ruler on the donor (Stuart, 5 Nov 2026 teardown)
    photo      — scaled off a teardown photo against the RasPiO ruler (±1 mm)
    datasheet  — vendor datasheet / everymac / iFixit
    estimate   — a guess; the scan must replace it
    derived    — computed from other parameters (do not edit; edit the inputs)

When a Metro X scan of a part lands in mech/scans/, align it to the datum
(scan_ref.py) and replace the `estimate`/`photo` values it covers.
Licence: CERN-OHL-S v2 (see /LICENSE-CERN-OHL-S-2.0).
"""
import os
from collections import OrderedDict

PARAMS: "OrderedDict[str, dict]" = OrderedDict()


def P(name, value, source, note=""):
    assert source in ("measured", "photo", "datasheet", "estimate", "derived"), source
    PARAMS[name] = dict(value=value, source=source, note=note)
    return value


# --------------------------------------------------------------------------- #
# Depth model switch.  "derived": back-shell inner face comes from the 10.5 mm
# envelope minus the front stack (consistent with the thin shell).  "stated":
# forces the 7 mm HDD cavity Stuart quoted, which implies a ~12.5 mm envelope —
# used only to show what the layouts look like if the front stack turns out
# much thinner than estimated.  The space study runs both.
DEPTH_MODEL = P("DEPTH_MODEL", os.environ.get("NEUPOD_DEPTH_MODEL", "derived"), "estimate",
                "'derived' (envelope-consistent) or 'stated' (7 mm cavity forced)")

# --------------------------------------------------------------------------- #
# Shell envelope (A1238 thin, 2008)
SHELL_L = P("SHELL_L", 103.5, "datasheet", "everymac, length (Y)")
SHELL_W = P("SHELL_W", 61.8, "datasheet", "everymac, width (X)")
SHELL_T = P("SHELL_T", 10.5, "datasheet", "everymac, thin; thick variant 13.5")
SHELL_CORNER_R = P("SHELL_CORNER_R", 7.0, "estimate", "plan-view corner radius")
BACK_SHELL_WALL = P("BACK_SHELL_WALL", 0.5, "estimate", "stainless back shell wall")
BACK_SHELL_SIDE_H = P("BACK_SHELL_SIDE_H", 8.5, "estimate",
                      "height of the back-shell side wall from its inner floor (where it meets the front bezel)")
FRONT_PLATE_T = P("FRONT_PLATE_T", 0.8, "estimate",
                  "aluminium bezel + glass treated as one slab; measure at the wheel hole")
HDD_CAVITY_T_STATED = P("HDD_CAVITY_T_STATED", 7.0, "measured",
                        "Stuart: 70x55x5 disk + 1 mm bumpers → 7 mm 'free cavity' (conflicts with envelope, see report)")

# --------------------------------------------------------------------------- #
# Datum: locating pins and the donor logic board
PIN_D = P("PIN_D", 1.5, "estimate", "locating-pin diameter")
PIN_PITCH = P("PIN_PITCH", 28.5, "photo", "pin-to-pin centre distance: pins sit at the bottom ends of the two vertical ribs, flanking the dock bar (photo 16 ≈ 29–31, photo 18 holes ≈ 26); CALIPER THIS")
PIN_H = P("PIN_H", 2.5, "estimate", "pin height above the rib plane (through the board)")

BOARD_W = P("BOARD_W", 55.0, "measured", "donor 820-2437, X")
BOARD_L = P("BOARD_L", 55.0, "measured", "donor 820-2437, Y")
BOARD_T = P("BOARD_T", 1.0, "estimate", "donor board thickness; new board target 1.0–1.2")
BOARD_BOTTOM_Y = P("BOARD_BOTTOM_Y", -3.0, "photo", "pin centre to board bottom edge (photo 18)")
BOARD_CORNER_R = P("BOARD_CORNER_R", 1.5, "estimate")
DOCK_AP_W = P("DOCK_AP_W", 21.0, "measured", "30-pin dock aperture width (X), bottom edge")
DOCK_AP_H = P("DOCK_AP_H", 3.0, "measured", "30-pin dock aperture height (Z)")
SCREW_D = P("SCREW_D", 1.6, "estimate", "board-to-frame screw hole dia (M1.4?)")
SCREW_X = P("SCREW_X", 5.0, "photo", "screw holes at ±SCREW_X on the screw rib (photo 16)")
TOP_SCREW_INSET = P("TOP_SCREW_INSET", 3.0, "estimate", "top-left/top-right screw holes inset from board edges")

# --------------------------------------------------------------------------- #
# Mid-frame casting
FRAME_WALL_T = P("FRAME_WALL_T", 2.0, "estimate", "outer wall thickness of the casting")
FRAME_BOTTOM_BAR = P("FRAME_BOTTOM_BAR", 4.0, "estimate", "casting bar below the board bottom edge (carries the dock cut-out)")
FRAME_DIVIDER_T = P("FRAME_DIVIDER_T", 1.5, "estimate", "bar between the board grid and the LCD window (Y)")
FRAME_DIVIDER_Z_BACK = P("FRAME_DIVIDER_Z_BACK", -1.6, "estimate",
                         "rear face of the divider bar (assumed level with the rib back; a flex can pass behind it)")
FRAME_WALLS_TO_FLOOR = P("FRAME_WALLS_TO_FLOOR", True, "estimate", "outer walls of the casting reach the back-shell floor (photo 16 suggests full-depth walls)")
ADHESIVE_STRIP_T = P("ADHESIVE_STRIP_T", 0.3, "estimate", "LCD adhesive strip on the divider")
RIB_GAP = P("RIB_GAP", 0.8, "estimate", "board rear face to rib top (component clearance under a rib); Stuart: ≈1 by eye")
RIB_T = P("RIB_T", 0.8, "estimate", "rib thickness (Z)")
RIB_W = P("RIB_W", 1.2, "photo", "rib width in plan (photo 10)")
LEFT_COL_W = P("LEFT_COL_W", 12.8, "photo", "left pocket column width; Stuart's note says 10–11")
CENTRE_COL_W = P("CENTRE_COL_W", 27.0, "photo", "centre pocket column width; Stuart's note says ~23")
SCREW_RIB_FRAC = P("SCREW_RIB_FRAC", 0.59, "photo", "screw-rib centre as fraction of board length from the bottom (photo 10: 0.59; teardown text: ~1/3)")
CENTRE_SPLIT_FRAC = P("CENTRE_SPLIT_FRAC", 0.30, "photo", "second rib across the centre column, fraction from bottom")
BOSS_D = P("BOSS_D", 3.0, "estimate", "screw boss diameter (rib top to board rear face)")
WHEEL_WINDOW_D = P("WHEEL_WINDOW_D", 40.0, "estimate", "round window in the frame front over the wheel area")

# --------------------------------------------------------------------------- #
# Button zone (wheel-facing side of the board) — must be reproduced exactly
SWITCH_BODY = P("SWITCH_BODY", 6.0, "measured", "6x6 SMD tactile switch body")
SWITCH_H = P("SWITCH_H", 1.5, "estimate", "switch body height incl. actuator, to measure (1.5–2.5)")
SWITCH_RADIUS = P("SWITCH_RADIUS", 13.5, "photo", "N/S/E/W switch centres from the centre switch")
WHEEL_CX = P("WHEEL_CX", 0.0, "photo", "wheel/switch pattern centre X (photo 18 ≈ board centre)")
WHEEL_CY_FROM_BOTTOM = P("WHEEL_CY_FROM_BOTTOM", 24.5, "photo", "centre switch above board bottom edge (photo 18: 0.44 of 55)")
ADHESIVE_RING_D = P("ADHESIVE_RING_D", 37.0, "measured", "wheel adhesive ring on the board")
WHEEL_LOC_HOLE_D = P("WHEEL_LOC_HOLE_D", 1.2, "estimate", "wheel locating holes")
WHEEL_LOC_HOLE_X = P("WHEEL_LOC_HOLE_X", 4.5, "measured", "either side of the centre switch")
RING_TO_BOARD_GAP = P("RING_TO_BOARD_GAP", 0.3, "estimate", "adhesive ring thickness / wheel-flex-to-board gap")
FRONT_FACE_LOWPROFILE_H = P("FRONT_FACE_LOWPROFILE_H", 0.8, "estimate",
                            "design limit for parts on the wheel face outside the 37 mm ring (donor: 0402s, one QFN)")

# --------------------------------------------------------------------------- #
# Click wheel assembly 821-0545-A
WHEEL_D = P("WHEEL_D", 40.0, "measured", "capacitive ring + controller sub-assembly diameter")
WHEEL_T = P("WHEEL_T", 0.7, "estimate", "sub-assembly thickness between switch tops and the bezel")
WHEEL_FLEX_W = P("WHEEL_FLEX_W", 5.0, "photo", "13-pin 0.3 mm flex width (fingers ≈ 4 mm)")
WHEEL_FLEX_EXIT = P("WHEEL_FLEX_EXIT", "left", "measured", "flex tail leaves the wheel's left edge")
WHEEL_CONN_POS = P("WHEEL_CONN_POS", (-21.0, 48.7), "photo", "donor wheel connector centre, REAR face; photo 12 top-right seen from the rear = top-left in this front-view frame. FIXED: reused flex")
WHEEL_CONN_SIZE = P("WHEEL_CONN_SIZE", (6.0, 3.5, 1.0), "datasheet", "13P 0.3 mm FPC connector, flip latch (HDGC 0.3K-HX-13PWB class)")

# --------------------------------------------------------------------------- #
# Display — stock LCD and the AMOLED replacement
LCD_ACTIVE = P("LCD_ACTIVE", (50.8, 38.1), "datasheet", "2.5in 4:3 stock TN, X × Y")
LCD_MODULE = P("LCD_MODULE", (55.0, 40.5, 2.3), "estimate", "stock LCD module outline X × Y × T (must fit the 42 mm upper bay)")
LCD_BACKPLATE_T = P("LCD_BACKPLATE_T", 0.3, "estimate", "steel backplate behind the LCD")
LCD_Y0_OFFSET = P("LCD_Y0_OFFSET", 0.5, "estimate", "gap between the divider and the LCD module bottom edge")
LCD_ZIF_POS = P("LCD_ZIF_POS", (0.0, 50.0), "photo", "donor wide LCD ZIF, board top centre")
AMOLED_ACTIVE = P("AMOLED_ACTIVE", (48.96, 36.72), "datasheet", "2.41in RM690B0, mounted landscape X × Y")
AMOLED_MODULE = P("AMOLED_MODULE", (52.5, 41.5, 1.2), "estimate", "module outline X × Y × T (datasheet drawing to replace)")
AMOLED_FPC_W = P("AMOLED_FPC_W", 17.0, "datasheet", "30-pin 0.5 mm FPC width")
AMOLED_FPC_EXIT = P("AMOLED_FPC_EXIT", "bottom", "estimate", "FPC leaves the panel's bottom (divider) edge")
AMOLED_PANEL_Z = P("AMOLED_PANEL_Z", None, "derived", "panel rear face Z, set in derived block")

# --------------------------------------------------------------------------- #
# HDD (removed) and the cavity it leaves
HDD_SIZE = P("HDD_SIZE", (54.0, 71.0, 5.0), "datasheet", "1.8in Toshiba MK1231GAL X × Y × T")
HDD_BUMPER_T = P("HDD_BUMPER_T", 1.0, "measured", "rubber/foam bumpers each side")
HDD_Y0 = P("HDD_Y0", 1.0, "photo", "HDD bottom edge above the pins (photo 03: ≈ 8 mm above the shell inner bottom)")

# --------------------------------------------------------------------------- #
# Back-shell parts: battery pocket, jack/hold flex, RF window
BATT_POCKET = P("BATT_POCKET", (45.0, 20.0, 5.0), "measured", "donor cell pocket X × Y × T (width to re-check)")
BATT_POCKET_POS = P("BATT_POCKET_POS", (-17.0, None), "photo", "pocket X0 (right-biased, beside the jack); Y set in derived block to the shell top")
JACK_BODY = P("JACK_BODY", (14.0, 9.0, 5.0), "estimate", "headphone jack + hold switch module X × Y × T, top-left corner")
JACK_CONN_POS = P("JACK_CONN_POS", (-22.0, 2.5), "photo", "donor jack/hold flex connector centre, REAR face, beside the dock; photo 12 bottom-right seen from the rear = bottom-left here. FIXED: reused flex")
JACK_CONN_SIZE = P("JACK_CONN_SIZE", (5.0, 3.0, 1.2), "estimate", "jack/hold FPC connector body")
JACK_FLEX_L = P("JACK_FLEX_L", 110.0, "estimate", "donor jack/hold flex length, jack to connector")
RF_WINDOW = P("RF_WINDOW", (20.0, 10.0), "estimate", "RF-transparent plastic window at the top-left of the back shell, X × Y footprint on the inner face")
RF_WINDOW_X0 = P("RF_WINDOW_X0", -30.0, "estimate", "RF window left edge X")

# --------------------------------------------------------------------------- #
# Candidate parts (datasheet sizes, modelled as boxes) X × Y × H
MCU_RP2350B = P("MCU_RP2350B", (10.0, 10.0, 0.9), "datasheet", "QFN-80")
MCU_ESP32S3 = P("MCU_ESP32S3", (18.0, 25.5, 3.1), "datasheet", "ESP32-S3-WROOM-1")
RADIO_RM2 = P("RADIO_RM2", (18.0, 14.0, 2.5), "datasheet", "Raspberry Pi RM2")
RADIO_BT1026D = P("RADIO_BT1026D", (27.0, 13.0, 2.2), "datasheet", "Feasycom FSC-BT1026D")
MICROSD = P("MICROSD", (15.0, 14.0, 1.85), "datasheet", "push-push socket")
USBC = P("USBC", (9.0, 7.5, 3.2), "datasheet", "mid-mount receptacle, total height straddling the board")
QFN_SMALL = P("QFN_SMALL", (4.0, 4.0, 0.9), "datasheet", "codec / charger / gauge / haptic driver class")
INDUCTOR = P("INDUCTOR", (4.0, 4.0, 2.0), "datasheet", "4.7 uH")
PSRAM_FLASH = P("PSRAM_FLASH", (6.0, 5.0, 1.0), "datasheet", "SOIC-8 / WSON-8 class, two of them")
BATT_FOAM = P("BATT_FOAM", 0.5, "estimate", "foam each side of a LiPo")
CELLS = P("CELLS", {
    "505060": (50.0, 60.0, 5.0, 2500),
    "605060": (50.0, 60.0, 6.0, 3000),
    "405060": (50.0, 60.0, 4.0, 2000),
    "305060": (50.0, 60.0, 3.0, 1400),
    "503060": (30.0, 60.0, 5.0, 1000),
    "603060": (30.0, 60.0, 6.0, 1200),
    "503040": (30.0, 40.0, 5.0, 650),
    "403030": (30.0, 30.0, 4.0, 400),
    "603048": (30.0, 48.0, 6.0, 1000),
    "703048": (30.0, 48.0, 7.0, 1150),
    "603450": (34.0, 50.0, 6.0, 1200),
    "502040": (20.0, 40.0, 5.0, 400),
}, "datasheet", "LiPo X × Y × T × mAh; 505060/605060 from vendor pages, others typical catalogue values (estimate)")
HAPTIC_TAPTIC = P("HAPTIC_TAPTIC", (35.0, 11.0, 4.5), "datasheet", "iPhone 7/8 Taptic Engine")
HAPTIC_LRA = P("HAPTIC_LRA", (10.0, 10.0, 4.0), "datasheet", "rectangular LRA")
HAPTIC_PIEZO = P("HAPTIC_PIEZO", (12.0, 12.0, 0.6), "datasheet", "piezo disc Ø12")
B2B_CONN = P("B2B_CONN", (10.0, 4.0), "datasheet", "board-to-board mezzanine pair footprint; stack height is derived per layout")
FLEX_LINK_W = P("FLEX_LINK_W", 8.0, "estimate", "inter-board flex width")
SWITCH_BOARD_SHAPE = P("SWITCH_BOARD_SHAPE", "full", "measured", "front board in the stock position: 'full' = full donor outline carrying the switches (Stuart, 5 Nov); 'square'/'round' kept for comparison")
SWITCH_BOARD_D = P("SWITCH_BOARD_D", 42.0, "estimate", "round switch board diameter (40–45)")
SWITCH_BOARD_SQ = P("SWITCH_BOARD_SQ", (50.0, 45.0), "estimate", "square switch board X × Y: wide enough for the pin holes, corners carry the wheel connector and the link")
CLEARANCE = P("CLEARANCE", 0.3, "estimate", "design clearance to any fixed surface")

# --------------------------------------------------------------------------- #
# Derived values — edit the inputs above, not these
BOARD_TOP_Y = P("BOARD_TOP_Y", BOARD_BOTTOM_Y + BOARD_L, "derived")
WHEEL_CY = P("WHEEL_CY", BOARD_BOTTOM_Y + WHEEL_CY_FROM_BOTTOM, "derived")
BOARD_FRONT_Z = P("BOARD_FRONT_Z", BOARD_T, "derived", "wheel-facing face of the donor board")
Z_FRONT_IN = P("Z_FRONT_IN", BOARD_T + SWITCH_H + WHEEL_T, "derived", "inner face of the front plate")
Z_FRONT_OUT = P("Z_FRONT_OUT", Z_FRONT_IN + FRONT_PLATE_T, "derived", "front glass surface")
RIB_Z_TOP = P("RIB_Z_TOP", -RIB_GAP, "derived")
RIB_Z_BACK = P("RIB_Z_BACK", -RIB_GAP - RIB_T, "derived", "rear face of the ribs = front of the HDD cavity in the lower half")
if DEPTH_MODEL == "stated":
    Z_BACK_IN = P("Z_BACK_IN", RIB_Z_BACK - HDD_CAVITY_T_STATED, "derived", "forced by the 7 mm stated cavity")
else:
    Z_BACK_IN = P("Z_BACK_IN", Z_FRONT_OUT - SHELL_T + BACK_SHELL_WALL, "derived", "inner floor of the back shell")
Z_BACK_OUT = P("Z_BACK_OUT", Z_BACK_IN - BACK_SHELL_WALL, "derived")
ENVELOPE_T_ACTUAL = P("ENVELOPE_T_ACTUAL", Z_FRONT_OUT - Z_BACK_OUT, "derived", "what the stack adds up to")
INT_W = P("INT_W", SHELL_W - 2 * BACK_SHELL_WALL, "derived")
INT_L = P("INT_L", SHELL_L - 2 * BACK_SHELL_WALL, "derived")
INT_Y0 = P("INT_Y0", BOARD_BOTTOM_Y - FRAME_BOTTOM_BAR, "derived", "shell inner bottom end")
INT_Y1 = P("INT_Y1", INT_Y0 + INT_L, "derived", "shell inner top end")
FRAME_IN_W = P("FRAME_IN_W", INT_W - 2 * FRAME_WALL_T, "derived")
FRAME_Z_BACK = P("FRAME_Z_BACK", Z_BACK_IN + 0.2 if FRAME_WALLS_TO_FLOOR else RIB_Z_BACK, "derived", "rear of the casting walls")
DIVIDER_Y0 = P("DIVIDER_Y0", BOARD_TOP_Y, "derived")
DIVIDER_Y1 = P("DIVIDER_Y1", BOARD_TOP_Y + FRAME_DIVIDER_T, "derived")
SCREW_RIB_Y = P("SCREW_RIB_Y", BOARD_BOTTOM_Y + SCREW_RIB_FRAC * BOARD_L, "derived")
CENTRE_SPLIT_Y = P("CENTRE_SPLIT_Y", BOARD_BOTTOM_Y + CENTRE_SPLIT_FRAC * BOARD_L, "derived")
RIGHT_COL_W = P("RIGHT_COL_W", BOARD_W - LEFT_COL_W - CENTRE_COL_W - 2 * RIB_W, "derived")
VRIB_L_X = P("VRIB_L_X", -BOARD_W / 2 + LEFT_COL_W + RIB_W / 2, "derived", "left vertical rib centre")
VRIB_R_X = P("VRIB_R_X", VRIB_L_X + RIB_W + CENTRE_COL_W, "derived", "right vertical rib centre")
LCD_Y0 = P("LCD_Y0", DIVIDER_Y1 + LCD_Y0_OFFSET, "derived")
LCD_Z_BACK = P("LCD_Z_BACK", Z_FRONT_IN - LCD_MODULE[2] - LCD_BACKPLATE_T, "derived", "rear of the stock backplate = front of the under-LCD cavity")
PARAMS["AMOLED_PANEL_Z"]["value"] = Z_FRONT_IN - AMOLED_MODULE[2]
AMOLED_PANEL_Z = PARAMS["AMOLED_PANEL_Z"]["value"]
AMOLED_Z_BACK = P("AMOLED_Z_BACK", AMOLED_PANEL_Z - LCD_BACKPLATE_T, "derived", "AMOLED on the stock backplate")
HDD_Z_FRONT = P("HDD_Z_FRONT", RIB_Z_BACK, "derived")
CAVITY_T_BEHIND_RIBS = P("CAVITY_T_BEHIND_RIBS", RIB_Z_BACK - Z_BACK_IN, "derived", "lower half: rib back to shell floor")
CAVITY_T_UNDER_LCD = P("CAVITY_T_UNDER_LCD", LCD_Z_BACK - Z_BACK_IN, "derived", "upper half: backplate rear to shell floor")
POCKET_T_FULL = P("POCKET_T_FULL", 0 - Z_BACK_IN, "derived", "board rear face to shell floor through an open pocket")
BATT_POCKET_Y1 = P("BATT_POCKET_Y1", INT_Y1, "derived")
BATT_POCKET_Y0 = P("BATT_POCKET_Y0", INT_Y1 - BATT_POCKET[1], "derived")
JACK_POS = P("JACK_POS", (-INT_W / 2, INT_Y1 - JACK_BODY[1]), "derived", "jack module corner X0, Y0")


def table(sources=("estimate", "photo")):
    """Markdown table of parameters filtered by source."""
    rows = ["| name | value | source | note |", "|---|---|---|---|"]
    for k, v in PARAMS.items():
        if v["source"] in sources:
            rows.append(f"| {k} | {v['value']} | {v['source']} | {v['note']} |")
    return "\n".join(rows)


if __name__ == "__main__":
    print(table(("measured", "photo", "datasheet", "estimate", "derived")))
