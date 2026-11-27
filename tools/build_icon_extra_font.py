#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build the icon glyphs Lucide does not have into a small TrueType font, for tools/gen_ui_fonts.sh to merge into neupod_icons_*.

    python3 tools/build_icon_extra_font.py OUT.ttf      # prints each SVG's sha256 (one line each), for the generated header comment

The sources are the icon set's SVGs (docs/design/2026-11-26-icon-set/svg/, 24x24, 2 px round strokes; owner decisions there):
jack-plug -> U+EF00, record -> U+EF01, star-filled -> U+EF02 (UI_ICON_JACK_PLUG / _RECORD / _STAR_FILLED in app/ui/ui_icons.h).
Strokes are expanded to outlines with picosvg and scaled to the Lucide font's grid (24 units -> 1000 per em, y' = 1000 - y * 1000 / 24,
ascent 1000, descent 0, advance 1000), so lv_font_conv rasterises them with the same metrics as the Lucide glyphs. Lifted from
build_extra_font() in docs/design/2026-11-26-icon-set/render.py, which drew the mock-up sheets with the same font.

Needs exactly the pinned versions below (pip install fonttools==4.65.0 picosvg==0.23.0 skia-pathops==0.9.2): a different picosvg can expand a stroke
differently and move pixels in the generated fonts.
"""
import hashlib
import os
import sys

FONTTOOLS_VER = "4.65.0"
PICOSVG_VER = "0.23.0"
SKIA_PATHOPS_VER = "0.9.2"  # picosvg expands the strokes with it: a different version can move pixels too

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SVG_DIR = os.path.join(ROOT, "docs", "design", "2026-11-26-icon-set", "svg")
GLYPHS = [("jack-plug", 0xEF00), ("record", 0xEF01), ("star-filled", 0xEF02)]


def check_versions():
    from importlib.metadata import version

    got = {"fonttools": version("fonttools"), "picosvg": version("picosvg"), "skia-pathops": version("skia-pathops")}
    want = {"fonttools": FONTTOOLS_VER, "picosvg": PICOSVG_VER, "skia-pathops": SKIA_PATHOPS_VER}
    if got != want:
        sys.exit(f"build_icon_extra_font: need {want}, have {got} (pip install fonttools=={FONTTOOLS_VER} picosvg=={PICOSVG_VER} skia-pathops=={SKIA_PATHOPS_VER})")


def build(out_path):
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.cu2quPen import Cu2QuPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.svgLib.path import parse_path
    from picosvg.svg import SVG

    order, cmap, glyphs, metrics = [".notdef"], {}, {}, {".notdef": (1000, 0)}
    glyphs[".notdef"] = TTGlyphPen(None).glyph()
    sums = []
    for name, cp in GLYPHS:
        path = os.path.join(SVG_DIR, name + ".svg")
        with open(path, "rb") as f:
            sums.append(f"{name}.svg sha256 {hashlib.sha256(f.read()).hexdigest()}")
        pico = SVG.parse(path).topicosvg()
        pen = TTGlyphPen(None)
        tp = TransformPen(Cu2QuPen(pen, 0.5, reverse_direction=True), (1000 / 24, 0, 0, -1000 / 24, 0, 1000))
        for el in pico.svg_root.iter():
            if el.tag.endswith("path") and el.get("d"):
                parse_path(el.get("d"), tp)
        gname = "u%04X" % cp
        order.append(gname)
        cmap[cp] = gname
        glyphs[gname] = pen.glyph()
        metrics[gname] = (1000, 0)
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    # The left side bearing is the glyph's xMin, as in any TrueType font: lv_font_conv places the bitmap by it (0 would push every glyph to the left edge)
    glyf = fb.font["glyf"]
    for gname in order[1:]:
        g = glyf[gname]
        g.recalcBounds(glyf)
        metrics[gname] = (1000, g.xMin)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=1000, descent=0)
    fb.setupNameTable({"familyName": "neuPod Icons Extra", "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=1000, sTypoDescender=0, usWinAscent=1090, usWinDescent=0)
    fb.setupPost()
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    fb.save(out_path)
    return "\n".join(sums)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    check_versions()
    print(build(sys.argv[1]))
