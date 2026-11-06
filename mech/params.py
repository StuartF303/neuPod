"""neuPod mechanical model — every dimension in one place.

Units: mm.  Frame: origin at the midpoint of the two mid-frame locating pins,
X across the device (+X = right when viewed from the front), Y along the device
(+Y toward the LCD / headphone-jack end), Z toward the front glass.  Z = 0 is the
REAR (component) face of the donor logic board, i.e. the face that looks at the ribs.

Every value is registered with P(name, value, source, note).  `source` is one of
    measured   — caliper/ruler on the donor (Stuart, 5 Nov 2026 teardown)
    scan       — read off the Metro X scans (6 Nov 2026, mech/scans/), ±0.3 mm typical
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
    assert source in ("measured", "scan", "photo", "datasheet", "estimate", "derived"), source
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
BACK_SHELL_SIDE_H = P("BACK_SHELL_SIDE_H", 7.8, "scan",
                      "inner floor to rim; outer scan: back face to rim 8.3 minus the wall (floor did not scan — polished steel)")
FRONT_PLATE_T = P("FRONT_PLATE_T", 0.8, "estimate",
                  "aluminium bezel + glass treated as one slab; measure at the wheel hole")
HDD_CAVITY_T_STATED = P("HDD_CAVITY_T_STATED", 7.0, "measured",
                        "Stuart: 70x55x5 disk + 1 mm bumpers → 7 mm 'free cavity' (conflicts with envelope, see report)")

# --------------------------------------------------------------------------- #
# Datum: locating pins and the donor logic board
PIN_D = P("PIN_D", 1.5, "estimate", "locating-pin diameter")
PIN_PITCH = P("PIN_PITCH", 26.4, "photo", "pins stand where the lower vertical ribs meet the bottom wall, just outside each rib, either side of the dock (photo 16 scaled by the scanned strap width; too thin to scan). CALIPER")
PIN_H = P("PIN_H", 2.5, "estimate", "pin height above the rib plane (through the board)")

BOARD_W = P("BOARD_W", 55.0, "measured", "donor 820-2437, X (scan 55.4 incl. edge bleed)")
WHEEL_TO_TOP = P("WHEEL_TO_TOP", 17.2, "scan", "centre switch to the board's top main edge (pcb scan)")
BOARD_TAB = P("BOARD_TAB", (20.0, 1.9), "scan", "tab at the top-edge centre, X × Y (teardown finding 11)")
BOARD_T = P("BOARD_T", 1.0, "estimate", "donor board thickness; new board target 1.0–1.2")
BOARD_DOCK_NOTCH = P("BOARD_DOCK_NOTCH", (19.5, 2.0), "scan", "donor notch in the dock edge for the 30-pin connector, width × depth (pcb scan edge dips at ±9.5; depth estimate)")
BOARD_CORNER_R = P("BOARD_CORNER_R", 1.5, "estimate")
DOCK_AP_W = P("DOCK_AP_W", 21.0, "measured", "dock hole in the back shell, width X, ends fully rounded (Stuart; scan 21.2)")
DOCK_AP_H = P("DOCK_AP_H", 3.0, "measured", "dock hole height Z (scan reads 1.5 — edges eroded)")
DOCK_AP_CX = P("DOCK_AP_CX", 0.4, "scan", "dock hole centre X (back_shell_in end wall)")
DOCK_AP_ZC = P("DOCK_AP_ZC", -1.5, "scan", "dock hole centre Z in the datum: ~2 mm BEHIND the front board mid-plane. ±1 mm (shell floor did not scan) — CALIPER: hole centre to the back face")
SCREW_D = P("SCREW_D", 1.6, "estimate", "board-to-frame screw hole dia (M1.4?)")
SCREW_X = P("SCREW_X", 4.5, "measured", "board screws at ±X from the wheel centre on the middle (screw) rib = the two holes either side of the centre switch (Stuart, 6 Nov)")
TOP_SCREW_POS = P("TOP_SCREW_POS", (13.0, 40.0), "photo", "top screw holes at ±X, Y (photo 18, which is upside down): either side of the top tab")

# --------------------------------------------------------------------------- #
# Mid-frame casting
# Frame features in FRAME-CENTRED coordinates (suffix _F: origin at the casting's bbox centre, front view),
# read off frame-new1-btm (front face) and 1106_03frame_top (rear face).  FRAME_PIN_YF ties them to the datum.
FRAME_OUT = P("FRAME_OUT", (60.2, 101.2), "scan", "casting outline X × Y")
FRAME_PIN_YF = P("FRAME_PIN_YF", -47.2, "photo", "pin centres in frame coords: ~0.8 above the bottom-wall inner edge (photo 16)")
FRAME_WALL_T = P("FRAME_WALL_T", 2.0, "scan", "outer wall thickness (1.6–2.4 along the walls)")
FRAME_CORNER_R = P("FRAME_CORNER_R", 4.8, "scan", "outer plan-view corner radius (4.1–5.5 across the four corners, both scans)")
POCKET_CORNER_R = P("POCKET_CORNER_R", 0.8, "photo", "fillet in the pocket corners (photo 16)")
FRAME_WALL_FRONT_H = P("FRAME_WALL_FRONT_H", 4.0, "scan", "walls stand this far proud of the rib front face on the board side (3.6 lower half, 4.2 upper)")
FRAME_WALLS_TO_FLOOR = P("FRAME_WALLS_TO_FLOOR", False, "scan", "rear: walls are flush with the rib rear face (they do not reach the shell floor)")
RIB_W = P("RIB_W", 2.1, "scan", "vertical rib width in plan (2.0–2.2)")
SCREW_RIB_YF = P("SCREW_RIB_YF", -21.6, "scan", "horizontal (screw) rib centre, full width between the walls")
SCREW_RIB_W = P("SCREW_RIB_W", 2.6, "scan")
CROSS_RIB_YF = P("CROSS_RIB_YF", -35.5, "scan", "short rib across the lower centre column")
CROSS_RIB_W = P("CROSS_RIB_W", 2.2, "scan")
LOWER_RIB_X = P("LOWER_RIB_X", 12.4, "scan", "lower vertical ribs at ±X (dock strap → screw rib)")
UPPER_RIB_X = P("UPPER_RIB_X", (-16.0, 15.9), "scan", "upper vertical ribs (screw rib → divider): they step OUT from the lower ones")
DIVIDER_YF = P("DIVIDER_YF", (-4.1, -0.5), "scan", "bar between the grid and the LCD window")
DIVIDER_REAR_PROUD = P("DIVIDER_REAR_PROUD", 1.3, "scan", "divider stands proud of the rib rear face (toward the back shell); flush with the ribs on the front")
DOCK_STRAP_F = P("DOCK_STRAP_F", (-13.2, -48.9, 13.4, -45.3), "scan", "strap over the dock connector at wall-top height, x0 y0 x1 y1")
DOCK_STRAP_T = P("DOCK_STRAP_T", 1.0, "estimate", "strap thickness (Z)")
FRAME_BOTTOM_WALL_YF = P("FRAME_BOTTOM_WALL_YF", -48.0, "scan", "inner edge of the bottom wall")
LCD_WINDOW_YF = P("LCD_WINDOW_YF", (-0.5, 48.1), "scan", "LCD window in the casting (upper half)")
ADHESIVE_STRIP_T = P("ADHESIVE_STRIP_T", 0.3, "estimate", "LCD adhesive strip on the divider")
RIB_GAP = P("RIB_GAP", 0.5, "estimate", "board rear face to rib front face (component height under a rib). CALIPER: does the board sit on the ribs?")
RIB_T = P("RIB_T", 1.4, "estimate", "rib thickness Z; scan-derived if the dock strap is 1.0 thick (strap rear face sits 4.4 in front of the rib rear plane). CALIPER")
BOSS_D = P("BOSS_D", 3.0, "estimate", "screw boss diameter on the screw rib")
FRAME_DOCK_GAP_X = P("FRAME_DOCK_GAP_X", 15.1, "scan", "the bottom wall is open between ±X for the dock")
SCREWS_ARE_WHEEL_HOLES = P("SCREWS_ARE_WHEEL_HOLES", True, "measured", "confirmed: the holes beside the centre switch are the screw holes in the middle bar")

# --------------------------------------------------------------------------- #
# Button zone (wheel-facing side of the board) — must be reproduced exactly
SWITCH_BODY = P("SWITCH_BODY", 6.0, "measured", "6x6 SMD tactile switch body")
SWITCH_H = P("SWITCH_H", 0.8, "scan", "outer four switches, top above the board (scan 0.73–0.80; scanner smooths edges). CALIPER")
SWITCH_H_C = P("SWITCH_H_C", 1.2, "scan", "centre switch top incl. actuator nub")
SWITCH_RADIUS = P("SWITCH_RADIUS", 14.1, "scan", "N/S/E/W centres from the centre switch: 14.5 / 14.1 / 14.0 / 14.0 (N, S, W, E)")
WHEEL_CX = P("WHEEL_CX", 0.5, "scan", "centre switch X from the board centre line")
ADHESIVE_RING_D = P("ADHESIVE_RING_D", 37.0, "measured", "wheel adhesive ring on the board")
WHEEL_LOC_HOLE_D = P("WHEEL_LOC_HOLE_D", 1.2, "estimate", "wheel locating holes")
WHEEL_LOC_HOLE_X = P("WHEEL_LOC_HOLE_X", 4.5, "measured", "either side of the centre switch")
RING_TO_BOARD_GAP = P("RING_TO_BOARD_GAP", 0.3, "estimate", "adhesive ring thickness / wheel-flex-to-board gap")
FRONT_FACE_LOWPROFILE_H = P("FRONT_FACE_LOWPROFILE_H", 0.8, "estimate",
                            "design limit for parts on the wheel face outside the 37 mm ring (donor: 0402s, one QFN)")

# --------------------------------------------------------------------------- #
# Click wheel assembly 821-0545-A
WHEEL_D = P("WHEEL_D", 40.0, "measured", "capacitive ring + controller sub-assembly diameter")
WHEEL_FLEX_W = P("WHEEL_FLEX_W", 5.0, "photo", "13-pin 0.3 mm flex width (fingers ≈ 4 mm)")
WHEEL_FLEX_EXIT = P("WHEEL_FLEX_EXIT", "left", "measured", "flex tail leaves the wheel's left edge")
WHEEL_CONN_OFS = P("WHEEL_CONN_OFS", (-20.8, 2.7), "photo", "donor wheel connector centre X, and distance below the board's top edge; REAR face (photo 12 is a rear view). FIXED: reused flex")
WHEEL_CONN_SIZE = P("WHEEL_CONN_SIZE", (6.0, 3.5, 1.0), "datasheet", "13P 0.3 mm FPC connector, flip latch (HDGC 0.3K-HX-13PWB class)")

# --------------------------------------------------------------------------- #
# Display — stock LCD and the AMOLED replacement
LCD_ACTIVE = P("LCD_ACTIVE", (50.8, 38.1), "datasheet", "2.5in 4:3 stock TN, X × Y")
LCD_MODULE = P("LCD_MODULE", (55.0, 44.0, 2.3), "estimate", "stock LCD module outline X × Y × T, centred in the 48.6 mm frame window")
LCD_BACKPLATE_T = P("LCD_BACKPLATE_T", 0.3, "estimate", "steel backplate behind the LCD")
LCD_ZIF_POS = P("LCD_ZIF_POS", (0.0, 38.5), "photo", "donor wide LCD ZIF, rear face, top centre")
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
BATT_POCKET_POS = P("BATT_POCKET_POS", (-28.0, None), "scan", "pocket X0 (right-biased, beside the jack); Y set in derived block to the shell top")
JACK_BLOCK_F = P("JACK_BLOCK_F", (18.0, 37.0, 28.0, 50.0), "scan", "jack + hold module block in shell coords (front view: TOP-RIGHT), x0 y0 x1 y1")
JACK_BLOCK_DROP = P("JACK_BLOCK_DROP", 1.3, "scan", "block top sits this far below the shell rim")
JACK_BOSS_F = P("JACK_BOSS_F", (22.0, 32.0, 7.0), "scan", "round feature below the block (jack barrel / screw boss): x, y, dia")
JACK_BOSS_DROP = P("JACK_BOSS_DROP", 4.1, "scan", "its top below the rim")
PLASTIC_INSERT_YF = P("PLASTIC_INSERT_YF", 46.0, "scan", "inner edge of the plastic top insert (shell coords); it spans x -28.5…15 beside the jack")
JACK_CONN_OFS = P("JACK_CONN_OFS", (-21.9, 4.2), "photo", "donor jack/hold connector centre X, and height above the dock edge; REAR face. FIXED: reused flex")
JACK_CONN_SIZE = P("JACK_CONN_SIZE", (5.0, 3.0, 1.2), "estimate", "jack/hold FPC connector body")
JACK_FLEX_L = P("JACK_FLEX_L", 110.0, "estimate", "donor jack/hold flex length, jack to connector")
RF_WINDOW = P("RF_WINDOW", (43.5, 5.0), "scan", "plastic top insert = the RF-transparent window, X × Y on the inner face (plastic scans, steel does not)")
RF_WINDOW_X0 = P("RF_WINDOW_X0", -28.5, "scan", "insert left edge X (it runs up to the jack module)")

# --------------------------------------------------------------------------- #
# Candidate parts (datasheet sizes, modelled as boxes) X × Y × H
MCU_RP2350B = P("MCU_RP2350B", (10.0, 10.0, 0.9), "datasheet", "QFN-80")
MCU_ESP32S3 = P("MCU_ESP32S3", (18.0, 25.5, 3.1), "datasheet", "ESP32-S3-WROOM-1")
RADIO_RM2 = P("RADIO_RM2", (18.0, 14.0, 2.5), "datasheet", "Raspberry Pi RM2")
RADIO_BT1026D = P("RADIO_BT1026D", (27.0, 13.0, 2.2), "datasheet", "Feasycom FSC-BT1026D")
MICROSD = P("MICROSD", (15.0, 14.0, 1.85), "datasheet", "push-push socket")
USBC = P("USBC", (8.94, 7.35, 3.26), "datasheet", "USB-C 2.0 receptacle body X × Y(depth) × Z (16-pin class)")
USBC_TAB_W = P("USBC_TAB_W", 13.0, "estimate", "board tab carrying the USB-C out to the shell wall through the frame's dock gap")
USBC_MOUNT_RESIDUAL_OK = P("USBC_MOUNT_RESIDUAL_OK", 0.3, "estimate", "max acceptable centre error between receptacle and hole before a different mount is needed")
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
    "604040": (40.0, 40.0, 6.0, 1100),
    "704040": (40.0, 40.0, 7.0, 1300),
    "604045": (40.0, 45.0, 6.0, 1250),
    "704045": (40.0, 45.0, 7.0, 1450),
}, "datasheet", "LiPo X × Y × T × mAh; 505060/605060 from vendor pages, others typical catalogue values (capacities estimate); 40×40/40×45 added for the under-LCD bay")
HAPTIC_TAPTIC = P("HAPTIC_TAPTIC", (35.0, 11.0, 4.5), "datasheet", "iPhone 7/8 Taptic Engine — CHOSEN haptic (6 Nov); driven from a new connector on one of the boards")
HAPTIC_CONN = P("HAPTIC_CONN", (6.0, 4.0, 1.0), "estimate", "board connector for the Taptic Engine lead (2-wire drive)")
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
FRAME_CY = P("FRAME_CY", -FRAME_PIN_YF, "derived", "frame (and shell) centre Y in the datum")


def YF(y):
    """frame/shell-centred Y → datum Y"""
    return y + FRAME_CY


WHEEL_CY = P("WHEEL_CY", 0.0, "derived", "set below: on the screw rib")
BOARD_FRONT_Z = P("BOARD_FRONT_Z", BOARD_T, "derived", "wheel-facing face of the board")
RIB_Z_TOP = P("RIB_Z_TOP", -RIB_GAP, "derived", "rib FRONT face (toward the board)")
RIB_Z_BACK = P("RIB_Z_BACK", -RIB_GAP - RIB_T, "derived", "rib rear face = front of the HDD cavity in the lower half")
Z_FRONT_IN = P("Z_FRONT_IN", RIB_Z_TOP + FRAME_WALL_FRONT_H, "derived", "inner face of the front plate = frame wall top")
WHEEL_T = P("WHEEL_T", Z_FRONT_IN - BOARD_FRONT_Z - SWITCH_H, "derived", "room for the wheel sub-assembly between switch tops and the bezel")
Z_FRONT_OUT = P("Z_FRONT_OUT", Z_FRONT_IN + FRONT_PLATE_T, "derived", "front glass surface")
if DEPTH_MODEL == "stated":
    Z_BACK_IN = P("Z_BACK_IN", RIB_Z_BACK - HDD_CAVITY_T_STATED, "derived", "forced by the 7 mm stated cavity")
else:
    Z_BACK_IN = P("Z_BACK_IN", Z_FRONT_OUT - SHELL_T + BACK_SHELL_WALL, "derived", "inner floor of the back shell")
Z_BACK_OUT = P("Z_BACK_OUT", Z_BACK_IN - BACK_SHELL_WALL, "derived")
ENVELOPE_T_ACTUAL = P("ENVELOPE_T_ACTUAL", Z_FRONT_OUT - Z_BACK_OUT, "derived", "what the stack adds up to")
Z_RIM = P("Z_RIM", Z_BACK_IN + BACK_SHELL_SIDE_H, "derived", "back-shell rim")
INT_W = P("INT_W", SHELL_W - 2 * BACK_SHELL_WALL, "derived")
INT_L = P("INT_L", SHELL_L - 2 * BACK_SHELL_WALL, "derived")
INT_Y0 = P("INT_Y0", FRAME_CY - INT_L / 2, "derived", "shell inner bottom end (shell centred on the frame)")
INT_Y1 = P("INT_Y1", FRAME_CY + INT_L / 2, "derived", "shell inner top end")
FRAME_X = P("FRAME_X", FRAME_OUT[0] / 2, "derived")
FRAME_Y0 = P("FRAME_Y0", FRAME_CY - FRAME_OUT[1] / 2, "derived")
FRAME_Y1 = P("FRAME_Y1", FRAME_CY + FRAME_OUT[1] / 2, "derived")
FRAME_IN_W = P("FRAME_IN_W", FRAME_OUT[0] - 2 * FRAME_WALL_T, "derived")
FRAME_Z_BACK = P("FRAME_Z_BACK", Z_BACK_IN + 0.2 if FRAME_WALLS_TO_FLOOR else RIB_Z_BACK, "derived", "rear of the casting walls")
DIVIDER_Y0 = P("DIVIDER_Y0", YF(DIVIDER_YF[0]), "derived")
DIVIDER_Y1 = P("DIVIDER_Y1", YF(DIVIDER_YF[1]), "derived")
FRAME_DIVIDER_Z_BACK = P("FRAME_DIVIDER_Z_BACK", RIB_Z_BACK - DIVIDER_REAR_PROUD, "derived", "rear face of the divider")
SCREW_RIB_Y = P("SCREW_RIB_Y", YF(SCREW_RIB_YF), "derived")
PARAMS["WHEEL_CY"]["value"] = WHEEL_CY = SCREW_RIB_Y          # screws beside the centre switch go into this rib
BOARD_TOP_Y = P("BOARD_TOP_Y", WHEEL_CY + WHEEL_TO_TOP, "derived", "main top edge (the tab adds BOARD_TAB[1])")
CENTRE_SPLIT_Y = P("CENTRE_SPLIT_Y", YF(CROSS_RIB_YF), "derived", "cross rib in the lower centre column")
BOTTOM_WALL_Y = P("BOTTOM_WALL_Y", YF(FRAME_BOTTOM_WALL_YF), "derived")
BOARD_BOTTOM_Y = P("BOARD_BOTTOM_Y", BOTTOM_WALL_Y + 0.1, "derived", "dock edge sits against the frame's bottom wall")
BOARD_L = P("BOARD_L", BOARD_TOP_Y - BOARD_BOTTOM_Y, "derived", "main edges, Y (scan read 44.4 incl. edge bleed). CALIPER to confirm")
WHEEL_CONN_POS = P("WHEEL_CONN_POS", (WHEEL_CONN_OFS[0], BOARD_TOP_Y - WHEEL_CONN_OFS[1]), "derived")
JACK_CONN_POS = P("JACK_CONN_POS", (JACK_CONN_OFS[0], BOARD_BOTTOM_Y + JACK_CONN_OFS[1]), "derived")
WHEEL_CY_FROM_BOTTOM = P("WHEEL_CY_FROM_BOTTOM", WHEEL_CY - BOARD_BOTTOM_Y, "derived", "centre switch above the dock edge")
DOCK_STRAP = P("DOCK_STRAP", (DOCK_STRAP_F[0], YF(DOCK_STRAP_F[1]), DOCK_STRAP_F[2], YF(DOCK_STRAP_F[3])), "derived")
LCD_WIN_Y0 = P("LCD_WIN_Y0", YF(LCD_WINDOW_YF[0]), "derived")
LCD_WIN_Y1 = P("LCD_WIN_Y1", YF(LCD_WINDOW_YF[1]), "derived")
LCD_Y0 = P("LCD_Y0", (LCD_WIN_Y0 + LCD_WIN_Y1) / 2 - LCD_MODULE[1] / 2, "derived", "LCD module bottom edge (centred in the window)")
LCD_Z_BACK = P("LCD_Z_BACK", Z_FRONT_IN - LCD_MODULE[2] - LCD_BACKPLATE_T, "derived", "rear of the stock backplate = front of the under-LCD cavity")
PARAMS["AMOLED_PANEL_Z"]["value"] = Z_FRONT_IN - AMOLED_MODULE[2]
AMOLED_PANEL_Z = PARAMS["AMOLED_PANEL_Z"]["value"]
AMOLED_Z_BACK = P("AMOLED_Z_BACK", AMOLED_PANEL_Z - LCD_BACKPLATE_T, "derived", "AMOLED on the stock backplate")
HDD_Z_FRONT = P("HDD_Z_FRONT", RIB_Z_BACK, "derived")
CAVITY_T_BEHIND_RIBS = P("CAVITY_T_BEHIND_RIBS", RIB_Z_BACK - Z_BACK_IN, "derived", "lower half: rib back to shell floor")
CAVITY_T_UNDER_DIVIDER = P("CAVITY_T_UNDER_DIVIDER", FRAME_DIVIDER_Z_BACK - Z_BACK_IN, "derived", "what a cell crossing the divider gets")
CAVITY_T_UNDER_LCD = P("CAVITY_T_UNDER_LCD", LCD_Z_BACK - Z_BACK_IN, "derived", "upper half: backplate rear to shell floor")
POCKET_T_FULL = P("POCKET_T_FULL", 0 - Z_BACK_IN, "derived", "board rear face to shell floor through an open pocket")
SCREW_POS = P("SCREW_POS", [(WHEEL_CX - SCREW_X, WHEEL_CY), (WHEEL_CX + SCREW_X, WHEEL_CY)] if SCREWS_ARE_WHEEL_HOLES
               else [(-SCREW_X, SCREW_RIB_Y), (SCREW_X, SCREW_RIB_Y)], "derived", "board screws into the screw rib")
JACK_X0 = P("JACK_X0", JACK_BLOCK_F[0], "derived")
JACK_POS = P("JACK_POS", (JACK_BOSS_F[0] - JACK_BOSS_F[2] / 2 - 0.5, YF(JACK_BOSS_F[1] - JACK_BOSS_F[2] / 2)), "derived", "lowest-left corner of the jack module (boss included)")
INSERT_Y0 = P("INSERT_Y0", YF(PLASTIC_INSERT_YF), "derived", "inner edge of the plastic top insert")
BATT_POCKET_Y1 = P("BATT_POCKET_Y1", INSERT_Y0, "derived", "old cell pocket ends at the plastic insert")
BATT_POCKET_Y0 = P("BATT_POCKET_Y0", INSERT_Y0 - BATT_POCKET[1], "derived")
TAPTIC_POS = P("TAPTIC_POS", (JACK_X0 - 0.5 - HAPTIC_TAPTIC[0] / 2, INSERT_Y0 - 0.3 - HAPTIC_TAPTIC[1] / 2), "derived",
               "Taptic Engine centre: on the back-shell floor at the top end, long axis along X, right up against the jack/hold module (Stuart, 6 Nov)")

def table(sources=("estimate", "photo")):
    """Markdown table of parameters filtered by source."""
    rows = ["| name | value | source | note |", "|---|---|---|---|"]
    for k, v in PARAMS.items():
        if v["source"] in sources:
            rows.append(f"| {k} | {v['value']} | {v['source']} | {v['note']} |")
    return "\n".join(rows)


if __name__ == "__main__":
    print(table(("measured", "photo", "datasheet", "estimate", "derived")))
