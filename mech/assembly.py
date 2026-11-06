"""Assemble stock parts + one candidate layout.

    python -m mech.assembly            # exports STEP of stock + each layout to mech/out/
    from mech.assembly import build
    parts = build("D2")                # list of Part

Toggles: layout name ("stock", "A", "B", "C", "D1", "D2"); display ("stock"|"amoled");
show_removed (HDD + donor battery ghosts); OPTIONS in layouts.py for MCU/radio.
"""
import sys
from pathlib import Path
import cadquery as cq

from . import params as p
from . import layouts as L
from .parts import shell, midframe, board, front, cavity
from .parts.scan_ref import load_scan

OUT = Path(__file__).resolve().parent / "out"


def stock_parts(display="amoled", show_removed=False, donor_board=True):
    parts = [shell.back_shell(), shell.rf_window(), shell.front_plate(), midframe.midframe(), front.jack_flex()]
    parts += midframe.locating_pins()
    parts += front.click_wheel()
    parts += front.amoled() if display == "amoled" else front.stock_lcd()
    if donor_board:
        parts.append(board.donor_board())
        parts += board.switch_zone()
        parts += board.stock_connectors()
    if show_removed:
        parts += [cavity.hdd(), cavity.donor_battery()]
    return parts


def build(layout="stock", display=None, show_removed=False, scans=()):
    display = display or L.OPTIONS["display"]
    if layout == "stock":
        parts = stock_parts(display, show_removed=True, donor_board=True)
        meta = {}
    else:
        parts = stock_parts(display, show_removed=False, donor_board=False)
        cand, meta = L.LAYOUTS[layout]()
        parts += cand
    for s in scans:
        ref = load_scan(s)
        if ref is not None:
            parts.append(ref)
    return parts, meta


def to_cq_assembly(parts):
    asm = cq.Assembly(name="neuPod")
    for pt in parts:
        asm.add(pt.shape, name=pt.name, color=cq.Color(*pt.colour))
    return asm


def board_assemblies(parts):
    """owner -> list of parts (each PCB with its components and connectors, as one piece)."""
    out = {}
    for pt in parts:
        if pt.owner and pt.owner != "link":
            out.setdefault(pt.owner, []).append(pt)
    return out


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    names = sys.argv[1:] or ["stock"] + list(L.LAYOUTS)
    for nm in names:
        parts, _ = build(nm)
        to_cq_assembly(parts).save(str(OUT / f"neupod_{nm}.step"))
        print("wrote", OUT / f"neupod_{nm}.step", len(parts), "parts")
        for owner, ps in board_assemblies(parts).items():      # one STEP per PCB assembly, for KiCad / enclosure work
            f = OUT / f"neupod_{nm}_{owner}.step"
            to_cq_assembly(ps).save(str(f))
            print("   ", f.name, len(ps), "parts")
