# Mid-frame rebuilt from the scans

`frame_from_scan.step` — the cast mid-frame as one valid solid, rebuilt from the front and rear Metro X
scans (6 Nov 2026). Fit to the scan surface: median 0.08 mm, 90 % within 0.24 mm.

Frame: mm; X/Y centred on the casting outline (front view, +Y toward the LCD); Z = 0 at the front-most
face (dock bar), −Z toward the back shell. Overall depth 7.2.

| feature | front face Z | rear face Z |
|---|---|---|
| dock bar | −0.1 | −1.0 |
| side walls, lower half | ≈ −1.2 | ≈ −5.6 |
| side walls, upper half | −1.7 … −4.9 | ≈ −5.8 |
| rib grid (board seat) | −5.0 | −5.6 |
| top wall | −5.2 | −5.9 |
| divider | −5.0 | −7.1 |

Pipeline: `merge_frame.py <raw-scan-folder>` (register the halves, level on the rib face, height maps,
flat levels) → `build_slabs.py` (stack of Z-slabs, one outline set per slab, fused into one solid).

Not captured (scanner limits): the screw holes and locating pins (filled/too thin), walls thinner than
0.15 mm, undercuts and anything only visible side-on. Side walls are slightly stepped where they slope.
Use calipers for pins, screw holes and the wall clips.
