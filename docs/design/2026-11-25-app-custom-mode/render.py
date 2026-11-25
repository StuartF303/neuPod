#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Render the Custom mode mock-ups: standalone HTML and PNG in mockups/.

    python3 docs/design/2026-11-25-app-custom-mode/render.py [--no-png]

Extends the device view's generator (../2026-11-21-app-device-view/render.py): the geometry, the
600x450 screens and the page writer are imported from it, unchanged. This file adds the custom
paint: the three parts' colours, the contrast rule, the board picture and its layering. The colour
arithmetic here (hsv, shade, paint) is the reference for the rule in README.md section 4.
PNGs need Chrome (`google-chrome` or CHROME=/path) and the Montserrat font installed.

The board pictures are PLACEHOLDERS drawn by this script. They are not neuPod boards.
"""
import importlib.util, math, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("device_view", os.path.join(HERE, "..", "2026-11-21-app-device-view", "render.py"))
dv = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(dv)
dv.OUT = os.path.join(HERE, "mockups")
FONT = dv.FONT

# ---- colour arithmetic (README section 4) --------------------------------------------------------
WHITE, BLACK = (255, 255, 255), (0, 0, 0)
BODY_MAX, WHEEL_MAX, CENTRE_MAX = 85, 60, 60      # most transparency, per cent
AMP = {"body": 0.10, "wheel": 0.06, "centre": 0.06}  # how far the gradient's ends sit from the colour

def hsv(h, s, v):
    """Hue 0-359, saturation and brightness 0-100, to (r, g, b)."""
    s, v = s / 100, v / 100
    def f(n):
        k = (n + h / 60) % 6
        return round(255 * (v - v * s * max(0, min(k, 4 - k, 1))))
    return (f(5), f(3), f(1))

def mix(c, t, a):
    return tuple(round(x + (y - x) * a) for x, y in zip(c, t))

def lum(c):
    def lin(x):
        x /= 255
        return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4
    return 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2])

def cr(a, b):
    x, y = lum(a), lum(b)
    return (max(x, y) + 0.05) / (min(x, y) + 0.05)

def hexs(c):
    return "#%02x%02x%02x" % tuple(c)

def rgba(c, a=1.0):
    return f"rgba({c[0]},{c[1]},{c[2]},{a:.3g})"

def paint(p, part, grounds=None, need=4.5):
    """The contrast rule for one part. p = (hue, saturation, brightness, transparency).
    grounds: the darkest and lightest colour of what shows through under the legends (wheel only).
    Returns the colours to paint with, the ink, the worst contrast, and the transparency kept."""
    amp, top = AMP[part], {"body": BODY_MAX, "wheel": WHEEL_MAX, "centre": CENTRE_MAX}[part]
    b = hsv(*p[:3])
    t, flat = min(p[3], top), False
    def run():
        stops = [b, b, b] if flat else [mix(b, WHITE, amp), b, mix(b, BLACK, amp)]
        under = [mix(s, g, t / 100) for g in grounds for s in stops] if (t and grounds) else stops
        wk, ww = min(cr(BLACK, u) for u in under), min(cr(WHITE, u) for u in under)
        return stops, (BLACK if wk >= ww else WHITE), max(wk, ww)
    stops, ink, worst = run()
    if worst < need:
        flat = True
        stops, ink, worst = run()
    while worst < need and t > 0:
        t -= 5
        stops, ink, worst = run()
    return dict(base=b, stops=stops, ink=ink, worst=worst, t=t, flat=flat, asked=min(p[3], top))

def increase_contrast(p, part):
    """Increase Contrast: opaque, and brightness moved the least that makes the legend reach 7:1."""
    for d in range(0, 101):
        for v in (p[2] - d, p[2] + d):
            if 0 <= v <= 100:
                q = paint((p[0], p[1], v, 0), part, need=7.0)
                if q["worst"] >= 7.0:
                    return (p[0], p[1], v, 0)
    return (p[0], p[1], 0, 0)

# ---- looks and boards ----------------------------------------------------------------------------
LOOKS = {  # part: (hue, saturation, brightness, transparency)
    "harbour": dict(name="Harbour", body=(205, 62, 44, 0), wheel=(40, 8, 94, 0), centre=(205, 62, 36, 0)),
    "seaglass": dict(name="Sea glass", body=(165, 35, 70, 55), wheel=(165, 10, 96, 0), centre=(165, 40, 60, 0)),
    "clear": dict(name="Clear case", body=(200, 6, 92, 85), wheel=(200, 4, 96, 60), centre=(200, 6, 90, 60)),
    "pale": dict(name="Chalk", body=(48, 10, 98, 0), wheel=(48, 4, 100, 0), centre=(48, 14, 94, 0)),
    "hot": dict(name="Flare", body=(338, 88, 86, 0), wheel=(338, 70, 30, 0), centre=(20, 90, 95, 0)),
    "default-light": dict(name="Look 4", body=(218, 4, 88, 0), wheel=(220, 2, 95, 0), centre=(220, 4, 87, 0)),
    "default-dark": dict(name="Look 4", body=(225, 20, 8, 0), wheel=(223, 12, 14, 0), centre=(228, 19, 6, 0)),
}
BOARDS = {  # placeholders. frost_dark / frost_light: the extremes of the blurred copy, per board
    "main-top": dict(name="Main board, top", ground=(18, 53, 36), trace=(214, 178, 72), frost_dark=(20, 48, 34), frost_light=(52, 84, 62)),
    "main-bottom": dict(name="Main board, bottom", ground=(16, 26, 46), trace=(150, 180, 230), frost_dark=(18, 26, 44), frost_light=(48, 60, 86)),
    "copper": dict(name="Copper only", ground=(42, 28, 16), trace=(224, 140, 70), frost_dark=(44, 30, 18), frost_light=(84, 60, 38)),
}
ASSET_W, ASSET_H, ANCHOR_X, ANCHOR_Y = 1200, 3120, 600, 2040   # the board picture and where the wheel's centre sits on it

def board_svg(board):
    """A placeholder board picture, 1200 x 3120, as SVG. Drawn here; labelled; not a neuPod board."""
    b = BOARDS[board]
    rnd = random.Random(board)
    tr = "rgb(%d,%d,%d)" % b["trace"]
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {ASSET_W} {ASSET_H}" preserveAspectRatio="none" aria-hidden="true">',
           f'<rect width="{ASSET_W}" height="{ASSET_H}" fill="{hexs(b["ground"])}"/>']
    for _ in range(90):  # traces: runs with 45 degree bends
        x, y = rnd.randrange(0, ASSET_W, 30), rnd.randrange(0, ASSET_H, 30)
        d = f"M{x} {y}"
        for _ in range(rnd.randint(2, 4)):
            run = rnd.choice([120, 180, 240, 330])
            k = rnd.choice([(1, 0), (0, 1), (1, 1), (1, -1), (-1, 0), (0, -1)])
            x, y = x + k[0] * run, y + k[1] * run
            d += f"L{x} {y}"
        out.append(f'<path d="{d}" fill="none" stroke="{tr}" stroke-opacity=".55" stroke-width="{rnd.choice([5, 5, 8])}" stroke-linejoin="round"/>')
        out.append(f'<circle cx="{x}" cy="{y}" r="11" fill="{tr}" fill-opacity=".8"/>')
    for i in range(22):  # parts
        w, h = rnd.choice([(150, 150), (210, 120), (90, 60), (120, 240), (60, 60)])
        x, y = rnd.randrange(30, ASSET_W - w, 30), rnd.randrange(900, ASSET_H - h, 30)
        out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="#0c0d10" stroke="#ffffff" stroke-opacity=".3" stroke-width="2"/>'
                   f'<text x="{x + w / 2}" y="{y + h / 2 + 9}" text-anchor="middle" font-family="{FONT}" font-size="26" fill="#ffffff" fill-opacity=".6">U{i + 1}</text>')
    for a in range(0, 360, 20):  # a ring of pads round the anchor, so the alignment can be seen
        t = math.radians(a)
        out.append(f'<circle cx="{ANCHOR_X + 300 * math.cos(t):.0f}" cy="{ANCHOR_Y + 300 * math.sin(t):.0f}" r="13" fill="{tr}"/>')
    for y in range(240, ASSET_H, 420):
        out.append(f'<text x="600" y="{y}" text-anchor="middle" transform="rotate(-12 600 {y})" font-family="{FONT}" font-size="40" letter-spacing="8" fill="#ffffff" fill-opacity=".8">PLACEHOLDER · {b["name"].upper()} · NOT A NEUPOD BOARD</text>')
    out.append("</svg>")
    return "".join(out)

def frost_css(board):
    """The blurred copy under the wheel and the centre: soft shapes between the board's two frost extremes."""
    b = BOARDS[board]
    d, l = rgba(b["frost_dark"]), rgba(b["frost_light"])
    return f"radial-gradient(circle at 28% 22%,{l},transparent 46%),radial-gradient(circle at 74% 70%,{l},transparent 38%),{d}"

# ---- the device, with custom paint ---------------------------------------------------------------
def resolve(look, board, a11y=None):
    L = dict(LOOKS[look]) if isinstance(look, str) else dict(look)
    if a11y == "reduce-transparency":
        for k in ("body", "wheel", "centre"):
            L[k] = L[k][:3] + (0,)
    if a11y == "increase-contrast":
        for k in ("body", "wheel", "centre"):
            L[k] = increase_contrast(L[k], k)
    B = BOARDS[board]
    return dict(look=L, body=paint(L["body"], "body"), wheel=paint(L["wheel"], "wheel", [B["frost_dark"], B["frost_light"]]),
                centre=paint(L["centre"], "centre"), edge=0.6 if a11y == "increase-contrast" else 0.3)

_uid = [0]

def wheel_svg(D, r, state="idle", angle=None):
    """The click wheel in custom paint. Same shapes as the device view's wheel_svg; fills carry the parts' opacity."""
    _uid[0] += 1
    u = f"w{_uid[0]}"   # gradient ids must be unique when a page holds several wheels
    wh, ce, R, c = r["wheel"], r["centre"], 100, dv.CENTRE_D * 100
    ink, ow, oc = wh["ink"], 1 - wh["t"] / 100, 1 - ce["t"] / 100
    tint = (0.13 if ink == BLACK else 0.14) + 0.10 * wh["t"] / 100
    touch = (0.07 if ink == BLACK else 0.08) + 0.06 * wh["t"] / 100
    def pol(rr, a):
        t = math.radians(a - 90)
        return 100 + rr * math.cos(t), 100 + rr * math.sin(t)
    def wedge(a0, a1, fill):
        x0, y0 = pol(99, a0); x1, y1 = pol(99, a1); x2, y2 = pol(c + 4, a1); x3, y3 = pol(c + 4, a0)
        return f'<path d="M{x0:.2f} {y0:.2f}A99 99 0 0 1 {x1:.2f} {y1:.2f}L{x2:.2f} {y2:.2f}A{c+4} {c+4} 0 0 0 {x3:.2f} {y3:.2f}Z" fill="{fill}"/>'
    extra = ""
    if state == "menu":
        extra += wedge(-45, 45, rgba(ink, tint))
    if state == "ring" and angle is not None:
        extra += wedge(angle - 25, angle + 25, rgba(ink, touch))
    ctint = (0.13 if ce["ink"] == BLACK else 0.14) + 0.10 * ce["t"] / 100
    cpress = f'<circle cx="100" cy="100" r="{c}" fill="{rgba(ce["ink"], ctint)}"/>' if state == "centre" else ""
    op = ".35" if state == "disabled" else "1"
    g = hexs(ink)
    return (f'<svg width="{D:.1f}" height="{D:.1f}" viewBox="0 0 200 200" role="img" aria-label="Click wheel, {state}">'
            f'<defs><linearGradient id="{u}rg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{hexs(wh["stops"][0])}" stop-opacity="{ow:.2f}"/><stop offset="1" stop-color="{hexs(wh["stops"][2])}" stop-opacity="{ow:.2f}"/></linearGradient>'
            f'<linearGradient id="{u}cg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{hexs(ce["stops"][0])}" stop-opacity="{oc:.2f}"/><stop offset="1" stop-color="{hexs(ce["stops"][2])}" stop-opacity="{oc:.2f}"/></linearGradient>'
            f'<mask id="{u}ring"><rect width="200" height="200" fill="#fff"/><circle cx="100" cy="100" r="{c}" fill="#000"/></mask></defs>'
            f'<g mask="url(#{u}ring)"><circle cx="100" cy="100" r="99" fill="url(#{u}rg)"/>{extra}<circle cx="100" cy="100" r="{c+2}" fill="none" stroke="{rgba(ink, 0.22)}" stroke-width="4"/></g>'
            f'<circle cx="100" cy="100" r="99" fill="none" stroke="{rgba(ink, r["edge"])}" stroke-width="1"/>'
            f'<circle cx="100" cy="100" r="{c}" fill="url(#{u}cg)" stroke="{rgba(ce["ink"], r["edge"])}" stroke-width=".8"/>{cpress}'
            f'<g opacity="{op}" fill="{g}"><text x="100" y="27" text-anchor="middle" font-family="{FONT}" font-weight="500" font-size="11.5" letter-spacing="1.2">MENU</text>'
            f'<g transform="translate(160 91) scale(.75)"><polygon points="5 4 15 12 5 20 5 4"/><rect x="17.5" y="4" width="3" height="16" rx="1"/></g>'
            f'<g transform="translate(22 91) scale(.75)"><polygon points="19 20 9 12 19 4 19 20"/><rect x="3.5" y="4" width="3" height="16" rx="1"/></g>'
            f'<g transform="translate(88 166) scale(.72)"><polygon points="2 4 14 12 2 20 2 4"/><rect x="18" y="4" width="4" height="16" rx="1"/><rect x="25" y="4" width="4" height="16" rx="1"/></g></g></svg>')

def device(inner, look, board="main-top", phone="393", wheel="idle", angle=None, a11y=None, extra=""):
    """One phone, fit A, in custom paint. Layers bottom to top as README section 5 lists them."""
    g = dv.geom(phone, "A")
    r = resolve(look, board, a11y)
    w, h, bz, D, cy = g["w"], g["h"], g["bz"], g["D"], g["cy"]
    bd, wh, ce = r["body"], r["wheel"], r["centre"]
    ob = 1 - bd["t"] / 100
    k = w / ASSET_W
    frame_top = g["scr_top"] - bz
    layers = ""
    if bd["t"]:  # 1 and 2: the board's ground colour, then the sharp picture, anchored on the wheel's centre
        layers += (f'<div style="position:absolute;inset:0;background:{hexs(BOARDS[board]["ground"])}"></div>'
                   f'<div style="position:absolute;left:{w / 2 - ANCHOR_X * k:.1f}px;top:{cy - ANCHOR_Y * k:.1f}px;width:{ASSET_W * k:.1f}px;height:{ASSET_H * k:.1f}px">{board_svg(board).replace("<svg ", "<svg width=\"100%\" height=\"100%\" ", 1)}</div>')
    s = bd["stops"]  # 3: the body colour at its opacity
    layers += f'<div style="position:absolute;inset:0;background:linear-gradient(180deg,{rgba(s[0], ob)} 0%,{rgba(s[1], ob)} 45%,{rgba(s[2], ob)} 100%)"></div>'
    if bd["t"]:      # 4: the solid top cap, fading out over 0.06 W below the frame's top edge
        layers += f'<div style="position:absolute;left:0;top:0;width:{w}px;height:{frame_top + 0.06 * w:.1f}px;background:linear-gradient(180deg,{rgba(s[0])} 0,{rgba(s[0])} {frame_top:.1f}px,{rgba(s[0], 0)} 100%)"></div>'
    status_ink = "#000000" if lum(s[0]) >= 0.179 else "#ffffff"
    sy = (g["st"] - 20) / 2 + 3 if g["island"] else 2
    statusbar = (f'<div aria-hidden="true" style="position:absolute;left:0;top:{sy:.0f}px;width:{w}px;height:20px;display:flex;justify-content:space-between;align-items:center;box-sizing:border-box;padding:0 {28 if g["island"] else 12}px;font:600 15px system-ui,sans-serif;color:{status_ink}">'
                 f'<span>13:56</span><span style="width:24px;height:11px;border:1.5px solid {status_ink};border-radius:3.5px;box-sizing:border-box;padding:1px"><span style="display:block;width:70%;height:100%;background:{status_ink};border-radius:1.5px"></span></span></div>')
    isl = f'<div aria-hidden="true" style="position:absolute;left:{(w-124)/2:.0f}px;top:12px;width:124px;height:36px;border-radius:18px;background:#000"></div>' if g["island"] else ""
    home = f'<div aria-hidden="true" style="position:absolute;left:{(w-136)/2:.0f}px;bottom:8px;width:136px;height:5px;border-radius:3px;background:{hexs(bd["ink"])};opacity:.55"></div>' if g["sb"] else ""
    frame = (f'<div style="position:absolute;left:{g["scr_x"]-bz:.1f}px;top:{frame_top:.1f}px;width:{g["sw"]+2*bz:.1f}px;height:{g["sh"]+2*bz:.1f}px;border-radius:{0.02*w:.1f}px;background:#050608;box-shadow:inset 0 0 0 1px rgba(255,255,255,.08),0 0 0 1px {rgba(bd["ink"], 0.16)}"></div>'
             f'<div style="position:absolute;left:{g["scr_x"]:.1f}px;top:{g["scr_top"]:.1f}px;width:{g["sw"]:.1f}px;height:{g["sh"]:.1f}px;overflow:hidden;border-radius:{0.008*w:.1f}px">'
             f'<div style="width:600px;height:450px;transform:scale({g["scale"]:.5f});transform-origin:0 0">{inner}</div></div>')
    gap = cy - D / 2 - g["scr_bot"] - bz
    fs = 0.036 * w
    badge = f"background:{rgba(bd['base'])};" if bd["t"] else ""
    mark = (f'<div aria-hidden="true" style="position:absolute;left:0;width:{w}px;top:{g["scr_bot"] + bz + gap / 2 - fs * 0.6 - 0.012 * w:.1f}px;text-align:center">'
            f'<span style="display:inline-block;padding:{0.012 * w:.1f}px {0.03 * w:.1f}px;border-radius:{fs:.1f}px;{badge}font:500 {fs:.1f}px/1.2 {FONT};letter-spacing:.04em;color:{hexs(bd["ink"])}">neuPod</span></div>'
            if gap > 44 else "")
    c = dv.CENTRE_D * D
    frost = ""
    if wh["t"]:  # 6: the blurred copy under the ring
        frost += f'<div style="position:absolute;left:{(w - D) / 2:.1f}px;top:{cy - D / 2:.1f}px;width:{D:.1f}px;height:{D:.1f}px;border-radius:50%;background:{frost_css(board)}"></div>'
    elif ce["t"] or bd["t"]:
        pass
    if ce["t"]:  # and under the centre button
        frost += f'<div style="position:absolute;left:{(w - c) / 2:.1f}px;top:{cy - c / 2:.1f}px;width:{c:.1f}px;height:{c:.1f}px;border-radius:50%;background:{frost_css(board)}"></div>'
    wl = f'<div style="position:absolute;left:{(w-D)/2:.1f}px;top:{cy-D/2:.1f}px;width:{D:.1f}px;height:{D:.1f}px;filter:drop-shadow(0 1px 1px rgba(0,0,0,.25))">{wheel_svg(D, r, wheel, angle)}</div>'
    fg = ""
    if angle is not None and wheel == "ring":
        t = math.radians(angle - 90)
        fg = dv.finger(w / 2 + 0.70 * D / 2 * math.cos(t), cy + 0.70 * D / 2 * math.sin(t))
    if wheel == "menu":
        fg = dv.finger(w / 2, cy - 0.70 * D / 2)
    if wheel == "centre":
        fg = dv.finger(w / 2, cy)
    return (f'<div style="position:relative;width:{w}px;height:{h}px;overflow:hidden;background:{hexs(bd["base"])}">{layers}{frame}{mark}{frost}{wl}{fg}{extra}{isl}{statusbar}{home}</div>', w, h, r)

# ---- the 600x450 screens this design adds --------------------------------------------------------
TICK = '<svg aria-hidden="true" width="{s}" height="{s}" viewBox="0 0 24 24" fill="none" stroke="{c}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6 9 17l-5-5"/></svg>'

def vrow(style, text, value=None, sel=False, chev=False, tick=False, swatch=None, h=56, fs=26):
    """A list row with an optional swatch, value, tick and chevron (the device view's row, extended)."""
    s = dv.ST[style]
    st = s["sel"] if sel else ""
    ink2 = "inherit" if sel else s["ink2"]
    sw = f'<span style="flex:none;width:{round(fs*1.15)}px;height:{round(fs*1.15)}px;box-sizing:border-box;border:1px solid rgba(128,128,128,.8);border-radius:4px;background:{swatch}"></span>' if swatch else ""
    val = f'<span style="font-size:{round(fs*0.85)}px;color:{ink2};white-space:nowrap">{value}</span>' if value else ""
    tk = TICK.format(s=round(fs), c="#ffffff" if sel else s["ink2"]) if tick else ""
    ch = dv.CHEV.format(s=round(fs*0.85), c="#ffffff" if sel else s["chev"]) if chev else ""
    return (f'<div style="height:{h}px;flex:none;box-sizing:border-box;display:flex;align-items:center;gap:{round(fs*0.5)}px;padding:0 {s["rowpad"]}px;font-size:{fs}px;{st}">{sw}'
            f'<span style="flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{text}</span>{val}{tk}{ch}</div>')

def how_clear(q):
    return f'{q["t"]} % clear' if q["t"] else "Solid"

def settings_screen(style, r):
    rows = [vrow(style, "Style", "Classic" if style == "classic" else "Modern"), vrow(style, "Mode", "Custom", sel=True),
            vrow(style, "Custom look", r["look"]["name"], chev=True), vrow(style, "Library", chev=True),
            vrow(style, "Click sound", "Off"), vrow(style, "Haptics", "On"), vrow(style, "Licences", chev=True)]
    return dv.screen(style, "Settings", dv.listwrap(style, "".join(rows)), playing=False)

def looks_screen(style, r):
    rows = [vrow(style, "Harbour", sel=True, tick=True, chev=True), vrow(style, "Sea glass", chev=True), vrow(style, "Clear case", chev=True), vrow(style, "New look")]
    return dv.screen(style, "Custom look", dv.listwrap(style, "".join(rows)), left="1 of 4", playing=False)

def look_screen(style, r, board):
    rows = [vrow(style, "Body", how_clear(r["body"]), sel=True, chev=True, swatch=hexs(r["body"]["base"])),
            vrow(style, "Wheel", how_clear(r["wheel"]), chev=True, swatch=hexs(r["wheel"]["base"])),
            vrow(style, "Centre", how_clear(r["centre"]), chev=True, swatch=hexs(r["centre"]["base"])),
            vrow(style, "Board", BOARDS[board]["name"], chev=True), vrow(style, "Screen", "Auto"),
            vrow(style, "Name", r["look"]["name"], chev=True), vrow(style, "Reset or delete", chev=True)]
    return dv.screen(style, r["look"]["name"], dv.listwrap(style, "".join(rows)), left="1 of 7", playing=False)

def board_screen(style, r, board):
    s = dv.ST[style]
    rows = "".join(vrow(style, b["name"], sel=(k == board), tick=(k == board)) for k, b in BOARDS.items())
    thumb = (f'<div style="width:150px;height:195px;overflow:hidden;border-radius:6px;box-shadow:0 2px 5px rgba(0,0,0,.4)">'
             f'<div style="width:300px;height:780px;margin:-330px 0 0 -75px">{board_svg(board).replace("<svg ", "<svg width=\"300\" height=\"780\" ", 1)}</div></div>')
    pane = (f'<div style="flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:14px;background:linear-gradient(#f3f4f6,#dde0e4);border-left:1px solid #b9bdc4;font-size:22px;color:{s["ink2"]}">{thumb}<span>Placeholder picture</span></div>')
    body = f'<div style="flex:1;min-height:0;display:flex"><div style="width:56%;flex:none;display:flex;flex-direction:column">{rows}</div>{pane}</div>'
    return dv.screen(style, "Board", body, left=f"{list(BOARDS).index(board) + 1} of 3", playing=False)

FIELDS = ["Hue", "Saturation", "Brightness", "Transparency"]

def disc(p, d, line):
    a, rad = math.radians(p[0]), p[1] / 100 * d / 2
    return (f'<div role="img" aria-label="Colour wheel" style="position:relative;flex:none;width:{d}px;height:{d}px;box-sizing:border-box;border:1px solid {line};border-radius:50%;'
            f'background:radial-gradient(closest-side,#fff,rgba(255,255,255,0)),conic-gradient(from 0deg,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)">'
            f'<span style="position:absolute;inset:0;border-radius:50%;background:#000;opacity:{1 - p[2] / 100:.2f}"></span>'
            f'<span style="position:absolute;left:{d / 2 + math.sin(a) * rad - 13:.1f}px;top:{d / 2 - math.cos(a) * rad - 13:.1f}px;width:26px;height:26px;box-sizing:border-box;border:3px solid #fff;border-radius:50%;box-shadow:0 0 0 1.5px #000,inset 0 0 0 1.5px #000;background:{hexs(hsv(*p[:3]))}"></span></div>')

def field_bar(i, p, top, sel):
    h, s, v, t = p
    grad = ["linear-gradient(90deg,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)",
            f"linear-gradient(90deg,{hexs(hsv(h, 0, v))},{hexs(hsv(h, 100, v))})",
            f"linear-gradient(90deg,#000,{hexs(hsv(h, s, 100))})",
            f"linear-gradient(90deg,{hexs(hsv(h, s, v))},{rgba(hsv(h, s, v), 0.1)}),repeating-conic-gradient(#bbb 0 25%,#fff 0 50%) 0 0/8px 8px"][i]
    frac = [h / 359, s / 100, v / 100, min(t, top) / top][i]
    edge = "#ffffff" if sel else "#8c9199"
    return (f'<div style="position:relative;height:10px;margin-top:5px;border-radius:5px;box-shadow:0 0 0 1px {edge};background:{grad}">'
            f'<span style="position:absolute;left:calc({frac:.3f} * (100% - 18px));top:-4px;width:18px;height:18px;box-sizing:border-box;border-radius:50%;background:#fff;border:2px solid #181818"></span></div>')

def picker_screen(style, r, part, field=0, k=1.0):
    """The editor for one part: part tabs, the colour wheel, four fields, a legend note and the hint."""
    s = dv.ST[style]
    q, p = r[part], r["look"][part]
    top = {"body": BODY_MAX, "wheel": WHEEL_MAX, "centre": CENTRE_MAX}[part]
    vals = [f"{p[0]}°", f"{p[1]} %", f"{p[2]} %", f"{min(p[3], top)} %"]
    note = f'Legends: {"black" if q["ink"] == BLACK else "white"} ink, {q["worst"]:.1f}:1' + (" · painted flat" if q["flat"] else "")
    if part == "centre":
        note = "The centre button carries no legend"
    if part == "wheel" and q["t"] < q["asked"]:
        note = f'Held at {q["t"]} % so MENU stays readable'
    title = {"body": "Body", "wheel": "Wheel", "centre": "Centre"}[part]
    if k > 1.3:  # largest text tier: the part is the status-bar title; one field, large
        f = (f'<div style="flex:1;min-width:0;display:flex;flex-direction:column;justify-content:center;gap:8px">'
             f'<div style="font-size:31px;color:{s["ink2"]}">{FIELDS[field]}</div><div style="font-size:53px;line-height:58px">{vals[field]}</div>'
             f'{field_bar(field, p, top, False)}<div style="font-size:31px;color:{s["ink2"]};margin-top:14px">{field + 1} of 4</div></div>')
        body = f'<div style="flex:1;min-height:0;display:flex;align-items:center;gap:28px;padding:0 19px">{disc(p, 230, "#9da2a9")}{f}</div><div style="height:48px;flex:none;padding:0 19px;font-size:31px;color:{s["ink2"]};white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{note}</div>'
        return dv.screen(style, title, body, left=r["look"]["name"], k=k, playing=False)
    tabs = ""
    for key, name in (("body", "Body"), ("wheel", "Wheel"), ("centre", "Centre")):
        on = "background:#d8e4f8;color:#1858c8;border:1px solid #d8e4f8" if key == part else f"border:1px solid #8c9199;color:{'#181818'}"
        tabs += f'<span style="height:36px;box-sizing:border-box;padding:0 18px;border-radius:18px;font-size:22px;line-height:34px;{on}">{name}</span>'
    rows = ""
    for i, name in enumerate(FIELDS):
        sel = i == field
        st = s["sel"] if sel else ""
        rows += (f'<div style="box-sizing:border-box;height:60px;padding:6px 12px 0;{st}"><div style="display:flex;justify-content:space-between;font-size:22px;line-height:26px"><span>{name}</span><span>{vals[i]}</span></div>{field_bar(i, p, top, sel)}</div>')
    sw = f'<div style="display:flex;align-items:center;gap:10px;height:40px;padding:0 12px;font-size:22px;color:{s["ink2"]}"><span style="width:30px;height:30px;box-sizing:border-box;border:1px solid #9da2a9;border-radius:4px;background:{hexs(q["base"])}"></span>{hexs(q["base"])}</div>'
    body = (f'<div style="height:52px;flex:none;display:flex;align-items:center;gap:10px;padding:0 19px">{tabs}</div>'
            f'<div style="flex:1;min-height:0;display:flex;gap:16px;padding:0 7px 0 19px">{disc(p, 250, "#9da2a9")}<div style="flex:1;min-width:0">{sw}{rows}</div></div>'
            f'<div style="height:30px;flex:none;padding:0 19px;font-size:22px;line-height:28px;color:{s["ink2"]}">{note}</div>'
            f'<div style="height:36px;flex:none;padding:0 19px;font-size:22px;line-height:30px;color:{s["ink3"]}">Scroll: change · Prev/Next: field · Select: next part</div>')
    return dv.screen(style, title, body, left=r["look"]["name"], playing=False)

# ---- sheets --------------------------------------------------------------------------------------
def mini(q, d=110, t_note=""):
    ink = q["ink"]
    ow = 1 - q["t"] / 100
    return (f'<div style="position:relative;flex:none;width:{d}px;height:{d}px;border-radius:50%;color:{hexs(ink)};box-shadow:0 1px 1px rgba(0,0,0,.25),inset 0 0 0 1px {rgba(ink, 0.3)};'
            f'background:linear-gradient(180deg,{rgba(q["stops"][0], ow)},{rgba(q["stops"][2], ow)}){t_note}">'
            f'<span style="position:absolute;left:0;top:12px;width:100%;text-align:center;font-size:11px;letter-spacing:1px">MENU</span>'
            f'<svg aria-hidden="true" width="20" height="14" viewBox="0 0 26 18" fill="currentColor" style="position:absolute;left:{d / 2 - 10}px;bottom:11px"><path d="M1 2v14l11-7z"/><rect x="15" y="2" width="3.4" height="14"/><rect x="21" y="2" width="3.4" height="14"/></svg>'
            f'<span style="position:absolute;left:{d / 2 - 20}px;top:{d / 2 - 20}px;width:40px;height:40px;box-sizing:border-box;border-radius:50%;border:2px solid {rgba(ink, 0.22)}"></span></div>')

def contrast_sheet():
    def cell(p, grounds=None, board=None):
        q = paint(p, "wheel", grounds)
        extra = f",{frost_css(board)}" if (board and q["t"]) else ""
        lines = [hexs(q["base"]), f"L {lum(q['base']):.3f}", f'{"Black" if q["ink"] == BLACK else "White"} ink', f'<b style="color:#181818">{q["worst"]:.2f}:1</b>',
                 ("Painted flat" if q["flat"] else "Gradient kept") if not grounds else (f'Asked {q["asked"]} %, kept {q["t"]} %')]
        return (f'<div style="display:flex;gap:14px;align-items:center;padding:12px;border:1px solid #e0e0e0;border-radius:6px;background:#fff">{mini(q, t_note=extra)}'
                f'<div style="display:flex;flex-direction:column;gap:3px;font-size:13px;line-height:17px;color:#606060">' + "".join(f"<span>{x}</span>" for x in lines) + "</div></div>")
    fr = [BOARDS["main-top"]["frost_dark"], BOARDS["main-top"]["frost_light"]]
    bands = [("Light colours: black ink", [cell((h, s, v, 0)) for h, s, v in ((0, 12, 98), (48, 14, 96), (140, 14, 94), (205, 14, 94), (290, 10, 96))]),
             ("Mid colours, round the 0.179 switch: where the gradient is given up", [cell((h, s, v, 0)) for h, s, v in ((0, 62, 82), (48, 70, 50), (140, 60, 50), (205, 60, 62), (290, 55, 70))]),
             ("Dark colours: white ink", [cell((h, s, v, 0)) for h, s, v in ((0, 70, 34), (48, 60, 24), (140, 60, 22), (205, 62, 30), (290, 60, 28))]),
             ("See-through wheel, 60 % asked, over the placeholder board's frost: transparency is given back until the legend passes",
              [cell((h, s, v, 60), fr, "main-top") for h, s, v in ((200, 4, 96), (48, 30, 80), (140, 40, 60), (205, 60, 50), (290, 60, 20))])]
    out = ('<div style="width:1340px;box-sizing:border-box;padding:36px 40px 40px;background:#f8f8f8;color:#181818;font-family:' + FONT + ';font-weight:500;display:flex;flex-direction:column;gap:22px">'
           '<div><div style="font-size:26px">Legend contrast on any colour</div><div style="margin-top:8px;font-size:15px;line-height:22px;color:#606060;max-width:1150px">Ink is pure black or pure white. '
           'The figure in each cell is the worst contrast under the legend, measured at both ends of the part\'s gradient (and, for a see-through wheel, over the darkest and lightest of the frosted board). Every cell reaches 4.5:1.</div></div>')
    for name, cells in bands:
        out += f'<div><div style="font-size:16px;color:#606060;margin-bottom:10px">{name}</div><div style="display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:16px">{"".join(cells)}</div></div>'
    return out + "</div>"

def wheel_states():
    cells = ""
    for look, board, name in (("clear", "main-top", "Clear case: 60 % see-through"), ("harbour", "main-top", "Harbour: solid")):
        r = resolve(look, board)
        for state, angle, label in (("idle", None, "Idle"), ("ring", 62, "Finger on the ring"), ("menu", None, "Menu pressed"), ("centre", None, "Centre pressed")):
            bg = f"background:{frost_css(board)};" if r["wheel"]["t"] else ""
            under = f'<div style="position:absolute;inset:0;border-radius:50%;{bg}"></div>'
            body = f'linear-gradient(180deg,{rgba(r["body"]["stops"][0], 1 - r["body"]["t"] / 100)},{rgba(r["body"]["stops"][2], 1 - r["body"]["t"] / 100)}),{hexs(BOARDS[board]["ground"])}'
            cells += (f'<div style="display:flex;flex-direction:column;align-items:center;gap:10px;padding:22px 18px 14px;background:{body}">'
                      f'<div style="position:relative;width:200px;height:200px">{under}<div style="position:relative">{wheel_svg(200, r, state, angle)}</div></div>'
                      f'<div style="padding:3px 10px;border-radius:12px;background:#fff;font-size:13px;white-space:nowrap;color:#181818">{name.split(":")[0]} · {label}</div></div>')
    return f'<div style="width:944px;display:grid;grid-template-columns:repeat(4,236px);font-family:{FONT};font-weight:500">{cells}</div>'

def asset_sheet():
    """The board asset: its size, the anchor, and where three phones' bodies fall on it."""
    k = 0.22
    W, H = ASSET_W * k, ASSET_H * k
    svg = board_svg("main-top").replace("<svg ", f'<svg width="{W:.0f}" height="{H:.0f}" ', 1)
    over = f'<svg width="{W:.0f}" height="{H:.0f}" viewBox="0 0 {ASSET_W} {ASSET_H}" style="position:absolute;left:0;top:0">'
    for phone, col, lx in (("375", "#ffd23f", 24), ("393", "#ffffff", 24), ("440", "#7fd4ff", 920)):
        g = dv.geom(phone, "A")
        s = ASSET_W / g["w"]
        top, bot = ANCHOR_Y - g["cy"] * s, ANCHOR_Y + (g["h"] - g["cy"]) * s
        over += f'<rect x="3" y="{top:.0f}" width="{ASSET_W - 6}" height="{bot - top:.0f}" fill="none" stroke="{col}" stroke-width="6" stroke-dasharray="26 14"/><text x="{lx}" y="{top + 54:.0f}" font-family="{FONT}" font-size="44" fill="{col}">{g["w"]}x{g["h"]}</text>'
    D = dv.WHEEL_D * ASSET_W
    over += (f'<circle cx="{ANCHOR_X}" cy="{ANCHOR_Y}" r="{D / 2:.0f}" fill="none" stroke="#ff5a5a" stroke-width="6"/><path d="M{ANCHOR_X - 50} {ANCHOR_Y}h100M{ANCHOR_X} {ANCHOR_Y - 50}v100" stroke="#ff5a5a" stroke-width="6"/></svg>')
    notes = ["<b>1200 x 3120 px</b>, portrait, opaque. W, the phone's width, is 1200 px.",
             "<b>Anchor (600, 2040)</b>: the red cross. The app puts it on the wheel's centre.",
             "Scale: phone width / 1200. Never stretched, never cropped to fit.",
             "The red circle is the wheel (0.64 W). What lies inside it is seen only as the blurred copy.",
             "Dashed boxes: the body of each phone size. A taller phone shows more above the wheel.",
             "Beyond the picture's edge the app paints the board's ground colour.",
             "This picture is a placeholder drawn by render.py. It is not a neuPod board."]
    side = "".join(f'<div style="font-size:15px;line-height:22px;margin-bottom:12px">{n}</div>' for n in notes)
    return (f'<div style="display:flex;gap:32px;width:{W + 420:.0f}px;box-sizing:border-box;padding:28px;background:#f8f8f8;color:#181818;font-family:{FONT};font-weight:500">'
            f'<div style="position:relative;flex:none;width:{W:.0f}px;height:{H:.0f}px">{svg}{over}</div><div><div style="font-size:24px;margin-bottom:16px">The board picture</div>{side}</div></div>', round(W + 420), round(H + 56))

# ---- pages ---------------------------------------------------------------------------------------
def build():
    def phone(name, title, inner_fn, look, board="main-top", **kw):
        r = resolve(look, board, kw.get("a11y"))
        doc, w, h, _ = device(inner_fn(r), look, board, **kw)
        dv.page(name, title, doc, w, h)
    home_c, home_m = dv.ui_body("classic-home"), dv.ui_body("modern-home")
    phone("settings-mode-rows", "Settings, Mode is Custom", lambda r: settings_screen("classic", r), "harbour")
    phone("looks-list", "Custom look, the list", lambda r: looks_screen("classic", r), "harbour")
    phone("look", "A look", lambda r: look_screen("classic", r, "main-top"), "harbour")
    phone("editor-body", "Editor, Body", lambda r: picker_screen("classic", r, "body", 0), "harbour", wheel="ring", angle=118)
    phone("editor-wheel", "Editor, Wheel, see-through", lambda r: picker_screen("classic", r, "wheel", 3), "clear")
    phone("editor-centre", "Editor, Centre", lambda r: picker_screen("classic", r, "centre", 2), "hot", wheel="centre")
    phone("editor-largest-text", "Editor at the largest text tier", lambda r: picker_screen("classic", r, "body", 0, k=1.4), "harbour")
    phone("board-picker", "Board picker", lambda r: board_screen("classic", r, "main-bottom"), "seaglass", board="main-bottom")
    phone("look-opaque", "Finished look: opaque colour", lambda r: home_c, "harbour")
    phone("look-part-transparent", "Finished look: part transparent", lambda r: home_c, "seaglass")
    phone("look-clear-case", "Finished look: clear case", lambda r: home_c, "clear", board="copper")
    phone("awkward-pale-classic-light", "Pale body with Classic Light", lambda r: home_c, "pale")
    phone("awkward-saturated-modern-dark", "Saturated body with Modern Dark", lambda r: dv.ui_body("modern-now-playing"), "hot")
    phone("a11y-reduce-transparency", "Clear case with Reduce Transparency on", lambda r: home_c, "clear", a11y="reduce-transparency")
    phone("a11y-increase-contrast", "Sea glass with Increase Contrast on", lambda r: home_c, "seaglass", a11y="increase-contrast")
    phone("default-light", "Reset to default, Light screen", lambda r: home_c, "default-light")
    phone("default-dark", "Reset to default, Dark screen", lambda r: home_m, "default-dark")
    dv.page("legend-contrast-sheet", "Legend contrast sheet", contrast_sheet(), 1340, 930)
    dv.page("wheel-states-see-through", "Wheel states, see-through and solid", wheel_states(), 944, 536)
    a, w, h = asset_sheet()
    dv.page("board-asset", "The board picture: size, anchor and alignment", a, w, h)

if __name__ == "__main__":
    build()
    dv.write(png="--no-png" not in sys.argv)
    if "--numbers" in sys.argv:
        for k in LOOKS:
            r = resolve(k, "main-top")
            print(k, {p: (hexs(r[p]["base"]), "black" if r[p]["ink"] == BLACK else "white", round(r[p]["worst"], 2), r[p]["t"], r[p]["flat"]) for p in ("body", "wheel", "centre")})
