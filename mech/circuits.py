"""Circuit blocks, the interfaces between them, and which board each block goes on.

Edit SPLIT to move circuits between boards; everything downstream (placement, board
fill, link pin count, link connector size, the viewer and space_report.md) follows.

Board roles are layout-independent:
    front  — the board in the stock position (donor outline, carries the switches)
    second — the extra board in layouts B, D1, D2 (cavity board / main board)
Single-board layouts (A, C) put every block on "front".

Footprints are the block's area INCLUDING its local passives and courtyard, as one
rectangle (w × l) for packing.  Heights are the tallest part in the block.
"""
from math import ceil

# name: footprint (w, l) mm, height mm, description, placement hint
BLOCKS = {
    "buttons":    dict(fp=None, h=None, fixed=True, desc="5 × 6×6 tact switches, wheel face (fixed: donor centres)"),
    "wheel_if":   dict(fp=None, h=None, fixed=True, desc="13P 0.3 mm wheel FPC connector + ESD (fixed: donor position)"),
    "jack_if":    dict(fp=None, h=None, fixed=True, desc="jack/hold flex connector + ESD (fixed: donor position)"),
    "usb_c":      dict(fp=None, h=None, fixed=True, desc="USB-C receptacle + ESD + CC resistors (fixed: centred on the shell's dock hole; mount follows the board)"),
    "amoled_if":  dict(fp=None, h=None, fixed=True, desc="30P 0.5 mm AMOLED FPC connector + panel supply (position set per board)"),
    "mcu":        dict(fp=(13.0, 13.0), h=0.9, desc="RP2350B QFN-80 + 12 MHz crystal + decoupling", near="board_centre"),
    "memory":     dict(fp=(13.0, 6.5), h=1.0, desc="QSPI PSRAM + flash (2 × WSON-8)", near="mcu"),
    "microsd":    dict(fp=(15.0, 14.5), h=1.85, desc="microSD push-push socket", near="sd_mux"),
    "usb_mux":    dict(fp=(3.5, 3.5), h=0.6, desc="USB 2.0 HS 2:1 switch (TS3USB221 class): USB-C → MCU or SD bridge", near="usb_c"),
    "sd_bridge":  dict(fp=(7.5, 7.5), h=0.9, desc="USB 2.0 HS → SD card reader bridge (GL823K / USB2240 class) + crystal: card mounts on the PC at full speed", near="usb_mux"),
    "sd_mux":     dict(fp=(4.5, 4.5), h=0.6, desc="SD bus switch, 6 lines (MCU SDIO ↔ bridge)", near="sd_bridge"),
    "radio":      dict(fp=(18.5, 14.5), h=2.5, desc="Raspberry Pi RM2 (CYW43439) module", near="rf_window"),
    "audio":      dict(fp=(9.0, 9.0), h=0.9, desc="codec + headphone amp + mic bias / TRRS detect", near="jack_if"),
    "charger":    dict(fp=(9.0, 8.0), h=1.2, desc="charger / power path (QFN 4×4 + low-profile 2.5×2 inductor)", near="batt_conn"),
    "gauge":      dict(fp=(4.5, 4.5), h=0.9, desc="fuel gauge + sense resistor", near="batt_conn"),
    "power":      dict(fp=(8.0, 6.0), h=1.2, desc="3V3 buck + LDOs", near="charger"),
    "batt_conn":  dict(fp=(7.0, 4.5), h=1.0, desc="battery connector (3-pin, NTC)", near="battery"),
    "haptic_drv": dict(fp=(4.5, 4.5), h=0.9, desc="Taptic/LRA driver (DRV2605L class)", near="haptic_conn"),
    "haptic_conn": dict(fp=(6.0, 4.0), h=1.0, desc="new connector for the Taptic Engine lead (not the piezo contacts)", near="haptic"),
}

# (a, b, signals, label, kind)  kind: sig | analog | power (power pins count double for current)
INTERFACES = [
    ("mcu", "memory", 7, "QSPI", "sig"),
    ("mcu", "sd_mux", 7, "SDIO 4-bit + detect (MCU side)", "sig"),
    ("sd_bridge", "sd_mux", 6, "SD bus (bridge side)", "sig"),
    ("sd_mux", "microsd", 7, "SD bus to the card", "sig"),
    ("usb_c", "usb_mux", 2, "USB HS D+/D−", "hs"),
    ("usb_mux", "mcu", 2, "USB HS D+/D− (MCU)", "hs"),
    ("usb_mux", "sd_bridge", 2, "USB HS D+/D− (SD bridge)", "hs"),
    ("mcu", "usb_mux", 2, "mux select, SD mux select", "sig"),
    ("mcu", "radio", 4, "gSPI + WL_ON", "sig"),
    ("mcu", "amoled_if", 9, "QSPI + RST/TE/EN", "sig"),
    ("mcu", "audio", 6, "I2S + MCLK + det/en", "sig"),
    ("mcu", "wheel_if", 3, "wheel serial", "sig"),
    ("mcu", "buttons", 5, "switch lines (if not via the wheel PSoC)", "sig"),
    ("mcu", "jack_if", 1, "hold switch", "sig"),
    ("audio", "jack_if", 4, "HP L/R, mic, detect", "analog"),
    ("charger", "usb_c", 3, "VBUS, CC1, CC2", "power"),
    ("charger", "batt_conn", 2, "VBAT, NTC", "power"),
    ("gauge", "batt_conn", 1, "cell sense", "sig"),
    ("charger", "mcu", 2, "INT, CE", "sig"),
    ("gauge", "mcu", 1, "ALERT", "sig"),
    ("haptic_drv", "mcu", 2, "EN, PWM", "sig"),
    ("haptic_drv", "haptic_conn", 2, "Taptic drive ±", "power"),
]
I2C_MEMBERS = ("charger", "gauge", "haptic_drv", "audio")      # master: mcu
SUPPLIED_3V3 = ("mcu", "memory", "microsd", "radio", "amoled_if", "audio", "gauge", "haptic_drv", "wheel_if", "sd_bridge", "usb_mux", "sd_mux")

# kind "hs" = USB 2.0 high-speed pair: keep it on one board (the split reports it if it crosses the link)
# The split.  Default = proposal for discussion (see space_report.md §3a):
# keep the MCU and its fast buses (QSPI memory, display, audio, wheel, buttons) on the stock-position board so the
# high-speed and analog nets never cross the link; put the tall / power parts (microSD, radio, charger, rails,
# battery connector) on the second board.
SPLIT_DEFAULT = dict(          # USB-C, USB mux, SD bridge, SD switch and the card on one board: no HS pair crosses
    front=["buttons", "wheel_if", "jack_if", "usb_c", "usb_mux", "sd_bridge", "sd_mux", "microsd", "amoled_if",
           "mcu", "memory", "audio", "haptic_drv", "haptic_conn"],
    second=["radio", "charger", "gauge", "power", "batt_conn"],
)
SPLIT_SD_LOWER = dict(         # alternative for B/D1: USB-C + SD chain on the lower board behind the ribs
    front=["buttons", "wheel_if", "jack_if", "amoled_if", "mcu", "memory", "audio", "haptic_drv", "haptic_conn"],
    second=["usb_c", "usb_mux", "sd_bridge", "sd_mux", "microsd", "radio", "charger", "gauge", "power", "batt_conn"],
)
SPLIT = {
    "A": dict(front=list(BLOCKS), second=[]),
    "C": dict(front=list(BLOCKS), second=[]),
    "B": SPLIT_DEFAULT,
    "D1": SPLIT_DEFAULT,
    "D2": dict(front=["buttons", "wheel_if", "jack_if", "usb_c", "usb_mux", "sd_bridge", "sd_mux", "mcu", "memory", "audio"],
               second=["amoled_if", "microsd", "radio", "charger", "gauge", "power", "batt_conn", "haptic_drv", "haptic_conn"]),
    "D1-sd-lower": SPLIT_SD_LOWER,
}


def board_of(layout):
    """block -> role"""
    sp = SPLIT[layout]
    out = {}
    for role, names in sp.items():
        for n in names:
            out[n] = role
    missing = set(BLOCKS) - set(out)
    assert not missing, f"{layout}: blocks with no board: {missing}"
    return out


def crossing(layout):
    """Signals that cross the link for this split: list of (label, pins, kind) and totals."""
    where = board_of(layout)
    rows = []
    for a, b, n, label, kind in INTERFACES:
        if where[a] != where[b]:
            rows.append((f"{a} ↔ {b}: {label}", n * (2 if kind == "power" else 1), kind))
    if any(where[m] != where["mcu"] for m in I2C_MEMBERS):
        rows.append(("I2C bus (SDA, SCL)", 2, "sig"))
    if any(where[m] != where["power"] for m in SUPPLIED_3V3):
        rows.append(("3V3 rail", 2, "power"))
    hs = [r for r in rows if r[2] == "hs"]
    if where["charger"] != where["power"] or where["audio"] != where["charger"]:
        rows.append(("VSYS rail", 2, "power"))
    sig = sum(r[1] for r in rows if r[2] != "power")
    hs_pairs = len(hs)
    pwr = sum(r[1] for r in rows if r[2] == "power")
    gnd = max(4, ceil((sig + pwr) / 3)) if rows else 0
    total = sig + pwr + gnd
    total += total % 2
    return rows, dict(signals=sig, power=pwr, ground=gnd, total=total,
                      analog=any(r[2] == "analog" for r in rows), hs_pairs=hs_pairs)


def link_connector(pins, kind):
    """Rough connector body for a pin count. kind: 'mezzanine' (0.4 mm, 2 rows) or 'fpc' (0.5 mm ZIF, 1 row)."""
    if pins == 0:
        return None
    if kind == "mezzanine":
        return dict(w=4.0, l=round(pins / 2 * 0.4 + 2.6, 1), h=1.0, desc=f"{pins}-pin 0.4 mm board-to-board pair")
    return dict(w=round(pins * 0.5 + 3.0, 1), l=4.5, h=1.0, desc=f"{pins}-pin 0.5 mm FPC connector (+ matching flex)")
