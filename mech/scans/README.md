# Scans

Revopoint Metro X scans of the donor, as binary STL (OBJ: convert first), one file per part:

| file | what | datum features to capture |
|---|---|---|
| `midframe.stl` | stripped casting, both faces | both locating pins, screw holes, every rib (top and rear face), divider, dock bar |
| `back-shell.stl` | inside face | floor, wall height, battery pocket outline, jack screw bosses, top-end plastic insert |
| `front-assembly.stl` | inner face of bezel + glass, wheel in place | wheel hole, LCD aperture, bezel inner face |
| `logic-board-top.stl` | component (rear) face | outline, all holes, tallest parts |
| `logic-board-bottom.stl` | wheel face | five switch bodies, two wheel-locating holes, adhesive ring |

Alignment lives in `../parts/scan_ref.py` (`SCAN_ALIGN`): translate/rotate so that the pin axes sit on the X
axis with the origin midway between them and the board rear face is Z = 0. Scans are reference bodies only
(overlay, bounding boxes) — the parametric parts stay the source of truth, updated from the scan readings.

Large meshes: keep repo copies decimated to ≤ 5 MB (MeshLab → Quadric Edge Collapse); the full scans can live
in a release asset.
