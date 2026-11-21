#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Render the app device-view mock-ups: standalone HTML and PNG in mockups/.

    python3 docs/design/2026-11-21-app-device-view/render.py [--no-png]

The 4:3 screens are the UI styles mock-ups (../2026-11-21-ui-styles/mockups/*.html), reused
unchanged and scaled, plus the few screens that design did not draw. PNGs need Chrome
(`google-chrome` or CHROME=/path) and the Montserrat font installed.
All geometry is in phone points and derived from the ratios in README.md.
"""
import html, math, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "mockups")
UI = os.path.join(HERE, "..", "2026-11-21-ui-styles", "mockups")
CHROME = os.environ.get("CHROME", "google-chrome")
FONT = "Montserrat, system-ui, sans-serif"

# ---- geometry (ratios of the phone width W) -----------------------------------------------------
SCREEN_W, SCREEN_H = 0.88, 0.66      # 4:3
BEZEL = 0.012                        # black glass frame round the screen
TOP_GAP = 0.03                       # safe-area top to the frame (fit A)
WHEEL_D = 0.64
CENTRE_D = 0.36                      # of the wheel diameter
BODY_ASPECT = 1.675                  # fit B: body height / width
BODY_R = 0.08
PHONES = {  # name: (w, h, safe top, safe bottom, island, keyboard height incl. safe bottom)
    "375": (375, 667, 20, 0, False, 260),
    "393": (393, 852, 59, 34, True, 336),
    "440": (440, 956, 62, 34, True, 346),
}
FINISH = {
    "silver": dict(body="linear-gradient(180deg,#eceef1 0%,#d9dce1 45%,#c3c7ce 100%)", ring=("#fbfbfc", "#e1e4e8"),
                   ring_edge="#a9aeb6", centre=("#e6e9ed", "#c9cdd4"), groove="#b4b9c0", glyph="#5a5f67",
                   press="rgba(0,0,0,.13)", touch="rgba(0,0,0,.07)", mark="#8d929a", status="#181818"),
    "black": dict(body="linear-gradient(180deg,#1b1c20 0%,#101114 50%,#08090a 100%)", ring=("#2b2d32", "#1d1f23"),
                  ring_edge="#3a3c42", centre=("#15161a", "#0b0c0e"), groove="#050506", glyph="#cfcfcf",
                  press="rgba(255,255,255,.14)", touch="rgba(255,255,255,.08)", mark="#5c5f66", status="#f0f0f0"),
}
SURROUND = "#0b0c0e"
STYLE_FINISH = {"classic": "silver", "modern": "black"}

# ---- the 600x450 screens ------------------------------------------------------------------------
def ui_body(name):
    t = open(os.path.join(UI, name + ".html"), encoding="utf-8").read()
    return re.search(r"<body>(.*)</body>", t, re.S).group(1).strip()

CHEV = '<svg aria-hidden="true" width="{s}" height="{s}" viewBox="0 0 24 24" fill="none" stroke="{c}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="m9 18 6-6-6-6"/></svg>'
HEADPH = '<svg aria-hidden="true" width="{s}" height="{s}" viewBox="0 0 24 24" fill="none" stroke="{c}" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" style="margin-right:{m}px"><path d="M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-7a9 9 0 0 1 18 0v7a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3"/></svg>'
SEARCH = '<svg aria-hidden="true" width="{s}" height="{s}" viewBox="0 0 24 24" fill="none" stroke="{c}" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="m21 21-4.34-4.34"/><circle cx="11" cy="11" r="8"/></svg>'

ST = {
    "classic": dict(
        root="background:#ffffff;color:#181818", ink2="#606060", ink3="#767676",
        bar="background:linear-gradient(#fdfdfd,#dcdfe3 50%,#c6cad0 51%,#b6bbc2);border-bottom:1px solid #8c9199",
        glyph="#3a3d42", left="#3a3d42", chev="#a0a0a0", list="", rowpad=19,
        sel="color:#ffffff;text-shadow:0 1px 0 rgba(0,0,0,.3);background:linear-gradient(#6db3f5,#2f80e0 50%,#1b62cf 51%,#1858c8)",
        header="background:linear-gradient(#f3f4f6,#e3e6e9);border-bottom:1px solid #b9bdc4",
        art="box-shadow:0 2px 5px rgba(0,0,0,.4)", amber="#d08018", amber_soft="#f8e8c8",
        field="background:#ffffff;border:1px solid #9da2a9;border-radius:8px", sect="background:#e9ebee;color:#606060",
        tile="background:rgba(24,24,24,.86);color:#ffffff", rail="#a0a0a0", rail_on="#181818",
        batt='<div style="width:{w}px;height:{h}px;box-sizing:border-box;padding:2px;border:2px solid #5d626a;border-radius:4px;background:#f4f5f6"><div style="width:78%;height:100%;border-radius:1px;background:linear-gradient(#c9f5a3,#63c62f 45%,#3f9c1c 55%,#58b82a)"></div></div><div style="width:4px;height:{n}px;background:#5d626a;border-radius:0 2px 2px 0"></div>'),
    "modern": dict(
        root="color:#f0f0f0;background:linear-gradient(rgba(0,0,0,.3),rgba(0,0,0,.5)),radial-gradient(120% 90% at 85% 20%,rgba(217,115,78,.45),rgba(217,115,78,0) 60%),radial-gradient(110% 100% at 10% 100%,rgba(30,70,110,.75),rgba(22,50,79,0) 65%),#050608",
        ink2="#b0b0b0", ink3="#a8a8a8",
        bar="background:rgba(10,10,12,.5);border-bottom:1px solid rgba(255,255,255,.1)",
        glyph="#f0f0f0", left="#b8b8b8", chev="#8a8a8a", list="padding:0 6px;background:rgba(0,0,0,.42)", rowpad=13,
        sel="border-radius:14px;color:#ffffff;background:linear-gradient(rgba(255,255,255,.16),rgba(255,255,255,0) 55%),#b8502c;box-shadow:0 6px 16px rgba(0,0,0,.45),inset 0 1px 0 rgba(255,255,255,.28)",
        header="", art="border-radius:14px;box-shadow:0 10px 24px rgba(0,0,0,.6),inset 0 1px 0 rgba(255,255,255,.2)",
        amber="#e89828", amber_soft="rgba(232,152,40,.28)",
        field="background:rgba(255,255,255,.1);border:1px solid rgba(255,255,255,.2);border-radius:14px",
        sect="background:rgba(255,255,255,.07);color:#c4c4c4",
        tile="background:rgba(240,240,240,.92);color:#101010", rail="#8a8a8a", rail_on="#ffffff",
        batt='<div style="width:{w}px;height:{h}px;box-sizing:border-box;padding:2px;border:2px solid rgba(255,255,255,.6);border-radius:8px"><div style="width:78%;height:100%;border-radius:4px;background:linear-gradient(#8cecb4,#30b870)"></div></div><div style="width:3px;height:{n}px;margin-left:1px;background:rgba(255,255,255,.6);border-radius:0 2px 2px 0"></div>'),
}

def px(v):
    return f"{round(v)}px"

def screen(style, title, content, left="", k=1.0, playing=True):
    """A 600x450 screen in the UI styles look. k scales type and bars for the large-text tiers."""
    s = ST[style]
    play = (f'<svg aria-hidden="true" width="{round(18*k)}" height="{round(18*k)}" viewBox="0 0 24 24" fill="{s["glyph"]}"><path d="M6 3l14 9-14 9z"/></svg>' if playing else "")
    lefttxt = f'<span>{left}</span>' if left else ""
    return (f'<div style="width:600px;height:450px;box-sizing:border-box;display:flex;flex-direction:column;{s["root"]};font-family:{FONT};font-weight:500;overflow:hidden;position:relative">'
            f'<div style="height:{px(45*k)};flex:none;box-sizing:border-box;display:grid;grid-template-columns:1fr auto 1fr;align-items:center;padding:0 19px;{s["bar"]}">'
            f'<div style="display:flex;align-items:center;gap:8px;font-size:{px(22*k)};color:{s["left"]};white-space:nowrap">{play}{lefttxt}</div>'
            f'<div style="font-size:{px(26*k)};white-space:nowrap">{title}</div>'
            f'<div style="display:flex;justify-content:flex-end;align-items:center">{HEADPH.format(s=round(22*k), c=s["glyph"], m=10)}{s["batt"].format(w=round(44*k), h=round(22*k), n=round(10*k))}</div>'
            f'</div>{content}</div>')

def row(style, text, sel=False, chev=True, h=56, fs=26, num=None, right=None):
    s = ST[style]
    st = s["sel"] if sel else ""
    cc = "#ffffff" if sel else s["chev"]
    n = (f'<span style="width:{round(fs*1.15)}px;flex:none;text-align:right;font-size:{round(fs*0.85)}px;color:{"inherit" if sel else s["ink3"]}">{num}</span>' if num is not None else "")
    r = CHEV.format(s=round(fs*0.85), c=cc) if chev else (f'<span style="font-size:{round(fs*0.85)}px;color:{"inherit" if sel else s["ink2"]}">{right}</span>' if right else "")
    return (f'<div style="height:{px(h)};flex:none;box-sizing:border-box;display:flex;align-items:center;gap:{round(fs*0.6)}px;padding:0 {s["rowpad"]}px;font-size:{px(fs)};{st}">{n}'
            f'<span style="flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{text}</span>{r}</div>')

def row2(style, a, b, sel=False, h=75, fs=26):
    s = ST[style]
    st = s["sel"] if sel else ""
    if sel and style == "modern":
        st = st.replace("border-radius:14px", "border-radius:16px")
    return (f'<div style="height:{px(h)};flex:none;box-sizing:border-box;display:flex;flex-direction:column;justify-content:center;padding:0 {s["rowpad"]}px;{st}">'
            f'<div style="font-size:{px(fs)};line-height:{px(fs*1.12)};white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{a}</div>'
            f'<div style="font-size:{px(fs*0.85)};line-height:{px(fs*0.93)};color:{"inherit" if sel else s["ink2"]};white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{b}</div></div>')

def listwrap(style, inner):
    return f'<div style="flex:1;min-height:0;box-sizing:border-box;display:flex;flex-direction:column;{ST[style]["list"]}">{inner}</div>'

HOME = ["Music", "Podcasts", "Photos", "Extras", "Settings", "Shuffle Songs", "Now Playing"]
SONGS = [("Carrier Wave", "Beacon · Signal"), ("Café Del Mar", "Aurora Lane · First Light"), ("Ember", "The Cinders · Now 7"),
         ("Harbour Lights", "Aurora Lane · First Light"), ("Hit A", "Zed Zed · Hit A")]
ART = "linear-gradient(135deg,#16324f 50%,#d9734e 50%)"
ART2 = "linear-gradient(135deg,#1d3557 50%,#a8dadc 50%)"

def home_k(style, k):
    """Home at a text tier: k = 1 (7 rows), 7/6 (6 rows), 7/5 (5 rows)."""
    s = ST[style]
    n = {1.0: 7}.get(k, 6 if k < 1.3 else 5)
    h = (450 - round(45 * k)) // n
    rows = "".join(row(style, t, sel=(i == 0), chev=(t != "Shuffle Songs"), h=h, fs=26 * k) for i, t in enumerate(HOME[:n]))
    pane_bg = "background:linear-gradient(#f3f4f6,#dde0e4);border-left:1px solid #b9bdc4" if style == "classic" else ""
    lst_bg = "" if style == "classic" else "padding:0 6px;background:rgba(0,0,0,.38);border-right:1px solid rgba(255,255,255,.08)"
    art = 220 if k < 1.3 else 200
    pane = (f'<div style="flex:1;min-width:0;box-sizing:border-box;display:flex;flex-direction:column;align-items:center;padding:22px 16px 0;text-align:center;{pane_bg}">'
            f'<div style="width:{art}px;height:{art}px;flex:none;background:{ART};{s["art"]}"></div>'
            f'<div style="margin-top:14px;max-width:100%;font-size:{px(26*k)};line-height:{px(29*k)};white-space:nowrap;overflow:hidden;text-overflow:ellipsis">Harbour Lights</div>'
            f'<div style="max-width:100%;font-size:{px(22*k)};line-height:{px(24*k)};color:{s["ink2"]};white-space:nowrap;overflow:hidden;text-overflow:ellipsis">Aurora Lane</div></div>')
    return screen(style, "neuPod", f'<div style="flex:1;display:flex;min-height:0"><div style="width:336px;flex:none;box-sizing:border-box;display:flex;flex-direction:column;{lst_bg}">{rows}</div>{pane}</div>', k=k)

def songs_k(style, k):
    n = {1.0: 5}.get(k, 4 if k < 1.3 else 3)
    h = (450 - round(45 * k)) // n
    sel = min(3, n - 1)
    rows = "".join(row2(style, a, b, sel=(i == sel), h=h, fs=26 * k) for i, (a, b) in enumerate(SONGS[:n]))
    return screen(style, "Songs", listwrap(style, rows), left="1,204", k=k)

def songs_letter(style):
    """The Songs mock-up with the fast-scroll letter tile and index rail (docs/ui.md, LB-2)."""
    s = ST[style]
    rows = "".join(row2(style, a, b, sel=(i == 3)) for i, (a, b) in enumerate(SONGS))
    letters = "".join(f'<span style="color:{s["rail_on"] if c == "H" else s["rail"]};font-size:{"22" if c == "H" else "17"}px;line-height:28px">{c}</span>' for c in "ACEGHKMOQSUWY")
    over = (f'<div style="position:absolute;right:52px;top:66px;width:82px;height:82px;border-radius:15px;display:flex;align-items:center;justify-content:center;font-size:48px;{s["tile"]}">H</div>'
            f'<div style="position:absolute;right:8px;top:54px;width:26px;display:flex;flex-direction:column;align-items:center">{letters}</div>')
    return screen(style, "Songs", listwrap(style, rows) + over, left="1,204")

def album_discs(style):
    s = ST[style]
    head = (f'<div style="height:121px;flex:none;box-sizing:border-box;display:flex;align-items:center;gap:18px;padding:0 19px;{s["header"]}">'
            f'<div style="width:96px;height:96px;flex:none;background:{ART2};{s["art"]}"></div>'
            f'<div style="min-width:0"><div style="font-size:38px;line-height:41px;white-space:nowrap">Orbital</div>'
            f'<div style="margin-top:4px;font-size:22px;line-height:24px;color:{s["ink2"]}">1996 · 2 discs</div></div></div>')
    sect = f'<div style="height:34px;flex:none;display:flex;align-items:center;padding:0 {s["rowpad"]+ (6 if style=="modern" else 0)}px;font-size:22px;{s["sect"]}">Disc 1</div>'
    tr = ["The Girl With the Sun in Her Head", "P.E.T.R.O.L.", "The Box", "The Box"]
    rows = "".join(row(style, t, sel=(i == 1), chev=False, num=i + 1) for i, t in enumerate(tr))
    wrap = ST[style]["list"].replace("background:rgba(0,0,0,.42)", "")
    body = head + (f'<div style="flex:1;min-height:0;display:flex;flex-direction:column;{"background:rgba(0,0,0,.42);border-top:1px solid rgba(255,255,255,.08)" if style=="modern" else ""}">{sect}<div style="display:flex;flex-direction:column;{wrap}">{rows}</div></div>')
    return screen(style, "In Sides", body)

def np_seek(style):
    s = ST[style]
    art_r = "" if style == "classic" else "border-radius:16px;"
    body = (f'<div style="flex:1;min-height:0;box-sizing:border-box;display:flex;flex-direction:column;padding:22px 19px 0">'
            f'<div style="display:flex;gap:22px;height:210px;flex:none">'
            f'<div style="width:210px;height:210px;flex:none;{art_r}background:{ART};box-shadow:0 2px 6px rgba(0,0,0,.4)"></div>'
            f'<div style="flex:1;min-width:0;display:flex;flex-direction:column">'
            f'<div style="font-size:38px;line-height:41px;white-space:nowrap">Harbour Lights</div>'
            f'<div style="margin-top:6px;font-size:26px;line-height:29px;color:{s["ink2"]};white-space:nowrap">Aurora Lane</div>'
            f'<div style="margin-top:auto;font-size:72px;line-height:76px;color:{s["amber"]}">2:14</div></div></div>'
            f'<div style="margin-top:34px;height:14px;flex:none;position:relative">'
            f'<div style="position:absolute;left:0;right:0;top:4px;height:6px;border-radius:3px;background:{s["amber_soft"]}"></div>'
            f'<div style="position:absolute;left:0;width:55%;top:4px;height:6px;border-radius:3px;background:{s["amber"]}"></div>'
            f'<div style="position:absolute;left:calc(55% - 13px);top:-6px;width:26px;height:26px;border-radius:13px;background:{s["amber"]};box-shadow:0 1px 4px rgba(0,0,0,.4)"></div></div>'
            f'<div style="margin-top:12px;display:grid;grid-template-columns:1fr 1fr;font-size:22px;line-height:24px"><div>1:32</div><div style="text-align:right">-2:29</div></div>'
            f'<div style="margin-top:14px;font-size:22px;line-height:24px;color:{s["ink2"]};text-align:center;white-space:nowrap">Scroll: seek · Select: go · Menu: cancel</div></div>')
    return screen(style, "Now Playing", body, left="3 of 12")

def search(style):
    s = ST[style]
    pad = s["rowpad"] + (6 if style == "modern" else 0)
    field = (f'<div style="height:60px;flex:none;box-sizing:border-box;padding:8px {pad}px"><div style="height:44px;box-sizing:border-box;display:flex;align-items:center;gap:10px;padding:0 12px;font-size:26px;{s["field"]}">'
             f'{SEARCH.format(s=24, c=s["ink2"])}<span>orb</span><span style="width:2px;height:28px;margin-left:-6px;background:{"#1858c8" if style=="classic" else "#f2a586"}"></span></div></div>')
    def sect(t):
        return f'<div style="height:30px;flex:none;display:flex;align-items:center;padding:0 {pad}px;font-size:20px;{s["sect"]}">{t}</div>'
    def thumb(g):
        return f'<span style="width:40px;height:40px;flex:none;background:{g};{"border-radius:8px" if style=="modern" else ""}"></span>'
    lw = ST[style]["list"].replace("background:rgba(0,0,0,.42)", "")
    def rows(inner):
        return f'<div style="display:flex;flex-direction:column;{lw}">{inner}</div>'
    al = lambda t, y, g, sel=False: (f'<div style="height:56px;flex:none;box-sizing:border-box;display:flex;align-items:center;gap:14px;padding:0 {s["rowpad"]}px;font-size:26px;{s["sel"] if sel else ""}">{thumb(g)}'
                                      f'<span style="flex:1;white-space:nowrap">{t}</span><span style="font-size:22px;color:{"inherit" if sel else s["ink2"]}">{y}</span></div>')
    body = (field + sect("Artists") + rows(row(style, "Orbital", sel=True)) + sect("Albums")
            + rows(al("Orbital", "1991", "linear-gradient(135deg,#2b2d42 50%,#8d99ae 50%)") + al("Orbital 2", "1993", "linear-gradient(135deg,#5a3e1b 50%,#e9c46a 50%)"))
            + sect("Songs") + rows(row2(style, "Ska'd For Life (Instrumental Mix)", "Orbital · Back to Mine", h=62, fs=24)))
    bg = "background:rgba(0,0,0,.42)" if style == "modern" else ""
    return screen(style, "Search", f'<div style="flex:1;min-height:0;display:flex;flex-direction:column;{bg}">{body}</div>')

SCREENS = {
    "home": lambda st: ui_body(f"{st}-home"),
    "songs": songs_letter,
    "album": album_discs,
    "cover-flow": lambda st: ui_body(f"{st}-cover-flow"),
    "now-playing": lambda st: ui_body(f"{st}-now-playing"),
    "now-playing-volume": lambda st: ui_body(f"{st}-now-playing-volume"),
    "now-playing-seek": np_seek,
    "panel": lambda st: ui_body(f"{st}-feedback-panel"),
    "search": search,
}

# ---- the device ---------------------------------------------------------------------------------
def wheel_svg(D, fin, state="idle", angle=None):
    """The click wheel, D points across. state: idle | ring | menu | centre | disabled. angle: finger on the ring, degrees clockwise from the top."""
    f = FINISH[fin]
    R, c = 100, CENTRE_D * 100
    op = ".35" if state == "disabled" else "1"
    def pol(r, a):
        t = math.radians(a - 90)
        return 100 + r * math.cos(t), 100 + r * math.sin(t)
    def wedge(a0, a1, fill):
        x0, y0 = pol(99, a0); x1, y1 = pol(99, a1); x2, y2 = pol(c + 4, a1); x3, y3 = pol(c + 4, a0)
        return f'<path d="M{x0:.2f} {y0:.2f}A99 99 0 0 1 {x1:.2f} {y1:.2f}L{x2:.2f} {y2:.2f}A{c+4} {c+4} 0 0 0 {x3:.2f} {y3:.2f}Z" fill="{fill}"/>'
    extra = ""
    if state == "menu":
        extra += wedge(-45, 45, f["press"])
    if state == "ring" and angle is not None:
        extra += wedge(angle - 25, angle + 25, f["touch"])
    cpress = f'<circle cx="100" cy="100" r="{c}" fill="{f["press"]}"/>' if state == "centre" else ""
    g = f["glyph"]
    return (f'<svg width="{D:.1f}" height="{D:.1f}" viewBox="0 0 200 200" role="img" aria-label="Click wheel, {state}">'
            f'<defs><linearGradient id="rg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{f["ring"][0]}"/><stop offset="1" stop-color="{f["ring"][1]}"/></linearGradient>'
            f'<linearGradient id="cg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{f["centre"][0]}"/><stop offset="1" stop-color="{f["centre"][1]}"/></linearGradient></defs>'
            f'<circle cx="100" cy="100" r="99" fill="url(#rg)" stroke="{f["ring_edge"]}" stroke-width="1"/>{extra}'
            f'<circle cx="100" cy="100" r="{c+4}" fill="{f["groove"]}"/><circle cx="100" cy="100" r="{c}" fill="url(#cg)" stroke="{f["ring_edge"]}" stroke-width=".8"/>{cpress}'
            f'<g opacity="{op}" fill="{g}"><text x="100" y="27" text-anchor="middle" font-family="{FONT}" font-weight="500" font-size="11.5" letter-spacing="1.2">MENU</text>'
            f'<g transform="translate(160 91) scale(.75)"><polygon points="5 4 15 12 5 20 5 4"/><rect x="17.5" y="4" width="3" height="16" rx="1"/></g>'
            f'<g transform="translate(22 91) scale(.75)"><polygon points="19 20 9 12 19 4 19 20"/><rect x="3.5" y="4" width="3" height="16" rx="1"/></g>'
            f'<g transform="translate(88 166) scale(.72)"><polygon points="2 4 14 12 2 20 2 4"/><rect x="18" y="4" width="4" height="16" rx="1"/><rect x="25" y="4" width="4" height="16" rx="1"/></g></g></svg>')

def finger(x, y, d=46):
    """Annotation only: where a finger is. Not part of the UI."""
    return f'<div aria-hidden="true" style="position:absolute;left:{x-d/2:.1f}px;top:{y-d/2:.1f}px;width:{d}px;height:{d}px;border-radius:50%;background:rgba(0,140,255,.28);border:2px solid rgba(0,140,255,.85);box-sizing:border-box"></div>'

def keyboard(w, kh, sb, dark):
    bg, key, ink = ("#1c1d20", "#3a3c42", "#f0f0f0") if dark else ("#d2d5db", "#ffffff", "#181818")
    rows = ["qwertyuiop", "asdfghjkl", "zxcvbnm"]
    kw = (w - 6 * 11) / 10
    out, y = "", 54
    for r in rows:
        x0 = (w - (len(r) * kw + (len(r) - 1) * 6)) / 2
        for i, ch in enumerate(r):
            out += f'<div style="position:absolute;left:{x0+i*(kw+6):.1f}px;top:{y}px;width:{kw:.1f}px;height:42px;border-radius:6px;background:{key};color:{ink};display:flex;align-items:center;justify-content:center;font-size:20px">{ch}</div>'
        y += 54
    out += f'<div style="position:absolute;left:{w*0.26:.0f}px;top:{y}px;width:{w*0.48:.0f}px;height:42px;border-radius:6px;background:{key}"></div>'
    out += f'<div style="position:absolute;right:6px;top:{y}px;width:{w*0.22:.0f}px;height:42px;border-radius:6px;background:#1858c8;color:#ffffff;display:flex;align-items:center;justify-content:center;font-size:16px">search</div>'
    return f'<div aria-hidden="true" style="position:absolute;left:0;bottom:0;width:{w}px;height:{kh}px;background:{bg};font-family:system-ui,sans-serif">{out}</div>'

def geom(phone, fit):
    w, h, st, sb, island, kh = PHONES[phone]
    sw, sh, bz = SCREEN_W * w, SCREEN_H * w, BEZEL * w
    D = WHEEL_D * w
    if fit == "A":
        body_top, body_bot = 0, h
        scr_top = st + max(8, TOP_GAP * w) + bz
        floor = h - max(sb, 0.03 * w)
    else:
        bh = BODY_ASPECT * w
        body_bot = h - sb if sb else h
        body_top = body_bot - bh
        scr_top = body_top + 0.075 * w + bz
        floor = body_bot - 0.03 * w
    scr_bot = scr_top + sh
    cy = scr_bot + bz + (floor - scr_bot - bz) / 2
    return dict(w=w, h=h, st=st, sb=sb, island=island, kh=kh, sw=sw, sh=sh, bz=bz, D=D, body_top=body_top, body_bot=body_bot,
                scr_x=(w - sw) / 2, scr_top=scr_top, scr_bot=scr_bot, cy=cy, scale=sw / 600)

def device(phone, style, inner, fit="A", wheel="idle", angle=None, extra="", kb=False, finish=None):
    """One phone screenshot: the phone's own status bar, the device body, the 4:3 screen and the wheel."""
    g = geom(phone, fit)
    fin = finish or STYLE_FINISH[style]
    f = FINISH[fin]
    w, h = g["w"], g["h"]
    page_bg = f["body"] if fit == "A" else SURROUND
    status_ink = f["status"] if fit == "A" else "#f0f0f0"
    body = ""
    if fit == "B":
        edge = "box-shadow:0 0 0 1px rgba(255,255,255,.14),0 18px 40px rgba(0,0,0,.6);" if fin == "black" else "box-shadow:0 18px 40px rgba(0,0,0,.6);"
        body = f'<div style="position:absolute;left:0;top:{g["body_top"]:.1f}px;width:{w}px;height:{g["body_bot"]-g["body_top"]:.1f}px;border-radius:{BODY_R*w:.1f}px;background:{f["body"]};{edge}"></div>'
    sy = (g["st"] - 20) / 2 + 3 if g["island"] else 2
    statusbar = (f'<div aria-hidden="true" style="position:absolute;left:0;top:{sy:.0f}px;width:{w}px;height:20px;display:flex;justify-content:space-between;align-items:center;box-sizing:border-box;padding:0 {28 if g["island"] else 12}px;font:600 15px system-ui,sans-serif;color:{status_ink}">'
                 f'<span>9:41</span><span style="display:flex;align-items:center;gap:1px"><span style="width:24px;height:11px;border:1.5px solid {status_ink};border-radius:3.5px;box-sizing:border-box;padding:1px"><span style="display:block;width:70%;height:100%;background:{status_ink};border-radius:1.5px"></span></span></span></div>')
    isl = f'<div aria-hidden="true" style="position:absolute;left:{(w-124)/2:.0f}px;top:12px;width:124px;height:36px;border-radius:18px;background:#000"></div>' if g["island"] else ""
    home = f'<div aria-hidden="true" style="position:absolute;left:{(w-136)/2:.0f}px;bottom:8px;width:136px;height:5px;border-radius:3px;background:{status_ink if fit=="A" else "#f0f0f0"};opacity:.55"></div>' if g["sb"] else ""
    bz = g["bz"]
    frame = (f'<div style="position:absolute;left:{g["scr_x"]-bz:.1f}px;top:{g["scr_top"]-bz:.1f}px;width:{g["sw"]+2*bz:.1f}px;height:{g["sh"]+2*bz:.1f}px;border-radius:{0.02*w:.1f}px;background:#050608;box-shadow:inset 0 0 0 1px rgba(255,255,255,.08),0 1px 0 rgba(255,255,255,.35)"></div>'
             f'<div style="position:absolute;left:{g["scr_x"]:.1f}px;top:{g["scr_top"]:.1f}px;width:{g["sw"]:.1f}px;height:{g["sh"]:.1f}px;overflow:hidden;border-radius:{0.008*w:.1f}px">'
             f'<div style="width:600px;height:450px;transform:scale({g["scale"]:.5f});transform-origin:0 0">{inner}</div></div>')
    mark_y = g["scr_bot"] + bz + (g["cy"] - g["D"] / 2 - g["scr_bot"] - bz) / 2 - 8
    mark = (f'<div aria-hidden="true" style="position:absolute;left:0;width:{w}px;top:{mark_y:.1f}px;text-align:center;font:500 {0.036*w:.1f}px {FONT};letter-spacing:.04em;color:{f["mark"]}">neuPod</div>'
            if g["cy"] - g["D"] / 2 - g["scr_bot"] > 44 else "")
    wl = f'<div style="position:absolute;left:{(w-g["D"])/2:.1f}px;top:{g["cy"]-g["D"]/2:.1f}px;width:{g["D"]:.1f}px;height:{g["D"]:.1f}px;filter:drop-shadow(0 1px 1px rgba(0,0,0,.25))">{wheel_svg(g["D"], fin, wheel, angle)}</div>'
    fg = ""
    if angle is not None and wheel == "ring":
        t = math.radians(angle - 90)
        fg = finger(w / 2 + 0.70 * g["D"] / 2 * math.cos(t), g["cy"] + 0.70 * g["D"] / 2 * math.sin(t))
    if wheel == "menu":
        fg = finger(w / 2, g["cy"] - 0.70 * g["D"] / 2)
    if wheel == "centre":
        fg = finger(w / 2, g["cy"])
    kbd = keyboard(w, g["kh"], g["sb"], fin == "black") if kb else ""
    return (f'<div style="position:relative;width:{w}px;height:{h}px;overflow:hidden;background:{page_bg}">{body}{frame}{mark}{wl}{fg}{extra}{kbd}{isl}{statusbar}{home}</div>', w, h, g)

# ---- pages --------------------------------------------------------------------------------------
PAGES = []  # (name, title, html, w, h)

def page(name, title, htmlbody, w, h):
    PAGES.append((name, title, htmlbody, w, h))

def build():
    # 7. the screens, at 393x852, fit A, both styles
    for st in ("classic", "modern"):
        for key, fn in SCREENS.items():
            wheel, angle, kb, extra = "idle", None, False, ""
            if key == "songs":
                wheel, angle = "ring", 62
            if key == "now-playing-volume":
                wheel, angle = "ring", 118
            if key == "now-playing-seek":
                wheel, angle = "ring", 300
            if key == "panel":
                wheel = "centre"
            if key == "search":
                kb = True
            body, w, h, _ = device("393", st, fn(st), wheel=wheel, angle=angle, kb=kb)
            page(f"{st}-{key}", f"{st.title()} {key.replace('-', ' ')}, 393x852", body, w, h)
    # 2. fitting the phone: A (fills) and B (true proportions), three sizes
    for st, phones in (("classic", ("375", "393", "440")), ("modern", ("393",))):
        for ph in phones:
            for fit in "AB":
                body, w, h, _ = device(ph, st, SCREENS["home"](st), fit=fit)
                page(f"fit-{fit.lower()}-{st}-{ph}", f"Fit {fit}, {st.title()}, {ph} pt wide", body, w, h)
    # 3. finishes the other way round (the rule for the modes not designed yet)
    for st, fin in (("classic", "black"), ("modern", "silver")):
        body, w, h, _ = device("393", st, SCREENS["home"](st), finish=fin)
        page(f"finish-{fin}-with-{st}", f"{fin.title()} body with the {st.title()} screen", body, w, h)
    # 4. wheel states
    for fin in ("silver", "black"):
        f = FINISH[fin]
        cells = ""
        for label, state, ang in (("Idle", "idle", None), ("Finger on the ring", "ring", 62), ("Button pressed (Menu)", "menu", None), ("Centre pressed", "centre", None), ("Disabled", "disabled", None)):
            cells += (f'<div style="display:flex;flex-direction:column;align-items:center;gap:14px"><div>{wheel_svg(200, fin, state, ang)}</div>'
                      f'<div style="font:500 14px {FONT};color:{f["status"]}">{label}</div></div>')
        page(f"wheel-states-{fin}", f"Wheel states, {fin}", f'<div style="width:1180px;height:290px;box-sizing:border-box;padding:28px 30px;display:flex;justify-content:space-between;background:{f["body"]}">{cells}</div>', 1180, 290)
    # 6. direct touch: wheel only, and wheel plus touching the screen
    st = "classic"
    body, w, h, g = device("393", st, ui_body("classic-music-menu"), wheel="ring", angle=75)
    page("touch-1-wheel-only", "Direct touch candidate 1: the wheel only", body, w, h)
    s = g["scale"]
    tap = finger(g["scr_x"] + 150 * s, g["scr_top"] + (45 + 56 * 5 + 28) * s, 40)
    tint = f'<div aria-hidden="true" style="position:absolute;left:{g["scr_x"]:.1f}px;top:{g["scr_top"]+(45+56*5)*s:.1f}px;width:{336*s:.1f}px;height:{56*s:.1f}px;background:rgba(24,88,200,.18)"></div>'
    body, w, h, _ = device("393", st, ui_body("classic-music-menu"), extra=tint + tap)
    page("touch-2-wheel-and-screen", "Direct touch candidate 2: the wheel plus tapping rows", body, w, h)
    # 8. largest text: 7, 6 and 5 rows
    for k, tag in ((1.0, "7-rows"), (7 / 6, "6-rows"), (7 / 5, "5-rows")):
        for nm, fn in (("home", home_k), ("songs", songs_k)):
            body, w, h, _ = device("393", "classic", fn("classic", k))
            page(f"text-{tag}-{nm}", f"Text size tier, {tag.replace('-', ' ')}, {nm}", body, w, h)
    body, w, h, _ = device("393", "modern", home_k("modern", 7 / 5))
    page("text-5-rows-home-modern", "Text size tier, 5 rows, Home, Modern", body, w, h)
    diagrams()

def lbl(x, y, t, c="#c0182a", anchor="start", size=11):
    return f'<text x="{x:.1f}" y="{y:.1f}" fill="{c}" font-family="system-ui,sans-serif" font-size="{size}" font-weight="600" text-anchor="{anchor}">{html.escape(t)}</text>'

def dim(x0, y0, x1, y1, c="#c0182a"):
    return f'<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}" stroke="{c}" stroke-width="1"/><circle cx="{x0:.1f}" cy="{y0:.1f}" r="2" fill="{c}"/><circle cx="{x1:.1f}" cy="{y1:.1f}" r="2" fill="{c}"/>'

def diagrams():
    # 1. geometry, annotated (fit A at 393x852)
    body, w, h, g = device("393", "classic", SCREENS["home"]("classic"))
    W = w
    x0, x1, yt, yb, cy, D = g["scr_x"], g["scr_x"] + g["sw"], g["scr_top"], g["scr_bot"], g["cy"], g["D"]
    c = CENTRE_D * D
    ov = (f'<svg style="position:absolute;left:0;top:0" width="{w+420}" height="{h}">'
          + dim(x0, yb + 14, x1, yb + 14) + lbl(W + 8, yb + 18, "screen width 0.88 W (4:3)")
          + dim(x1 + 12, yt, x1 + 12, yb) + lbl(W + 8, (yt + yb) / 2, "screen height 0.66 W")
          + dim(0, yt + 30, x0, yt + 30) + lbl(W + 8, yt + 34, "side margin 0.06 W, of which frame 0.012 W")
          + dim(W / 2, g["st"], W / 2, yt - g["bz"]) + lbl(W + 8, g["st"] + 12, "safe area to frame: 0.03 W (min 8 pt)")
          + dim(W / 2 - D / 2, cy + D / 2 + 12, W / 2 + D / 2, cy + D / 2 + 12) + lbl(W + 8, cy + D / 2 + 16, "wheel diameter D = 0.64 W")
          + dim(W / 2 - c / 2, cy, W / 2 + c / 2, cy) + lbl(W + 8, cy + 4, "centre button 0.36 D")
          + dim(W / 2 + D / 2 + 10, yb + g["bz"], W / 2 + D / 2 + 10, h - g["sb"]) + lbl(W + 8, cy - D / 2 - 8, "wheel centred between the frame and the bottom safe area")
          + lbl(W + 8, 40, "W = phone width in points (393 here)", "#181818", size=12)
          + lbl(W + 8, h - 12, f"scale from the 600x450 designs: 0.88 W / 600 = {g['scale']:.3f}", "#181818", size=12)
          + '</svg>')
    page("geometry", "Device geometry as ratios of the phone width", f'<div style="position:relative;width:{w+420}px;height:{h}px;background:#ffffff">{body}{ov}</div>', w + 420, h)
    # 4. hit zones
    S = 520
    R = 200
    cx = cyy = S / 2
    def pol(r, a):
        t = math.radians(a - 90)
        return cx + r * math.cos(t), cyy + r * math.sin(t)
    def ring(r0, r1, fill):
        return f'<circle cx="{cx}" cy="{cyy}" r="{(r0+r1)/2}" fill="none" stroke="{fill}" stroke-width="{r1-r0}"/>'
    lines = "".join(f'<line x1="{pol(0.44*R,a)[0]:.1f}" y1="{pol(0.44*R,a)[1]:.1f}" x2="{pol(1.06*R,a)[0]:.1f}" y2="{pol(1.06*R,a)[1]:.1f}" stroke="#ffffff" stroke-width="2"/>' for a in (45, 135, 225, 315))
    svg = (f'<svg width="{S+330}" height="{S}" viewBox="0 0 {S+330} {S}" role="img" aria-label="Wheel hit zones">'
           f'<rect width="{S+330}" height="{S}" fill="#ffffff"/>'
           + ring(0.44 * R, 1.06 * R, "#bcd7f7") + ring(0.36 * R, 0.44 * R, "#f3c9c9") + f'<circle cx="{cx}" cy="{cyy}" r="{0.36*R}" fill="#c9eccf"/>'
           + f'<circle cx="{cx}" cy="{cyy}" r="{R}" fill="none" stroke="#181818" stroke-width="1.5" stroke-dasharray="5 4"/>' + lines
           + lbl(cx, cyy + 4, "Select", "#14532d", "middle", 15) + lbl(cx, cyy - 0.74 * R, "Menu", "#12305f", "middle", 15)
           + lbl(cx + 0.74 * R, cyy + 5, "Next", "#12305f", "middle", 15) + lbl(cx - 0.74 * R, cyy + 5, "Previous", "#12305f", "middle", 15)
           + lbl(cx, cyy + 0.78 * R, "Play/Pause", "#12305f", "middle", 15)
           + lbl(S + 6, 60, "Centre: radius up to 0.36 R", "#14532d", size=14) + lbl(S + 6, 86, "Dead band: 0.36 R to 0.44 R, ignored", "#8a1c1c", size=14)
           + lbl(S + 6, 112, "Ring: 0.44 R to 1.06 R", "#12305f", size=14) + lbl(S + 6, 138, "Dashed: the drawn edge, R = D / 2", "#181818", size=14)
           + lbl(S + 6, 180, "Four button sectors of 90 degrees,", "#181818", size=14) + lbl(S + 6, 200, "centred on up, right, down, left.", "#181818", size=14)
           + lbl(S + 6, 242, "Press: travel under 7.5 degrees and", "#181818", size=14) + lbl(S + 6, 262, "lifted or held in place.", "#181818", size=14)
           + lbl(S + 6, 290, "Turn: travel of 7.5 degrees or more;", "#181818", size=14) + lbl(S + 6, 310, "no press fires when it lifts.", "#181818", size=14)
           + '</svg>')
    page("wheel-hit-zones", "Wheel hit zones", svg, S + 330, S)

def write(png=True):
    os.makedirs(OUT, exist_ok=True)
    for name, title, body, w, h in PAGES:
        doc = (f'<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n<title>neuPod app mock-up: {html.escape(title)}</title>\n'
               f'<!-- {w}x{h} pt. Design mock-up, not app output. The blue circle marks a finger and is not part of the UI. '
               f'Font: Montserrat Medium. Glyphs: Lucide (ISC) or drawn for this design. Album art squares are placeholders. -->\n'
               f'<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Montserrat:wght@500&display=swap">\n<style>body{{margin:0}}</style>\n</head>\n<body>\n{body}\n</body>\n</html>\n')
        p = os.path.join(OUT, name + ".html")
        open(p, "w", encoding="utf-8").write(doc)
        if png:
            subprocess.run([CHROME, "--headless=new", "--no-sandbox", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=2",
                            f"--window-size={w},{h}", f"--screenshot={os.path.join(OUT, name + '.png')}", "--virtual-time-budget=2000", "file://" + p],
                           capture_output=True, timeout=180)
    print(len(PAGES), "pages")

if __name__ == "__main__":
    build()
    write(png="--no-png" not in sys.argv)
