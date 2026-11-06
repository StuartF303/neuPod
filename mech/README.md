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
  scans/           align_scans.py + datum-aligned, decimated scan meshes (aligned/)
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

## Scans

Raw Metro X scans (6 Nov 2026) live in Stuart's OneDrive neuPod folder; they are not committed.
`python mech/scans/align_scans.py <raw-folder>` squares each one up, levels it, places it on the datum using the
current `params.py`, decimates it and writes `mech/scans/aligned/*.stl` (committed, ~0.6 MB each). The model
loads them with `build(layout, scans=["frame_front", ...])`, and the web viewer shows them under "Scans".

Values read off the scans carry the `scan` source tag in `params.py`; what still needs calipers is listed at the
end of `space_report.md` §5.

## Conventions

- One function per part in `parts/`; each returns a `Part(name, shape, colour, group, fixed, notes)` positioned
  in the datum frame. Candidate placements are `fixed=False`.
- Nothing is hard-coded in a part builder: every number comes from `params.py`, and every derived value is
  computed there too so the dependency is visible.
- Keep renders under 500 KB (`render.py` writes ~20–110 KB PNGs).
