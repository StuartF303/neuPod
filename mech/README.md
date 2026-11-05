# neuPod mechanical model (`mech/`)

Parametric CadQuery model of the iPod Classic 6G (A1238, thin) interior for component placement, plus the
space-allocation study that compares candidate board layouts. Licence: CERN-OHL-S v2
([LICENSE-CERN-OHL-S-2.0](../LICENSE-CERN-OHL-S-2.0)).

**Frame of reference:** units mm; origin at the midpoint of the two mid-frame locating pins; +X across the
device (right, viewed from the front), +Y toward the LCD / headphone end, +Z toward the glass; Z = 0 is the
donor logic board's rear (component) face — the face that looks at the ribs.

```
mech/
  params.py        every dimension, tagged measured / photo / datasheet / estimate / derived
  parts/           one builder per part (shell, midframe, board, front, cavity, scan_ref)
  layouts.py       candidate layouts A, B, C, D1, D2 and the part toggles (OPTIONS)
  assembly.py      stock + layout assembly; `python -m mech.assembly` exports STEP to mech/out/
  space_study.py   free volumes, clash lists, per-layout metrics → space_report.md
  render.py        off-screen VTK renders → renders/<layout>_{iso,top,section_side,section_wheel,exploded}.png
  scans/           Metro X scans (STL) and their alignment notes
  space_report.md  the study output — read §1 and §5 first
```

## Run

```sh
pip install cadquery cairosvg            # CadQuery 2.8 / OCP 7.9 tested; VTK comes with cadquery
python -m mech.space_study               # ~15 s, both depth models, writes space_report.md
xvfb-run -a python -m mech.render        # ~2 min for all layouts; omit xvfb-run on a desktop
python -m mech.assembly D2               # STEP export for KiCad / FreeCAD
NEUPOD_DEPTH_MODEL=stated python -m mech.render D2   # renders with the 7 mm cavity forced
```

Toggles: `layouts.OPTIONS` (`mcu`: RP2350B | ESP32S3, `radio`: RM2 | BT1026D, `display`: amoled | stock),
`params.SWITCH_BOARD_SHAPE` (square | round), `params.DEPTH_MODEL` or the `NEUPOD_DEPTH_MODEL` env var.

## Updating from the scans

1. Drop `midframe.stl`, `back-shell.stl`, `front-assembly.stl`, `logic-board-top.stl`, `logic-board-bottom.stl`
   into `mech/scans/` (see `scans/README.md`).
2. Fit each scan to the datum in your mesh tool (pins → X axis and origin; board rear face → Z = 0) and put the
   residual transform in `parts/scan_ref.SCAN_ALIGN`.
3. `build(layout, scans=["midframe"])` overlays the scan as a translucent reference body; compare it with the
   parametric part in a render and replace the `estimate` / `photo` values in `params.py`, one at a time,
   re-running the study after each group (the order that matters is in `space_report.md` §5).

## Conventions

- One function per part in `parts/`; each returns a `Part(name, shape, colour, group, fixed, notes)` positioned
  in the datum frame. Candidate placements are `fixed=False`.
- Nothing is hard-coded in a part builder: every number comes from `params.py`, and every derived value is
  computed there too so the dependency is visible.
- Keep renders under 500 KB (`render.py` writes ~20–110 KB PNGs).
