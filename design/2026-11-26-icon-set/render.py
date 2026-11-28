#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Render the icon set's sheets: standalone HTML and PNG in mockups/, plus the preview icon font.

    python3 docs/design/2026-11-26-icon-set/render.py [--no-png]

1. Builds build/neupod-icons-extra.ttf from the glyphs in svg/ that are not in Lucide (strokes expanded with
   picosvg, scaled to the Lucide font's grid: 24 units -> 1000 per em, ascent 1000, descent 0), at U+EF00 up.
2. Rasterises every glyph at 16, 24, 30, 36 and 45 px with lv_font_conv 1.5.3 at 1 bpp, the same tool and flags
   tools/gen_ui_fonts.sh uses: Lucide glyphs from the pinned lucide-static 1.72.0 font, new ones from the font of
   step 1. The sheets therefore show the firmware's own bitmaps, pixel for pixel, not an approximation.
3. Writes the sheets (glyphs on three grounds, the status bar, the app icon) and, with Chrome, their PNGs.

Needs node/npx, python3 with fonttools, picosvg and pillow, and Chrome for PNGs (`google-chrome` or CHROME=...).
Classic Dark is NOT designed yet (UI styles open question 1): its colours here are provisional, for icon checks only.
"""
import json, math, os, subprocess, sys, tarfile, tempfile, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SVG_DIR, OUT, BUILD = (os.path.join(HERE, d) for d in ("svg", "mockups", "build"))
SIZES = (16, 24, 30, 36, 45)
LUCIDE_VER = "1.72.0"
FONT = "Montserrat, system-ui, sans-serif"
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
MONTSERRAT = os.environ.get("MONTSERRAT") or os.path.join(REPO, "lib", "lvgl", "scripts", "generators", "built_in_font", "Montserrat-Medium.ttf")

# ---- the glyphs: name, source, Lucide code point (or our U+EF00 block), meaning ----------------------
GLYPHS = [
    ("jack-plug",           "new",   0xEF00, "Output set to Line (replaces the drawing in classic-status-states)"),
    ("fast-forward",        "lucide", 0xE0BD, "Scanning forward (held Next)"),
    ("rewind",              "lucide", 0xE147, "Scanning back (held Previous)"),
    ("loader-circle",       "lucide", 0xE10A, "Opening / loading a stream"),
    ("play-off",            "lucide", 0xE6FE, "Can't play"),
    ("record",              "new",   0xEF01, "Recording (dot is the cue; red is extra)"),
    ("ellipsis",            "lucide", 0xE0B6, "Busy: library update in the background"),
    ("speaker",             "lucide", 0xE166, "Output: external / wireless speaker (never AirPlay's glyph)"),
    ("download",            "lucide", 0xE0B2, "Download (action)"),
    ("cloud-download",      "lucide", 0xE089, "Downloading: iCloud file not on the phone"),
    ("star",                "lucide", 0xE176, "Rating star, unlit"),
    ("star-filled",         "new",   0xEF02, "Rating star, lit (derived from Lucide star)"),
    ("bluetooth-connected", "lucide", 0xE1B8, "Bluetooth headset connected (shape cue besides blue)"),
]
# Existing Lucide glyphs the status-bar sheet also needs (already in the firmware's icon font)
EXISTING = {"play": 0xE13C, "pause": 0xE12E, "wifi": 0xE1AE, "bluetooth": 0xE05C, "headphones": 0xE0F1,
            "lock": 0xE10B, "shuffle": 0xE15E, "repeat": 0xE146}

# ---- styles (Classic Light and Modern Dark: UI styles README; Classic Dark: PROVISIONAL) -------------
STYLES = {
    "classic-light": dict(name="Classic Light", ground="#ffffff", ink="#181818", ink2="#606060",
        bar="linear-gradient(#fdfdfd, #dcdfe3 50%, #c6cad0 51%, #b6bbc2)", bar_edge="1px solid #8c9199",
        bar_stops=("#fdfdfd", "#dcdfe3", "#c6cad0", "#b6bbc2"),
        glyph="#3a3d42", dim="#474b52", bt="#123f8f", rec="#8a1a15", lit="#1858c8", unlit="#737373",
        batt_outline="#5d626a", batt_in="#f4f5f6", batt_fill="linear-gradient(#c9f5a3, #63c62f 45%, #3f9c1c 55%, #58b82a)",
        batt_r=4, term="#5d626a"),
    "classic-dark": dict(name="Classic Dark (provisional)", ground="#0d0e10", ink="#ededed", ink2="#b4b4b4",
        bar="linear-gradient(#45484e, #2a2c31 50%, #1b1d21 51%, #121316)", bar_edge="1px solid #000000",
        bar_stops=("#45484e", "#2a2c31", "#1b1d21", "#121316"),
        glyph="#dfe2e6", dim="#b5bac1", bt="#9ccbff", rec="#ffa79e", lit="#6db3f5", unlit="#8e939a",
        batt_outline="#a6abb3", batt_in="#1b1d20", batt_fill="linear-gradient(#c9f5a3, #63c62f 45%, #3f9c1c 55%, #58b82a)",
        batt_r=4, term="#a6abb3"),
    "modern": dict(name="Modern Dark", ground="#050608", ink="#f0f0f0", ink2="#c0c0c0",
        bar="rgba(10,10,12,.5)", bar_edge="1px solid rgba(255,255,255,.10)",
        bar_stops=("#0a0a0c", "#2f2a1f"), glyph="#f0f0f0", dim="#9a9a9a", bt="#5aa2ff", rec="#ff7b70", lit="#f2a586", unlit="#7c7c7c",
        batt_outline="rgba(255,255,255,.6)", batt_in="transparent", batt_fill="linear-gradient(#8cecb4, #30b870)",
        batt_r=8, term="rgba(255,255,255,.6)"),
}

# ---- contrast ----------------------------------------------------------------------------------------
def _lin(c):
    c /= 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

def lum(h):
    h = h.lstrip("#")
    r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * _lin(r) + 0.7152 * _lin(g) + 0.0722 * _lin(b)

def ratio(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)

def contrast_table():
    rows = []
    for key, s in STYLES.items():
        grounds = [("ground", s["ground"])] + [("bar stop", c) for c in s["bar_stops"]]
        for role in ("glyph", "dim", "bt", "rec", "lit", "unlit", "ink2"):
            worst = min(grounds, key=lambda g: ratio(s[role], g[1]))
            if role in ("lit", "unlit", "ink2"):
                worst = ("ground", s["ground"])
            rows.append((s["name"], role, s[role], worst[1], round(ratio(s[role], worst[1]), 2)))
    return rows

# ---- 1. the extra font ---------------------------------------------------------------------------------
def build_extra_font():
    from picosvg.svg import SVG
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.pens.cu2quPen import Cu2QuPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.svgLib.path import parse_path
    os.makedirs(BUILD, exist_ok=True)
    news = [g for g in GLYPHS if g[1] == "new"]
    order, cmap, glyphs, metrics = [".notdef"], {}, {}, {".notdef": (1000, 0)}
    glyphs[".notdef"] = TTGlyphPen(None).glyph()
    for name, _, cp, _ in news:
        pico = SVG.parse(os.path.join(SVG_DIR, name + ".svg")).topicosvg()
        pen = TTGlyphPen(None)
        tp = TransformPen(Cu2QuPen(pen, 0.5, reverse_direction=True), (1000 / 24, 0, 0, -1000 / 24, 0, 1000))
        for el in pico.svg_root.iter():
            if el.tag.endswith("path") and el.get("d"):
                parse_path(el.get("d"), tp)
        gname = "u%04X" % cp
        order.append(gname); cmap[cp] = gname; glyphs[gname] = pen.glyph(); metrics[gname] = (1000, 0)
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(order); fb.setupCharacterMap(cmap); fb.setupGlyf(glyphs)
    # the left side bearing is the glyph's xMin, as tools/build_icon_extra_font.py writes it (0 put every glyph against the cell's left edge)
    glyf = fb.font["glyf"]
    for gname in order[1:]:
        glyf[gname].recalcBounds(glyf)
        metrics[gname] = (1000, glyf[gname].xMin)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=1000, descent=0)
    fb.setupNameTable({"familyName": "neuPod Icons Extra", "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=1000, sTypoDescender=0, usWinAscent=1090, usWinDescent=0)
    fb.setupPost()
    path = os.path.join(BUILD, "neupod-icons-extra.ttf")
    fb.save(path)
    return path

def lucide_font():
    path = os.path.join(BUILD, f"lucide-{LUCIDE_VER}.ttf")
    if not os.path.exists(path):
        with tempfile.TemporaryDirectory() as t:
            subprocess.run(["npm", "pack", f"lucide-static@{LUCIDE_VER}", "--silent"], cwd=t, check=True, stdout=subprocess.DEVNULL)
            with tarfile.open(os.path.join(t, f"lucide-static-{LUCIDE_VER}.tgz")) as tf:
                with open(path, "wb") as f:
                    f.write(tf.extractfile("package/font/lucide.ttf").read())
    return path

# ---- 2. rasterise with lv_font_conv ---------------------------------------------------------------------
def dump(font, cps, size):
    from PIL import Image
    with tempfile.TemporaryDirectory() as t:
        subprocess.run(["npx", "--yes", "lv_font_conv@1.5.3", "--no-compress", "--no-prefilter", "--bpp", "1", "--size", str(size),
                        "--font", font, "-r", ",".join(hex(c) for c in cps), "--format", "dump", "-o", t],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        info = json.load(open(os.path.join(t, "font_info.json")))
        out = {}
        for g in info["glyphs"]:
            im = Image.open(os.path.join(t, "%x.png" % g["code"])).convert("L")
            w, h = im.size
            px = [[im.getpixel((x, y)) < 128 for x in range(w)] for y in range(h)]
            out[g["code"]] = dict(x=g["bbox"]["x"], rows=px, adv=round(g["advanceWidth"]))
        return out

def rasterise():
    lf, ef = lucide_font(), build_extra_font()
    luc = [g[2] for g in GLYPHS if g[1] == "lucide"] + list(EXISTING.values())
    new = [g[2] for g in GLYPHS if g[1] == "new"]
    bm = {}
    for s in SIZES:
        d = dump(lf, luc, s); d.update(dump(ef, new, s))
        bm[s] = d
    return bm

# ---- pixel glyphs as SVG ---------------------------------------------------------------------------------
def pix(bm, size, cp, color, scale=1, label=None):
    g = bm[size][cp]
    rows = g["rows"]
    w = max(size, g["x"] + (len(rows[0]) if rows else 0))
    h = len(rows)
    d = []
    for y, row in enumerate(rows):
        x = 0
        while x < len(row):
            if row[x]:
                x0 = x
                while x < len(row) and row[x]:
                    x += 1
                d.append(f"M{g['x'] + x0} {y}h{x - x0}v1h-{x - x0}z")
            else:
                x += 1
    aria = f'role="img" aria-label="{label}"' if label else 'aria-hidden="true"'
    return (f'<svg {aria} width="{w * scale}" height="{h * scale}" viewBox="0 0 {w} {h}" shape-rendering="crispEdges" '
            f'style="display:block"><path fill="{color}" d="{"".join(d)}"/></svg>')

def page(title, w, h, body, bg="#ffffff"):
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>neuPod mock-up: {title}</title>
<!-- {w}x{h} px. Design mock-up, not firmware output, except the glyph pixels: those are lv_font_conv 1.5.3 output at 1 bpp
     (render.py). Font: Montserrat Medium. Icons: Lucide (ISC) and neuPod's own (svg/). -->
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Montserrat:wght@500&display=swap">
<style>body{{margin:0}}</style>
</head>
<body>
<div style="width: {w}px; height: {h}px; box-sizing: border-box; background: {bg}; font-family: {FONT}; font-weight: 500; overflow: hidden">
{body}
</div>
</body>
</html>
"""

# ---- 3a. glyph sheets ------------------------------------------------------------------------------------
def glyph_sheet(bm, key):
    s = STYLES[key]
    W, ROW = 1000, 76
    head = "".join(f'<div style="width: 64px; text-align: center">{z} px</div>' for z in SIZES)
    rows = []
    for name, src, cp, meaning in GLYPHS:
        color = s["glyph"]
        if name == "star-filled": color = s["lit"]
        if name == "star": color = s["unlit"]
        if name == "bluetooth-connected": color = s["bt"]
        if name == "record": color = s["rec"]
        cells = "".join(f'<div style="width: 64px; display: flex; justify-content: center">{pix(bm, z, cp, color, label=None)}</div>' for z in SIZES)
        rows.append(f"""<div style="height: {ROW}px; display: flex; align-items: center; gap: 0; border-top: 1px solid {s['unlit']}55">
  <div style="width: 300px; display: flex; flex-direction: column; gap: 2px"><div style="font-size: 17px; color: {s['ink']}">{name}</div><div style="font-size: 12px; line-height: 15px; color: {s['ink2']}">{meaning}</div></div>
  <div style="width: 90px; font-size: 12px; color: {s['ink2']}">{'Lucide' if src == 'lucide' else ('new, from Lucide star' if name == 'star-filled' else 'new')}<br>U+{cp:04X}</div>
  {cells}
  <div style="width: 110px; display: flex; justify-content: center">{pix(bm, 16, cp, color, scale=4, label=name + ' at 16 px, 4 times')}</div>
</div>""")
    def rating(size, n_lit=3):
        st = "".join(pix(bm, size, GLYPHS[11][2] if i < n_lit else GLYPHS[10][2], s["lit"] if i < n_lit else s["unlit"]) for i in range(5))
        return f'<div style="display: flex; gap: {max(2, size // 8)}px">{st}</div>'
    body = f"""<div style="padding: 28px 36px; display: flex; flex-direction: column; gap: 10px; color: {s['ink']}">
  <div style="display: flex; justify-content: space-between; align-items: baseline"><div style="font-size: 26px">Glyphs, 1-bit, on {s['name']}</div><div style="font-size: 13px; color: {s['ink2']}">ground {s['ground']}, glyph {s['glyph']}; pixels are lv_font_conv output</div></div>
  <div style="display: flex; font-size: 12px; color: {s['ink2']}; padding-top: 6px"><div style="width: 390px">Glyph and meaning</div>{head}<div style="width: 110px; text-align: center">16 px, x4</div></div>
  {''.join(rows)}
  <div style="border-top: 1px solid {s['unlit']}55; padding-top: 14px; display: flex; gap: 48px; align-items: flex-end">
    <div style="display: flex; flex-direction: column; gap: 8px"><div style="font-size: 12px; color: {s['ink2']}">Rating in a list, 16 px (320x240)</div>{rating(16)}</div>
    <div style="display: flex; flex-direction: column; gap: 8px"><div style="font-size: 12px; color: {s['ink2']}">Rating in a list, 30 px (600x450)</div>{rating(30)}</div>
    <div style="display: flex; flex-direction: column; gap: 8px"><div style="font-size: 12px; color: {s['ink2']}">Now Playing, 45 px (600x450)</div>{rating(45)}</div>
    <div style="font-size: 12px; line-height: 16px; color: {s['ink2']}; max-width: 230px">Three of five. Lit: filled, accent {s['lit']}. Unlit: outline, line grey {s['unlit']}. Filled versus outline is the non-colour cue.</div>
  </div>
</div>"""
    H = 150 + ROW * len(GLYPHS) + 190
    return page(f"Glyphs on {s['name']}", W, H, body, s["ground"]), W, H

# ---- 3b. the status-bar sheet ----------------------------------------------------------------------------
STATES = [
    ("Scanning forward; headset connected; Wi-Fi joined", ["fast-forward"], "3 of 12", "Now Playing", [("wifi", "glyph"), ("bluetooth-connected", "bt")]),
    ("Scanning back; headset saved, not connected; wired", ["rewind"], "3 of 12", "Now Playing", [("bluetooth", "dim"), ("headphones", "glyph")]),
    ("Opening a stream; output to a wireless speaker", ["loader-circle"], "", "Radio", [("wifi", "glyph"), ("speaker", "glyph")]),
    ("Library updating in the background; locked", ["play", "shuffle"], "1,204", "Songs", [("ellipsis", "glyph"), ("lock", "glyph")]),
    ("Track can't play; output set to Line", ["play-off"], "4 of 12", "Now Playing", [("jack-plug", "glyph")]),
    ("Recording", ["record"], "0:42", "Recording", [("headphones", "glyph")]),
]
CP = {g[0]: g[2] for g in GLYPHS}; CP.update(EXISTING)

def battery(s, k):
    w, h, b = round(44 * k), round(22 * k), max(1, round(2 * k))
    return (f'<div style="width: {w}px; height: {h}px; box-sizing: border-box; padding: {b}px; border: {b}px solid {s["batt_outline"]}; '
            f'border-radius: {max(2, round(s["batt_r"] * k))}px; background: {s["batt_in"]}"><div style="width: 78%; height: 100%; '
            f'border-radius: {max(1, round((4 if s["batt_r"] == 8 else 1) * k))}px; background: {s["batt_fill"]}"></div></div>'
            f'<div style="width: {max(2, round(4 * k))}px; height: {round(10 * k)}px; background: {s["term"]}; border-radius: 0 2px 2px 0; margin-left: {1 if s["batt_r"] == 8 else 0}px"></div>')

def status_screen(bm, key, W, H):
    s = STYLES[key]
    k = W / 600
    isz = 30 if W == 600 else 16
    barh, pad, gap = round(45 * k), round(19 * k), round(8 * k)
    small, body = round(22 * k), round(26 * k)
    lab = 13 if W == 600 else 8
    bars = []
    for label, left, ctx, title, right in STATES:
        L = "".join(pix(bm, isz, CP[n], s["rec"] if n == "record" else s["glyph"]) for n in left)
        R = "".join(f'<div style="margin-right: {round(10 * k)}px">{pix(bm, isz, CP[n], s[c])}</div>' for n, c in right)
        bars.append(f"""<div style="display: flex; flex-direction: column; gap: {round(3 * k)}px">
  <div style="padding: 0 {pad}px; font-size: {lab}px; line-height: {lab + 3}px; color: {s['ink2']}; white-space: nowrap">{label}</div>
  <div style="height: {barh}px; box-sizing: border-box; display: grid; grid-template-columns: 1fr auto 1fr; align-items: center; padding: 0 {pad}px; background: {s['bar']}; border-bottom: {s['bar_edge']}">
    <div style="display: flex; align-items: center; gap: {gap}px; font-size: {small}px; color: {s['glyph']}"><div style="display: flex; gap: {max(2, round(4 * k))}px">{L}</div><span>{ctx}</span></div>
    <div style="font-size: {body}px; color: {s['ink']}">{title}</div>
    <div style="display: flex; justify-content: flex-end; align-items: center">{R}{battery(s, k)}</div>
  </div>
</div>""")
    back = s["ground"]
    if key == "modern":
        back = ("linear-gradient(rgba(5,6,8,.30), rgba(5,6,8,.55)), radial-gradient(circle at 85% 8%, rgba(217,115,78,.5), transparent 55%), "
                "radial-gradient(circle at 10% 95%, rgba(233,196,106,.8), transparent 60%), #050608")
    return (f'<div style="width: {W}px; height: {H}px; box-sizing: border-box; padding: {round(10 * k)}px 0; display: flex; flex-direction: column; '
            f'justify-content: space-between; background: {back}; overflow: hidden; box-shadow: 0 0 0 1px #00000022">{"".join(bars)}</div>')

def status_sheet(bm):
    W, H = 1960, 930
    cols = []
    for key, s in STYLES.items():
        cols.append(f"""<div style="display: flex; flex-direction: column; gap: 14px">
  <div style="font-size: 20px; color: #181818">{s['name']}</div>
  {status_screen(bm, key, 600, 450)}
  <div style="font-size: 14px; color: #606060; padding-top: 14px">320x240, icons 16 px</div>
  {status_screen(bm, key, 320, 240)}
</div>""")
    body = f"""<div style="padding: 36px 40px; display: flex; flex-direction: column; gap: 22px">
  <div style="display: flex; justify-content: space-between; align-items: baseline"><div style="font-size: 30px; color: #181818">Status bar with the new glyphs</div>
  <div style="font-size: 14px; color: #606060">Left: play state, then context. Centre: title. Right: busy, Wi-Fi, Bluetooth, output, lock, battery. 600x450 icons 30 px, 1-bit.</div></div>
  <div style="display: flex; gap: 40px">{''.join(cols)}</div>
</div>"""
    return page("Status bar sheet", W, H, body, "#f3f4f6"), W, H

# ---- 3c. the app icon ------------------------------------------------------------------------------------
HARB = dict(body="#2b7bb4", body_lo="#215f8c", wheel="#f2f0ec", wheel_lo="#dcd8d0", centre="#23628f", centre_lo="#1b4d72", groove="#173f5c")

def wheel_shapes(cx, cy, R, ring, ring_lo, centre, centre_lo, groove, mark, uid, mono=False, wedge=None):
    """neuPod's wheel, from the device view's geometry: ring 0.44 R to R, centre button 0.36 D across inside a groove.
    In place of legends, four small dots mark the button sectors (up, right, down, left), and a 50-degree wedge of the
    ring is tinted where a finger would be (the device view's touch state), upper right, as if turning clockwise."""
    r_in, r_c = R * 0.44, R * 0.38
    dot_r, dot_at = R * 0.05, (R + r_in) / 2
    ring_d = (f"M{cx - R} {cy}a{R} {R} 0 1 0 {2 * R} 0a{R} {R} 0 1 0 {-2 * R} 0z"
              f"M{cx - r_in} {cy}a{r_in} {r_in} 0 1 0 {2 * r_in} 0a{r_in} {r_in} 0 1 0 {-2 * r_in} 0z")
    dots = [(cx + math.cos(math.radians(a)) * dot_at, cy + math.sin(math.radians(a)) * dot_at) for a in (-90, 0, 90, 180)]
    if mono:
        cut = "".join(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{dot_r:.1f}" fill="#000"/>' for x, y in dots)
        return (f'<defs><mask id="m{uid}"><rect width="100%" height="100%" fill="#fff"/>{cut}</mask></defs>'
                f'<path d="{ring_d}" fill-rule="evenodd" fill="{ring}" mask="url(#m{uid})"/><circle cx="{cx}" cy="{cy}" r="{r_c:.1f}" fill="{centre}"/>')
    a0, a1 = math.radians(-85), math.radians(-35)
    p = lambda r, a: (cx + math.cos(a) * r, cy + math.sin(a) * r)
    (x1, y1), (x2, y2), (x3, y3), (x4, y4) = p(R, a0), p(R, a1), p(r_in, a1), p(r_in, a0)
    wedge_d = f"M{x1:.1f} {y1:.1f}A{R} {R} 0 0 1 {x2:.1f} {y2:.1f}L{x3:.1f} {y3:.1f}A{r_in:.1f} {r_in:.1f} 0 0 0 {x4:.1f} {y4:.1f}z"
    defs = (f'<linearGradient id="ring{uid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{ring}"/><stop offset="1" stop-color="{ring_lo}"/></linearGradient>'
            f'<linearGradient id="ctr{uid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{centre}"/><stop offset="1" stop-color="{centre_lo}"/></linearGradient>')
    marks = "".join(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{dot_r:.1f}" fill="{mark}"/>' for x, y in dots)
    return (f'<defs>{defs}</defs><path d="{ring_d}" fill-rule="evenodd" fill="url(#ring{uid})"/>'
            f'<path d="{wedge_d}" fill="{wedge or "rgba(0,0,0,.08)"}"/>{marks}'
            f'<circle cx="{cx}" cy="{cy}" r="{r_in:.1f}" fill="{groove}"/><circle cx="{cx}" cy="{cy}" r="{r_c:.1f}" fill="url(#ctr{uid})"/>')

def icon_wheel(variant="light", uid="w"):
    if variant == "light":
        bg = f'<defs><linearGradient id="bg{uid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{HARB["body"]}"/><stop offset="1" stop-color="{HARB["body_lo"]}"/></linearGradient></defs><rect width="1024" height="1024" fill="url(#bg{uid})"/>'
        w = wheel_shapes(512, 512, 340, HARB["wheel"], HARB["wheel_lo"], HARB["centre"], HARB["centre_lo"], HARB["groove"], "#7d8a96", uid, wedge="rgba(43,123,180,.22)")
    elif variant == "dark":
        bg = f'<defs><linearGradient id="bg{uid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#1b1c20"/><stop offset="1" stop-color="#08090a"/></linearGradient></defs><rect width="1024" height="1024" fill="url(#bg{uid})"/>'
        w = wheel_shapes(512, 512, 340, "#4d93d6", "#2c6fae", "#15161a", "#0b0c0e", "#050506", "#cfe3f6", uid, wedge="rgba(255,255,255,.16)")
    else:  # tinted: grayscale on black; iOS applies the tint
        bg = '<rect width="1024" height="1024" fill="#000"/>'
        w = wheel_shapes(512, 512, 340, "#ffffff", "#d0d0d0", "#3a3a3a", "#262626", "#000000", "#6a6a6a", uid, wedge="rgba(0,0,0,.14)")
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">{bg}{w}</svg>'

def montserrat_path(text, size, x, y, tracking=0):
    """Montserrat Medium outlines (SIL OFL 1.1) as one SVG path, baseline at y."""
    from fontTools.ttLib import TTFont
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    f = TTFont(MONTSERRAT)
    gs, cm, upm = f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm
    k = size / upm
    out, pen = "", None
    for ch in text:
        g = cm[ord(ch)]
        pen = SVGPathPen(gs)
        gs[g].draw(TransformPen(pen, (k, 0, 0, -k, x, y)))
        out += pen.getCommands()
        x += f["hmtx"][g][0] * k + tracking
    return out

def text_width(text, size, tracking=0):
    from fontTools.ttLib import TTFont
    f = TTFont(MONTSERRAT)
    cm, upm = f.getBestCmap(), f["head"].unitsPerEm
    return sum(f["hmtx"][cm[ord(c)]][0] for c in text) * size / upm + tracking * (len(text) - 1)

def icon_monogram():
    size, tr = 520, -18
    w = text_width("nP", size, tr)
    d = montserrat_path("nP", size, 512 - w / 2, 512 + size * 0.36, tr)
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024"><defs><linearGradient id="bgm" x1="0" y1="0" x2="0" y2="1">'
            f'<stop offset="0" stop-color="#f6f5f2"/><stop offset="1" stop-color="#dedbd4"/></linearGradient></defs><rect width="1024" height="1024" fill="url(#bgm)"/>'
            f'<path d="{d}" fill="{HARB["body"]}"/></svg>')

def icon_trace():
    # an "n" as a PCB trace: 45-degree chamfers, round pads at both ends and a via; copper on solder mask
    t = 64
    d = "M332 760V430L432 330H592L692 430V760"
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024"><defs><linearGradient id="bgt" x1="0" y1="0" x2="1" y2="1">'
            '<stop offset="0" stop-color="#13392f"/><stop offset="1" stop-color="#0a231c"/></linearGradient></defs><rect width="1024" height="1024" fill="url(#bgt)"/>'
            '<path d="M120 250H260M120 330H200M764 694H904M824 774H904" stroke="#1f5446" stroke-width="22" stroke-linecap="round"/>'
            f'<path d="{d}" fill="none" stroke="#d68a4c" stroke-width="{t}" stroke-linejoin="round" stroke-linecap="round"/>'
            '<circle cx="332" cy="770" r="74" fill="#d68a4c"/><circle cx="332" cy="770" r="30" fill="#0d2b23"/>'
            '<circle cx="692" cy="770" r="74" fill="#d68a4c"/><circle cx="692" cy="770" r="30" fill="#0d2b23"/>'
            '<circle cx="512" cy="330" r="48" fill="#e7a86f"/><circle cx="512" cy="330" r="18" fill="#0d2b23"/></svg>')

def android_layers():
    # 108 dp canvas drawn at 432 px (4x); foreground kept inside the 66 dp safe circle (264 px), visible area 72 dp
    bg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 108 108" width="432" height="432"><defs><linearGradient id="abg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{HARB["body"]}"/><stop offset="1" stop-color="{HARB["body_lo"]}"/></linearGradient></defs><rect width="108" height="108" fill="url(#abg)"/></svg>'
    fg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 108 108" width="432" height="432">{wheel_shapes(54, 54, 31, HARB["wheel"], HARB["wheel_lo"], HARB["centre"], HARB["centre_lo"], HARB["groove"], "#7d8a96", "af", wedge="rgba(43,123,180,.22)")}</svg>'
    mono = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 108 108" width="432" height="432">{wheel_shapes(54, 54, 31, "#000000", "#000000", "#000000", "#000000", "#000000", "#000000", "am", mono=True)}</svg>'
    return bg, fg, mono

def inner(svg):
    """An SVG's children, for nesting at another size."""
    return svg[svg.index(">") + 1: svg.rindex("</svg>")]

def as_img(svg, size, radius="22.37%", extra=""):
    v = svg[svg.index('viewBox="') + 9:].split('"')[0]
    return (f'<svg role="img" aria-label="app icon" viewBox="{v}" width="{size}" height="{size}" style="display:block; border-radius: {radius}; {extra}">'
            f'{inner(svg)}</svg>')

def ios_home(icons, dark=False):
    # 393x852 iPhone, icons 60 pt, 4 columns; neutral placeholder tiles, no real apps
    wall = "linear-gradient(160deg, #3d5a73, #1d2a36)" if not dark else "linear-gradient(160deg, #202226, #08090a)"
    tiles = []
    labels = ["neuPod A", "neuPod B", "neuPod C"]
    for i in range(24):
        if i < 3:
            t = as_img(icons[i], 60, extra="")
            lab = labels[i]
        else:
            t = f'<div style="width: 60px; height: 60px; border-radius: 13.4px; background: rgba(255,255,255,{.18 if i % 3 else .28})"></div>'
            lab = ""
        tiles.append(f'<div style="display: flex; flex-direction: column; align-items: center; gap: 5px; width: 72px">{t}<div style="height: 14px; font-size: 11px; color: #fff; font-family: system-ui">{lab}</div></div>')
    return (f'<div style="width: 393px; height: 852px; border-radius: 48px; background: {wall}; box-sizing: border-box; padding: 76px 24px 0; '
            f'display: grid; grid-template-columns: repeat(4, 72px); justify-content: space-between; row-gap: 22px; align-content: start; box-shadow: 0 0 0 10px #0b0c0e">{"".join(tiles)}</div>')

def android_home(fgbg_list):
    wall = "linear-gradient(170deg, #2a2f3a, #111318)"
    tiles = []
    for i in range(20):
        if i < 3:
            bgs, fgs = fgbg_list[i]
            # adaptive: 108 dp layers, shown 72 dp of them under a circle mask at 52 dp
            t = (f'<svg role="img" aria-label="app icon" viewBox="18 18 72 72" width="52" height="52" style="display:block; border-radius: 50%">'
                 f'{inner(bgs)}{inner(fgs)}</svg>')
            lab = ["A", "B", "C"][i]
        else:
            t = f'<div style="width: 52px; height: 52px; border-radius: 50%; background: rgba(255,255,255,{.16 if i % 3 else .26})"></div>'
            lab = ""
        tiles.append(f'<div style="display: flex; flex-direction: column; align-items: center; gap: 6px; width: 76px">{t}<div style="height: 14px; font-size: 12px; color: #fff; font-family: system-ui">{("neuPod " + lab) if lab else ""}</div></div>')
    return (f'<div style="width: 412px; height: 852px; border-radius: 36px; background: {wall}; box-sizing: border-box; padding: 90px 18px 0; '
            f'display: grid; grid-template-columns: repeat(4, 76px); justify-content: space-between; row-gap: 26px; align-content: start; box-shadow: 0 0 0 10px #0b0c0e">{"".join(tiles)}</div>')

def to_android(svg1024, uid):
    """Wrap a 1024 master as adaptive layers for the comparison: background = the master, foreground empty."""
    bg = f'<svg viewBox="0 0 108 108"><g transform="translate(18 18) scale({72 / 1024})">{inner(svg1024)}</g></svg>'
    return bg, '<svg viewBox="0 0 108 108"></svg>'

def directions_sheet():
    A, B, C = icon_monogram(), icon_wheel("light", "dir"), icon_trace()
    ab, af = to_android(A, "a"); cb, cf = to_android(C, "c")
    bb, bf, _ = android_layers()
    W, H = 1720, 1180
    def card(title, svg, lines, rec=False):
        return f"""<div style="width: 330px; display: flex; flex-direction: column; gap: 14px">
  {as_img(svg, 240)}
  <div style="font-size: 22px; color: #181818">{title}{' <span style="font-size: 13px; color: #fff; background: #1858c8; border-radius: 6px; padding: 3px 8px; vertical-align: 3px">Recommended</span>' if rec else ''}</div>
  <div style="font-size: 14px; line-height: 20px; color: #4a4a4a">{lines}</div>
  <div style="display: flex; gap: 14px; align-items: flex-end">{as_img(svg, 60)}{as_img(svg, 40)}{as_img(svg, 29)}{as_img(svg, 20)}</div>
</div>"""
    body = f"""<div style="padding: 40px 48px; display: flex; flex-direction: column; gap: 28px">
  <div style="font-size: 32px; color: #181818">App icon: three directions</div>
  <div style="display: flex; gap: 48px">
    <div style="display: flex; flex-direction: column; gap: 40px">
      <div style="display: flex; gap: 36px">
        {card("A · Monogram", A, "nP from the wordmark, Montserrat Medium outlines, Harbour blue on warm white. Safest on marks; reads as a brand, not as music.")}
        {card("B · Turning wheel", B, "The device view's wheel in the Harbour starter look. Dots mark the four button sectors in place of legends; a tinted wedge is a finger turning it. No screen, no MENU, no Apple glyphs.", True)}
      </div>
      {card("C · Trace", C, "An n routed as a PCB trace with pads and a via: open hardware, the clear-case story. Distinctive, but says little about music.")}
    </div>
    <div style="display: flex; gap: 36px; align-items: flex-start">
      <div style="display: flex; flex-direction: column; gap: 10px"><div style="font-size: 14px; color: #606060">iPhone, real size (60 pt)</div>{ios_home([A, B, C])}</div>
      <div style="display: flex; flex-direction: column; gap: 10px"><div style="font-size: 14px; color: #606060">Pixel, real size (52 dp, circle mask)</div>{android_home([(ab, af), (bb, bf), (cb, cf)])}</div>
    </div>
  </div>
</div>"""
    return page("App icon directions", W, H, body, "#f3f4f6"), W, H

def ios_sheet():
    L, D, T = icon_wheel("light", "il"), icon_wheel("dark", "id"), icon_wheel("tinted", "it")
    W, H = 1500, 660
    def col(title, svg, note, bg):
        return f"""<div style="display: flex; flex-direction: column; gap: 12px; width: 420px">
  <div style="font-size: 20px; color: #181818">{title}</div>
  <div style="padding: 30px; background: {bg}; border-radius: 20px; display: flex; flex-direction: column; gap: 20px; align-items: center">{as_img(svg, 300)}<div style="display: flex; gap: 16px; align-items: flex-end">{as_img(svg, 60)}{as_img(svg, 40)}{as_img(svg, 29)}</div></div>
  <div style="font-size: 13px; line-height: 19px; color: #4a4a4a">{note}</div>
</div>"""
    body = f"""<div style="padding: 40px 48px; display: flex; flex-direction: column; gap: 24px">
  <div style="display: flex; justify-content: space-between; align-items: baseline"><div style="font-size: 32px; color: #181818">iOS AppIcon, 1024x1024: direction B</div><div style="font-size: 14px; color: #606060">Masters are full-bleed squares; iOS applies the corner mask. Shown here with it.</div></div>
  <div style="display: flex; gap: 40px">
    {col("Light: app-icon-master.svg", L, "Harbour starter look: blue body, warm white ring, blue centre in its groove. Dots and wedge in muted slate.", "#e9ebee")}
    {col("Dark: app-icon-master-dark.svg", D, "Black body finish (device view). Ring in a lighter Harbour blue so it holds on black; centre as the black wheel's.", "#1c1c1e")}
    {col("Tinted: app-icon-master-tinted.svg", T, "Grayscale on black; iOS tints it. Ring white to light grey, centre dark grey: the shape survives any tint.", "#1c1c1e")}
  </div>
</div>"""
    return page("iOS app icon variants", W, H, body, "#f3f4f6"), W, H

def android_sheet():
    bg, fg, mono = android_layers()
    W, H = 1500, 660
    def layer(title, svg, note, checker=False):
        chk = "background: repeating-conic-gradient(#d9dbe0 0 25%, #f3f4f6 0 50%) 0 0 / 20px 20px" if checker else "background: #e9ebee"
        return f"""<div style="display: flex; flex-direction: column; gap: 10px; width: 216px">
  <div style="font-size: 16px; color: #181818">{title}</div>
  <div style="position: relative; width: 216px; height: 216px; {chk}">
    <svg aria-hidden="true" viewBox="0 0 108 108" width="216" height="216" style="display:block; position: absolute; inset: 0">{inner(svg)}</svg>
    <svg aria-hidden="true" viewBox="0 0 108 108" width="216" height="216" style="display:block; position: absolute; inset: 0"><rect x="18" y="18" width="72" height="72" fill="none" stroke="#e0457b" stroke-width=".6" stroke-dasharray="2 1.5"/><circle cx="54" cy="54" r="33" fill="none" stroke="#e0457b" stroke-width=".6"/></svg>
  </div>
  <div style="font-size: 12px; line-height: 17px; color: #4a4a4a">{note}</div>
</div>"""
    def masked(name, clip):
        return f"""<div style="display: flex; flex-direction: column; gap: 10px; align-items: center">
  <svg role="img" aria-label="app icon, {name} mask" viewBox="18 18 72 72" width="144" height="144" style="display:block; {clip}">{inner(bg)}{inner(fg)}</svg>
  <div style="font-size: 13px; color: #606060">{name}</div>
</div>"""
    themed = (f'<div style="display: flex; flex-direction: column; gap: 10px; align-items: center"><svg role="img" aria-label="themed icon" viewBox="18 18 72 72" width="144" height="144" style="display:block; border-radius: 50%; background: #d3e3fd">'
              f'<g style="filter: none">{inner(mono).replace("#000000", "#0b3a73")}</g></svg><div style="font-size: 13px; color: #606060">Themed (monochrome, system tint)</div></div>')
    body = f"""<div style="padding: 40px 48px; display: flex; flex-direction: column; gap: 26px">
  <div style="display: flex; justify-content: space-between; align-items: baseline"><div style="font-size: 32px; color: #181818">Android adaptive icon: direction B</div><div style="font-size: 14px; color: #606060">108 dp canvas; dashed square = 72 dp visible; circle = 66 dp safe zone</div></div>
  <div style="display: flex; gap: 32px">
    {layer("android-background.svg", bg, "Harbour body gradient, full bleed.")}
    {layer("android-foreground.svg", fg, "Wheel, 62 dp across, inside the 66 dp safe circle; transparent around it.", True)}
    {layer("android-monochrome.svg", mono, "One colour, alpha only: ring with the four dots cut out, groove open, centre solid.", True)}
  </div>
  <div style="display: flex; gap: 48px; align-items: flex-end">
    {masked("Circle", "border-radius: 50%")}
    {masked("Squircle", "border-radius: 38%")}
    {masked("Rounded square", "border-radius: 18%")}
    {themed}
  </div>
</div>"""
    return page("Android adaptive icon", W, H, body, "#f3f4f6"), W, H

def write_svgs():
    from fontTools.ttLib import TTFont  # noqa: F401  (keeps the import check early)
    L, D, T = icon_wheel("light", "l"), icon_wheel("dark", "d"), icon_wheel("tinted", "t")
    bg, fg, mono = android_layers()
    for n, s in (("app-icon-master", L), ("app-icon-master-dark", D), ("app-icon-master-tinted", T),
                 ("android-background", bg), ("android-foreground", fg), ("android-monochrome", mono),
                 ("app-icon-direction-a-monogram", icon_monogram()), ("app-icon-direction-c-trace", icon_trace())):
        with open(os.path.join(SVG_DIR, n + ".svg"), "w") as f:
            f.write(s + "\n")

def main():
    os.makedirs(OUT, exist_ok=True); os.makedirs(BUILD, exist_ok=True)
    if not os.path.exists(MONTSERRAT):
        sys.exit("Montserrat-Medium.ttf not found: git submodule update --init lib/lvgl, or set MONTSERRAT=/path/to/it")
    bm = rasterise()
    write_svgs()
    pages = {f"glyphs-1bit-{k}": glyph_sheet(bm, k) for k in STYLES}
    pages["status-bar-sheet"] = status_sheet(bm)
    pages["app-icon-directions"] = directions_sheet()
    pages["app-icon-ios"] = ios_sheet()
    pages["app-icon-android"] = android_sheet()
    for n, (html, w, h) in pages.items():
        with open(os.path.join(OUT, n + ".html"), "w") as f:
            f.write(html)
    with open(os.path.join(BUILD, "contrast.json"), "w") as f:
        json.dump(contrast_table(), f, indent=1)
    if "--no-png" not in sys.argv:
        chrome = os.environ.get("CHROME", "google-chrome")
        for n, (_, w, h) in pages.items():
            subprocess.run([chrome, "--headless=new", "--no-sandbox", "--hide-scrollbars", "--force-device-scale-factor=1",
                            f"--window-size={w},{h}", f"--screenshot={os.path.join(OUT, n + '.png')}",
                            "file://" + os.path.join(OUT, n + ".html")], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for r in contrast_table():
        print("%-28s %-6s %s on %s: %.2f" % r)

if __name__ == "__main__":
    main()
