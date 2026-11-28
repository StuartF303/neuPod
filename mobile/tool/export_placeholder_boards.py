#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Export the three PLACEHOLDER board pictures from the design's render.py (board_svg).

    python3 mobile/tool/export_placeholder_boards.py

Needs `rsvg-convert` and `cwebp` (brew install librsvg webp). Writes mobile/assets/boards/<id>.webp,
1200 x 3120, lossy quality 85. Each carries `PLACEHOLDER - NOT A NEUPOD BOARD` across its face.
Then run `dart run tool/build_board.dart` from mobile/ for the frost copies and boards.json.
Real pictures (KiCad renders or photographs of neuPod's own boards) replace these: the owner's to
supply (design README, Open question 2). Never add a picture of a board that is not neuPod's own.
"""
import math, os, random, subprocess, tempfile, types

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
RENDER = os.path.join(REPO, "docs", "design", "2026-11-25-app-custom-mode", "render.py")
OUT = os.path.join(HERE, "..", "assets", "boards")

# render.py as a whole needs Python 3.12 (nested quotes in f-strings); only its board pictures are
# wanted here, so take that part of its source: hexs(), BOARDS, the asset constants and board_svg().
src = open(RENDER).read()
part = (src[src.index("def hexs"):src.index("def rgba")]
        + src[src.index("BOARDS = {"):src.index("def frost_css")])
render = types.SimpleNamespace()
scope = {"math": math, "random": random, "FONT": "Montserrat, sans-serif"}
exec(part, scope)
render.BOARDS, render.board_svg, render.hexs = scope["BOARDS"], scope["board_svg"], scope["hexs"]

os.makedirs(OUT, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="neupod-board-") as tmp:
    for board in render.BOARDS:
        svg, png = os.path.join(tmp, board + ".svg"), os.path.join(tmp, board + ".png")
        with open(svg, "w") as f:
            f.write(render.board_svg(board))
        subprocess.run(["rsvg-convert", "-w", "1200", "-h", "3120", "-b", render.hexs(render.BOARDS[board]["ground"]),
                        "-o", png, svg], check=True)
        subprocess.run(["cwebp", "-quiet", "-q", "85", png, "-o", os.path.join(OUT, board + ".webp")], check=True)
        print("wrote", board)
