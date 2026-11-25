#!/usr/bin/env python3
"""Render neuPod UI proposal mock-ups at native resolution (320x240 and 480x360).

Uses Inter as a stand-in for Montserrat Medium (same x-height class, similar widths).
Every length is derived from the metric profile so both resolutions come from one layout.
"""
from PIL import Image, ImageDraw, ImageFont
import os, math

OUT = os.path.join(os.path.dirname(__file__), "mockups")
os.makedirs(OUT, exist_ok=True)
FONT_DIR = "/usr/share/fonts/opentype/inter/"
F_MED = FONT_DIR + "Inter-Medium.otf"
F_SEMI = FONT_DIR + "Inter-SemiBold.otf"
F_REG = FONT_DIR + "Inter-Regular.otf"

# ---------- design tokens (RGB565-friendly: R,B multiples of 8, G multiples of 4) ----------
LIGHT = dict(bg=(248, 248, 248), surface=(255, 255, 255), ink=(24, 24, 24), ink2=(96, 96, 96),
             ink3=(160, 160, 160), line=(224, 224, 224), bar=(232, 232, 232),
             accent=(24, 88, 200), on_accent=(255, 255, 255), accent_soft=(216, 228, 248),
             amber=(208, 128, 24), amber_soft=(248, 232, 200), green=(32, 152, 88), red=(208, 48, 40),
             art=(40, 56, 80), art2=(72, 96, 128))
DARK = dict(bg=(16, 16, 16), surface=(24, 24, 24), ink=(240, 240, 240), ink2=(160, 160, 160),
            ink3=(104, 104, 104), line=(48, 48, 48), bar=(32, 32, 32),
            accent=(48, 120, 232), on_accent=(255, 255, 255), accent_soft=(32, 48, 80),
            amber=(232, 152, 40), amber_soft=(72, 56, 24), green=(48, 184, 112), red=(232, 72, 64),
            art=(40, 56, 80), art2=(72, 96, 128))

PROFILES = {
    "320x240": dict(w=320, h=240, s=1.0, body=14, title=20, big=48, small=12),
    "480x360": dict(w=480, h=360, s=1.5, body=20, title=28, big=48, small=16),
}


class Screen:
    def __init__(self, profile, theme=LIGHT):
        p = PROFILES[profile]
        self.pn = profile
        self.w, self.h, self.s = p["w"], p["h"], p["s"]
        self.t = theme
        self.body = ImageFont.truetype(F_MED, p["body"])
        self.body_semi = ImageFont.truetype(F_SEMI, p["body"])
        self.title = ImageFont.truetype(F_SEMI, p["title"])
        self.big = ImageFont.truetype(F_SEMI, p["big"])
        self.small = ImageFont.truetype(F_MED, p["small"])
        self.small_semi = ImageFont.truetype(F_SEMI, p["small"])
        self.im = Image.new("RGB", (self.w, self.h), theme["bg"])
        self.d = ImageDraw.Draw(self.im)
        self.status_h = self.px(24)
        self.row = self.px(30)      # single-line row
        self.row2 = self.px(40)     # two-line row
        self.pad = self.px(10)
        self.r = self.px(4)

    # ---- helpers ----
    def px(self, v):
        return int(round(v * self.s))

    def text_w(self, s, font):
        return self.d.textlength(s, font=font)

    def ellipsize(self, s, font, maxw):
        if self.text_w(s, font) <= maxw:
            return s
        while s and self.text_w(s + "…", font) > maxw:
            s = s[:-1]
        return s.rstrip() + "…"

    def text(self, xy, s, font, fill, anchor="la", maxw=None):
        if maxw:
            s = self.ellipsize(s, font, maxw)
        self.d.text(xy, s, font=font, fill=fill, anchor=anchor)

    def rrect(self, box, r, fill=None, outline=None, width=1):
        self.d.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)

    def battery(self, x, y, pct=0.72, charging=False, color=None):
        t = self.t
        c = color or t["ink2"]
        bw, bh = self.px(20), self.px(10)
        self.rrect((x, y, x + bw, y + bh), self.px(2), outline=c, width=1)
        self.d.rectangle((x + bw, y + self.px(3), x + bw + self.px(2), y + bh - self.px(3)), fill=c)
        fw = int((bw - 4) * pct)
        fc = t["green"] if charging else (t["red"] if pct < 0.15 else t["ink"])
        self.d.rectangle((x + 2, y + 2, x + 2 + fw, y + bh - 2), fill=fc)

    def bt_glyph(self, x, y, h, color):
        # simple Bluetooth rune drawn with lines (no trade-dress icon)
        w = h * 0.55
        pts = [(x, y + h * 0.25), (x + w, y + h * 0.75), (x + w / 2, y + h), (x + w / 2, y),
               (x + w, y + h * 0.25), (x, y + h * 0.75)]
        self.d.line(pts, fill=color, width=max(1, self.px(1.5)))

    def wheel_hint(self, items):
        """bottom hint line: list of (key, action)"""
        y = self.h - self.px(14)
        s = "   ".join(f"{k}: {a}" for k, a in items)
        self.text((self.w / 2, y), s, self.small, self.t["ink3"], anchor="mm")

    def status(self, title, bt=None, pct=0.72, charging=False, left=None):
        t = self.t
        sh = self.status_h
        self.d.rectangle((0, 0, self.w, sh), fill=t["bar"])
        self.d.line((0, sh, self.w, sh), fill=t["line"])
        self.text((self.w / 2, sh / 2), title, self.body_semi, t["ink"], anchor="mm", maxw=self.w * 0.6)
        if left:
            self.text((self.pad, sh / 2), left, self.small, t["ink2"], anchor="lm")
        bx = self.w - self.pad - self.px(22)
        self.battery(bx, (sh - self.px(10)) / 2, pct, charging)
        if bt:
            col = {"saved": t["ink3"], "connected": t["ink"], "streaming": t["accent"]}[bt]
            self.bt_glyph(bx - self.px(16), self.px(5), sh - self.px(10), col)

    def art(self, box, kind="placeholder"):
        t = self.t
        x0, y0, x1, y1 = box
        self.rrect(box, self.px(6), fill=t["art"])
        # subtle two-tone so it reads as "art slot" without being a logo
        self.d.polygon([(x0, y1), (x1, y0), (x1, y1)], fill=t["art2"])
        self.rrect(box, self.px(6), outline=t["line"], width=1)
        if kind == "placeholder":
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            r = (x1 - x0) * 0.22
            self.d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=(200, 208, 224), width=max(1, self.px(2)))
            self.d.ellipse((cx - r * 0.25, cy - r * 0.25, cx + r * 0.25, cy + r * 0.25), fill=(200, 208, 224))

    def list_rows(self, rows, y0=None, sel=0, x0=0, x1=None, two_line=False, scrollbar=None):
        """rows: list of dicts title, sub, right, chevron, dim, icon"""
        t = self.t
        y = self.status_h + self.px(2) if y0 is None else y0
        x1 = self.w if x1 is None else x1
        rh = self.row2 if two_line else self.row
        for i, r in enumerate(rows):
            if y + rh > self.h:
                break
            selected = (i == sel)
            if selected:
                self.rrect((x0 + self.px(3), y, x1 - self.px(3), y + rh - 1), self.r, fill=t["accent"])
            ink = t["on_accent"] if selected else (t["ink3"] if r.get("dim") else t["ink"])
            ink2 = t["on_accent"] if selected else t["ink2"]
            tx = x0 + self.pad
            if r.get("icon"):
                self.d.rounded_rectangle((tx, y + rh / 2 - self.px(6), tx + self.px(12), y + rh / 2 + self.px(6)),
                                         radius=self.px(2), outline=ink2, width=1)
                tx += self.px(18)
            right = r.get("right")
            rw = self.text_w(right, self.body if not selected else self.body) + self.px(8) if right else 0
            chev = self.px(14) if r.get("chevron") else 0
            maxw = x1 - self.pad - tx - rw - chev
            if two_line:
                self.text((tx, y + self.px(5)), r["title"], self.body, ink, maxw=maxw)
                self.text((tx, y + self.px(22)), r.get("sub", ""), self.small, ink2, maxw=maxw)
            else:
                self.text((tx, y + rh / 2), r["title"], self.body, ink, anchor="lm", maxw=maxw)
            if right:
                self.text((x1 - self.pad - chev, y + rh / 2), right, self.body, ink2, anchor="rm")
            if r.get("chevron"):
                cx = x1 - self.pad - self.px(4)
                cy = y + rh / 2
                a = self.px(4)
                self.d.line([(cx - a, cy - a), (cx, cy), (cx - a, cy + a)], fill=ink2, width=max(1, self.px(1.5)))
            y += rh
        if scrollbar:
            pos, size = scrollbar  # fractions
            sx = x1 - self.px(3)
            top = self.status_h + self.px(4)
            bot = self.h - self.px(4)
            self.d.rectangle((sx, top, sx + self.px(2), bot), fill=t["line"])
            self.d.rectangle((sx, top + (bot - top) * pos, sx + self.px(2), top + (bot - top) * (pos + size)), fill=t["ink3"])
        return y

    def progress(self, box, frac, color=None, track=None, knob=False):
        t = self.t
        x0, y0, x1, y1 = box
        self.rrect(box, (y1 - y0) / 2, fill=track or t["line"])
        if frac > 0:
            self.rrect((x0, y0, x0 + (x1 - x0) * frac, y1), (y1 - y0) / 2, fill=color or t["accent"])
        if knob:
            kx = x0 + (x1 - x0) * frac
            kr = (y1 - y0) * 1.4
            self.d.ellipse((kx - kr, (y0 + y1) / 2 - kr, kx + kr, (y0 + y1) / 2 + kr), fill=color or t["accent"])

    def toast(self, msg, kind="warn", top=True):
        t = self.t
        h = self.px(22)
        y0 = self.status_h if top else self.h - h
        fill = {"warn": t["amber_soft"], "ok": t["accent_soft"]}[kind]
        ink = t["ink"]
        self.d.rectangle((0, y0, self.w, y0 + h), fill=fill)
        self.d.line((0, y0 + h, self.w, y0 + h), fill=t["line"])
        self.text((self.w / 2, y0 + h / 2), msg, self.small_semi, ink, anchor="mm")

    def save(self, name):
        self.im.save(os.path.join(OUT, f"{name}-{self.pn}.png"))


# ---------------- screens ----------------

def home(S, theme):
    s = Screen(S, theme)
    s.status("neuPod", bt="connected", left="21:12")
    split = int(s.w * 0.56)
    rows = [dict(title="Music", chevron=1), dict(title="Podcasts", chevron=1), dict(title="Photos", chevron=1),
            dict(title="Extras", chevron=1), dict(title="Settings", chevron=1), dict(title="Shuffle Songs"),
            dict(title="Now Playing", chevron=1)]
    s.list_rows(rows, sel=0, x1=split)
    s.d.line((split, s.status_h, split, s.h), fill=s.t["line"])
    # right panel: now playing card
    px = split + s.px(12)
    aw = s.w - px - s.px(12)
    ay = s.status_h + s.px(12)
    s.art((px, ay, px + aw, ay + aw), kind="cover")
    s.text((px + aw / 2, ay + aw + s.px(10)), "Harbour Lights", s.body_semi, s.t["ink"], anchor="ma", maxw=aw)
    s.text((px + aw / 2, ay + aw + s.px(28)), "Aurora Lane", s.small, s.t["ink2"], anchor="ma", maxw=aw)
    by = ay + aw + s.px(46)
    s.progress((px, by, px + aw, by + s.px(3)), 0.38)
    s.text((px, by + s.px(8)), "▶ 1:32", s.small, s.t["ink2"])
    s.text((px + aw, by + s.px(8)), "-2:29", s.small, s.t["ink2"], anchor="ra")
    s.save("home-home")


def home_nothing(S, theme):
    s = Screen(S, theme)
    s.status("neuPod", left="21:12")
    split = int(s.w * 0.56)
    rows = [dict(title="Music", chevron=1), dict(title="Podcasts", chevron=1), dict(title="Photos", chevron=1),
            dict(title="Extras", chevron=1), dict(title="Settings", chevron=1), dict(title="Shuffle Songs"),
            dict(title="Now Playing", chevron=1, dim=1)]
    s.list_rows(rows, sel=4, x1=split)
    s.d.line((split, s.status_h, split, s.h), fill=s.t["line"])
    px = split + s.px(12)
    aw = s.w - px - s.px(12)
    cy = (s.status_h + s.h) / 2
    s.text((px + aw / 2, cy - s.px(26)), "21:12", s.big, s.t["ink"], anchor="mm")
    s.text((px + aw / 2, cy + s.px(10)), "Thu 12 Nov", s.body, s.t["ink2"], anchor="mm")
    s.text((px + aw / 2, cy + s.px(34)), "1,204 songs · 72%", s.small, s.t["ink3"], anchor="mm")
    s.save("home-idle")


def now_playing(S, theme, variant="wired"):
    s = Screen(S, theme)
    bt = "streaming" if variant == "bt" else None
    s.status("Now Playing", bt=bt, left="3 of 12")
    a = s.px(112)
    ax, ay = s.pad, s.status_h + s.px(12)
    s.art((ax, ay, ax + a, ay + a), kind="cover")
    tx = ax + a + s.px(12)
    maxw = s.w - tx - s.pad
    s.text((tx, ay + s.px(2)), "Harbour Lights", s.title, s.t["ink"], maxw=maxw)
    s.text((tx, ay + s.px(30)), "Aurora Lane", s.body, s.t["ink2"], maxw=maxw)
    s.text((tx, ay + s.px(48)), "First Light · 2019", s.small, s.t["ink3"], maxw=maxw)
    # badge row
    by = ay + a - s.px(22)
    def badge(x, label, strong=False):
        w = s.text_w(label, s.small_semi) + s.px(12)
        s.rrect((x, by, x + w, by + s.px(18)), s.px(9), fill=s.t["accent_soft"] if strong else None,
                outline=None if strong else s.t["line"])
        s.text((x + w / 2, by + s.px(9)), label, s.small_semi, s.t["accent"] if strong else s.t["ink2"], anchor="mm")
        return x + w + s.px(6)
    x = tx
    if variant == "bt":
        x = badge(x, "AAC 44.1", True)
        x = badge(x, "AirPods Pro")
    else:
        x = badge(x, "FLAC 16/44.1", True)
        x = badge(x, "Wired")
    # progress
    py = ay + a + s.px(18)
    s.progress((s.pad, py, s.w - s.pad, py + s.px(4)), 0.38)
    s.text((s.pad, py + s.px(9)), "1:32", s.small, s.t["ink2"])
    s.text((s.w - s.pad, py + s.px(9)), "-2:29", s.small, s.t["ink2"], anchor="ra")
    # centre glyphs: shuffle / repeat state, quiet
    cx = s.w / 2
    s.text((cx, py + s.px(9)), "repeat 1", s.small, s.t["ink3"], anchor="ma")
    # volume row
    vy = s.h - s.px(18)
    label = "Headset 62%" if variant == "bt" else "Volume −18 dB"
    s.text((s.pad, vy), label, s.small, s.t["ink2"], anchor="lm")
    vx = s.w * 0.45
    s.progress((vx, vy - s.px(2), s.w - s.pad, vy + s.px(2)), 0.62, color=s.t["ink2"])
    s.save("np-nowplaying" if variant == "wired" else "np-nowplaying-bt")


def seek(S, theme):
    s = Screen(S, theme)
    s.status("Now Playing", left="3 of 12")
    a = s.px(112)
    ax, ay = s.pad, s.status_h + s.px(12)
    s.art((ax, ay, ax + a, ay + a), kind="cover")
    tx = ax + a + s.px(12)
    maxw = s.w - tx - s.pad
    s.text((tx, ay + s.px(2)), "Harbour Lights", s.title, s.t["ink"], maxw=maxw)
    s.text((tx, ay + s.px(30)), "Aurora Lane", s.body, s.t["ink2"], maxw=maxw)
    # big seek time replaces badges while seeking
    s.text((tx, ay + a - s.px(30)), "1:58", s.big if s.s > 1 else s.title, s.t["amber"])
    py = ay + a + s.px(18)
    s.progress((s.pad, py, s.w - s.pad, py + s.px(6)), 0.49, color=s.t["amber"], track=s.t["amber_soft"], knob=True)
    s.text((s.pad, py + s.px(11)), "1:58", s.small, s.t["amber"])
    s.text((s.w - s.pad, py + s.px(11)), "-2:03", s.small, s.t["ink2"], anchor="ra")
    s.wheel_hint([("Scroll", "seek"), ("Select", "go"), ("Menu", "cancel")])
    s.save("np-seek")


def np_page2(S, theme):
    s = Screen(S, theme)
    s.status("Now Playing", left="3 of 12")
    a = s.px(64)
    ax, ay = s.pad, s.status_h + s.px(8)
    s.art((ax, ay, ax + a, ay + a), kind="cover")
    tx = ax + a + s.px(10)
    s.text((tx, ay + s.px(2)), "Harbour Lights", s.body_semi, s.t["ink"], maxw=s.w - tx - s.pad)
    s.text((tx, ay + s.px(20)), "Aurora Lane · First Light", s.small, s.t["ink2"], maxw=s.w - tx - s.pad)
    s.text((tx, ay + s.px(38)), "★ ★ ★ ★ ☆", s.body, s.t["accent"])
    rows = [dict(title="Shuffle", right="Songs"), dict(title="Repeat", right="Album"),
            dict(title="Gapless", right="On"), dict(title="Add to playlist", chevron=1),
            dict(title="Go to album", chevron=1), dict(title="Go to artist", chevron=1)]
    s.list_rows(rows, y0=ay + a + s.px(10), sel=0)
    s.save("np-page2")


def lib_songs(S, theme, overlay=False):
    s = Screen(S, theme)
    s.status("Songs", left="1,204")
    rows = [dict(title="Carrier Wave", sub="Beacon · Signal"), dict(title="Café Del Mar", sub="Aurora Lane · First Light"),
            dict(title="Ember", sub="The Cinders · Now 7"), dict(title="Harbour Lights", sub="Aurora Lane · First Light"),
            dict(title="Hit A", sub="Zed Zed · Hit A"), dict(title="An Unreasonably Long Song Title That Will Not Fit", sub="Beacon · Signal"),
            dict(title="Morning Rain", sub="Aurora Lane · First Light"), dict(title="Opening", sub="Aurora Lane · First Light")]
    s.list_rows(rows, sel=3, two_line=True, scrollbar=None if overlay else (0.28, 0.08))
    if overlay:
        # letter overlay during fast scroll: small, right-aligned, not covering the selection
        bw = s.px(44)
        bx = s.w - bw - s.px(14)
        by = s.status_h + s.px(14)
        s.rrect((bx, by, bx + bw, by + bw), s.px(8), fill=s.t["ink"])
        s.text((bx + bw / 2, by + bw / 2), "H", s.big if s.s == 1 else s.big, s.t["surface"], anchor="mm")
        # mini index rail
        letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        ry0, ry1 = s.status_h + s.px(6), s.h - s.px(6)
        for i, ch in enumerate(letters):
            yy = ry0 + (ry1 - ry0) * i / 25
            col = s.t["ink"] if ch == "H" else s.t["ink3"]
            if s.s == 1 and i % 2 and ch != "H":
                continue
            s.text((s.w - s.px(6), yy), ch, s.small if ch == "H" else ImageFont.truetype(F_MED, s.px(8)), col, anchor="rm")
    s.save("lib-songs-fastscroll" if overlay else "lib-songs")


def lib_album(S, theme):
    s = Screen(S, theme)
    s.status("Album", left="First Light")
    a = s.px(64)
    ax, ay = s.pad, s.status_h + s.px(8)
    s.art((ax, ay, ax + a, ay + a), kind="cover")
    tx = ax + a + s.px(10)
    s.text((tx, ay), "First Light", s.title, s.t["ink"], maxw=s.w - tx - s.pad)
    s.text((tx, ay + s.px(26)), "Aurora Lane · 2019", s.body, s.t["ink2"], maxw=s.w - tx - s.pad)
    s.text((tx, ay + s.px(46)), "3 songs · 9:41 · FLAC", s.small, s.t["ink3"])
    y = ay + a + s.px(8)
    s.d.line((s.pad, y, s.w - s.pad, y), fill=s.t["line"])
    rows = [dict(title="1   Opening", right="3:12"), dict(title="2   Morning Rain", right="2:58"),
            dict(title="3   Harbour Lights", right="3:31"), dict(title="4   Carrier Wave (AAC)", right="4:02", dim=1)]
    s.list_rows(rows, y0=y + s.px(2), sel=0)
    s.save("lib-album")


def lib_coverflow(S, theme):
    s = Screen(S, theme)
    s.status("Albums", left="7 of 31")
    cy = s.status_h + s.px(14)
    big = s.px(96)
    sm = s.px(64)
    cx = s.w / 2
    # neighbours as flat, smaller tiles (no 3D transform: cheap blits)
    for k, dx in ((-2, -1.9), (2, 1.9), (-1, -1.05), (1, 1.05)):
        sz = sm if abs(k) == 1 else s.px(48)
        x = cx + dx * big * 0.72 - sz / 2
        y = cy + (big - sz) / 2
        s.rrect((x, y, x + sz, y + sz), s.px(4), fill=s.t["art2"] if abs(k) == 1 else s.t["line"])
    s.art((cx - big / 2, cy, cx + big / 2, cy + big), kind="cover")
    s.text((cx, cy + big + s.px(10)), "First Light", s.body_semi, s.t["ink"], anchor="ma")
    s.text((cx, cy + big + s.px(28)), "Aurora Lane · 2019 · 3 songs", s.small, s.t["ink2"], anchor="ma")
    # position dots
    s.wheel_hint([("Scroll", "browse"), ("Select", "open"), ("Play", "play album")])
    s.save("lib-coverflow")


def bt_devices(S, theme):
    s = Screen(S, theme)
    s.status("Bluetooth", bt="connected")
    rows = [dict(title="AirPods Pro", right="Connected"), dict(title="WH-1000XM5", right="Saved"),
            dict(title="Flip 6", right=""), dict(title="Car audio", right=""),
            dict(title="Pixel 9 (phone)", right="", dim=1), dict(title="Scan again", right="")]
    y = s.list_rows(rows, sel=0)
    # signal bars as 3 small blocks at right (drawn after text for found devices)
    def bars(row_i, n):
        yy = s.status_h + s.px(2) + s.row * row_i + s.row / 2
        for i in range(3):
            col = (s.t["on_accent"] if row_i == 0 else s.t["ink"]) if i < n else (s.t["accent_soft"] if row_i == 0 else s.t["line"])
            bx = s.w - s.pad - s.px(14) + i * s.px(5)
            s.d.rectangle((bx, yy + s.px(4) - (i + 1) * s.px(3), bx + s.px(3), yy + s.px(4)), fill=col)
    for i, n in ((2, 3), (3, 1), (4, 2)):
        bars(i, n)
    s.text((s.w / 2, s.h - s.px(14)), "Scanning ●●○○", s.small, s.t["ink3"], anchor="mm")
    s.save("bt-devices")


def bt_dropped(S, theme):
    s = Screen(S, theme)
    s.status("Now Playing", bt="saved", left="3 of 12")
    s.toast("AirPods Pro disconnected · reconnecting…", "warn")
    a = s.px(112)
    ax, ay = s.pad, s.status_h + s.px(12) + s.px(22)
    a = min(a, s.h - ay - s.px(60))
    s.art((ax, ay, ax + a, ay + a), kind="cover")
    tx = ax + a + s.px(12)
    s.text((tx, ay + s.px(2)), "Harbour Lights", s.title, s.t["ink"], maxw=s.w - tx - s.pad)
    s.text((tx, ay + s.px(30)), "Aurora Lane", s.body, s.t["ink2"])
    s.text((tx, ay + a - s.px(18)), "Paused", s.small_semi, s.t["ink2"])
    py = ay + a + s.px(14)
    s.progress((s.pad, py, s.w - s.pad, py + s.px(4)), 0.38, color=s.t["ink3"])
    s.text((s.pad, py + s.px(9)), "1:32", s.small, s.t["ink2"])
    s.text((s.w - s.pad, py + s.px(9)), "-2:29", s.small, s.t["ink2"], anchor="ra")
    s.save("bt-dropped")


def settings(S, theme):
    s = Screen(S, theme)
    s.status("Settings")
    split = int(s.w * 0.56)
    rows = [dict(title="Display", right="Auto", chevron=1), dict(title="Click", right="Haptic"),
            dict(title="Haptics", chevron=1), dict(title="Gapless", right="On"),
            dict(title="Shuffle", right="Off"), dict(title="Repeat", right="Off"),
            dict(title="Resume", right="On"), dict(title="Bluetooth", chevron=1), dict(title="About", chevron=1)]
    s.list_rows(rows, sel=0, x1=split, scrollbar=(0.0, 0.7))
    s.d.line((split, s.status_h, split, s.h), fill=s.t["line"])
    px = split + s.px(12)
    pw = s.w - px - s.px(10)
    y = s.status_h + s.px(14)
    s.text((px, y), "Display", s.body_semi, s.t["ink"])
    y += s.px(24)
    for k, v in (("Theme", "Auto"), ("Brightness", "80%"), ("Dim after", "10 s"), ("Off after", "30 s")):
        s.text((px, y), k, s.small, s.t["ink2"])
        s.text((px + pw, y), v, s.small, s.t["ink"], anchor="ra")
        y += s.px(18)
    y += s.px(4)
    s.text((px, y), "Dark when dimmed", s.small, s.t["ink3"])
    s.save("settings-display")


def hold_lock(S, theme):
    s = Screen(S, theme)
    s.status("Now Playing", bt="streaming", left="3 of 12")
    a = s.px(112)
    ax, ay = s.pad, s.status_h + s.px(12)
    s.art((ax, ay, ax + a, ay + a), kind="cover")
    tx = ax + a + s.px(12)
    s.text((tx, ay + s.px(2)), "Harbour Lights", s.title, s.t["ink"], maxw=s.w - tx - s.pad)
    s.text((tx, ay + s.px(30)), "Aurora Lane", s.body, s.t["ink2"])
    py = ay + a + s.px(18)
    s.progress((s.pad, py, s.w - s.pad, py + s.px(4)), 0.38)
    # lock hint pill, bottom, no full-screen dim
    pw, ph = s.text_w("Locked · hold Power 2 s to unlock", s.small_semi) + s.px(24), s.px(24)
    px_ = (s.w - pw) / 2
    py_ = s.h - ph - s.px(8)
    s.rrect((px_, py_, px_ + pw, py_ + ph), ph / 2, fill=s.t["ink"])
    s.text((px_ + pw / 2, py_ + ph / 2), "Locked · hold Power 2 s to unlock", s.small_semi, s.t["surface"], anchor="mm")
    s.save("hold-locked")


def power_hold(S, theme):
    s = Screen(S, theme)
    s.status("Now Playing", left="3 of 12")
    a = s.px(112)
    ax, ay = s.pad, s.status_h + s.px(12)
    s.art((ax, ay, ax + a, ay + a), kind="cover")
    tx = ax + a + s.px(12)
    s.text((tx, ay + s.px(2)), "Harbour Lights", s.title, s.t["ink"], maxw=s.w - tx - s.pad)
    # progress ring replaced by a bar in the status bar: cheap, small area
    bh = s.px(3)
    s.d.rectangle((0, s.status_h - bh, s.w * 0.6, s.status_h), fill=s.t["accent"])
    pw, ph = s.text_w("Keep holding to lock · 10 s = off", s.small_semi) + s.px(24), s.px(24)
    px_ = (s.w - pw) / 2
    py_ = s.h - ph - s.px(8)
    s.rrect((px_, py_, px_ + pw, py_ + ph), ph / 2, fill=s.t["ink"])
    s.text((px_ + pw / 2, py_ + ph / 2), "Keep holding to lock · 10 s = off", s.small_semi, s.t["surface"], anchor="mm")
    s.save("hold-power-progress")


def text_entry(S, theme):
    s = Screen(S, theme)
    s.status("Wi-Fi password")
    # field
    fy = s.status_h + s.px(12)
    s.rrect((s.pad, fy, s.w - s.pad, fy + s.px(28)), s.px(4), fill=s.t["surface"], outline=s.t["line"])
    s.text((s.pad + s.px(8), fy + s.px(14)), "neub", s.body, s.t["ink"], anchor="lm")
    cx0 = s.pad + s.px(8) + s.text_w("neub", s.body) + 1
    s.d.line((cx0, fy + s.px(6), cx0, fy + s.px(22)), fill=s.t["accent"], width=max(1, s.px(1.5)))
    # wheel ring: letters in an arc; current large in the middle
    ring_y = fy + s.px(28) + s.px(50)
    letters = "xyzabcdefg"
    n = len(letters)
    mid = n // 2
    letters = letters[:mid] + letters[mid:mid + 1] + letters[mid + 1:]
    def fnt(i):
        dist = abs(i - mid)
        return (s.big, s.t["accent"]) if dist == 0 else ((s.body, s.t["ink2"]) if dist < 3 else (s.small, s.t["ink3"]))
    widths = [s.text_w(ch, fnt(i)[0]) + s.px(14) for i, ch in enumerate(letters)]
    total = sum(widths)
    x = s.w / 2 - total / 2
    for i, ch in enumerate(letters):
        f, col = fnt(i)
        s.text((x + widths[i] / 2, ring_y), ch, f, col, anchor="mm")
        x += widths[i]
    # mode chips
    cy = ring_y + s.px(38)
    x = s.pad
    for lbl, on in (("abc", True), ("ABC", False), ("123", False), ("#!?", False), ("space", False), ("⌫", False)):
        w = s.text_w(lbl, s.small_semi) + s.px(12)
        s.rrect((x, cy, x + w, cy + s.px(18)), s.px(9), fill=s.t["accent_soft"] if on else None, outline=None if on else s.t["line"])
        s.text((x + w / 2, cy + s.px(9)), lbl, s.small_semi, s.t["accent"] if on else s.t["ink2"], anchor="mm")
        x += w + s.px(6)
    s.wheel_hint([("Select", "type"), ("◁▷", "mode"), ("Menu", "delete"), ("Play", "done")] if s.s > 1 else [("Select", "type"), ("Menu", "delete"), ("Play", "done")])
    s.save("in-text")


def files(S, theme):
    s = Screen(S, theme)
    s.status("Files", left="/music")
    rows = [dict(title="Albums", chevron=1, icon=1), dict(title="docs", chevron=1, icon=1), dict(title="many", right="256+", chevron=1, icon=1),
            dict(title="cover.jpg", right="1 B", icon=1), dict(title="readme.txt", right="20 B", icon=1),
            dict(title="track01.flac", right="120 KB", icon=1)]
    s.list_rows(rows, sel=0, scrollbar=(0.0, 0.5))
    s.save("files-browser")


def licences(S, theme):
    s = Screen(S, theme)
    s.status("Licences")
    rows = [dict(title="neuPod firmware", right="GPL-3.0"), dict(title="LVGL", right="MIT"), dict(title="Montserrat", right="OFL 1.1"),
            dict(title="Lucide icons", right="ISC"), dict(title="libFLAC", right="BSD-3"), dict(title="minimp3", right="CC0"),
            dict(title="BTstack", right="BSD")]
    s.list_rows(rows, sel=1)
    s.save("settings-licences")


def playlists(S, theme):
    s = Screen(S, theme)
    s.status("Playlists")
    rows = [dict(title="On-The-Go", right="12", chevron=1), dict(title="Recently added", right="48", chevron=1),
            dict(title="Most played", right="25", chevron=1), dict(title="Unplayed", right="310", chevron=1),
            dict(title="Night drive.m3u", right="18", chevron=1), dict(title="Running.m3u", right="40", chevron=1)]
    s.list_rows(rows, sel=0)
    s.save("lib-playlists")


def volume_hud(S, theme):
    s = Screen(S, theme)
    s.status("Songs", left="1,204")
    rows = [dict(title="Carrier Wave", sub="Beacon · Signal"), dict(title="Café Del Mar", sub="Aurora Lane · First Light"),
            dict(title="Ember", sub="The Cinders · Now 7"), dict(title="Harbour Lights", sub="Aurora Lane · First Light"),
            dict(title="Hit A", sub="Zed Zed · Hit A")]
    s.list_rows(rows, sel=3, two_line=True)
    # bottom HUD strip (small area), appears on volume change from any screen
    h = s.px(26)
    y0 = s.h - h
    s.d.rectangle((0, y0, s.w, s.h), fill=s.t["ink"])
    s.text((s.pad, y0 + h / 2), "Volume −18 dB", s.small_semi, s.t["surface"], anchor="lm")
    s.progress((s.w * 0.45, y0 + h / 2 - s.px(2), s.w - s.pad, y0 + h / 2 + s.px(2)), 0.62, color=s.t["surface"], track=s.t["ink2"])
    s.save("np-volume-hud")


def empty_state(S, theme):
    s = Screen(S, theme)
    s.status("Podcasts")
    cy = (s.status_h + s.h) / 2
    s.text((s.w / 2, cy - s.px(16)), "No podcasts yet", s.title, s.t["ink"], anchor="mm")
    s.text((s.w / 2, cy + s.px(10)), "Copy episodes to /podcasts on the card,", s.small, s.t["ink2"], anchor="mm")
    s.text((s.w / 2, cy + s.px(26)), "then Music › Update Library.", s.small, s.t["ink2"], anchor="mm")
    bw, bh = s.px(120), s.px(22)
    bx, by = (s.w - bw) / 2, cy + s.px(44)
    s.rrect((bx, by, bx + bw, by + bh), bh / 2, fill=s.t["accent"])
    s.text((bx + bw / 2, by + bh / 2), "Update Library", s.small_semi, s.t["on_accent"], anchor="mm")
    s.save("lib-empty")


def busy_state(S, theme):
    s = Screen(S, theme)
    s.status("Update Library")
    cy = (s.status_h + s.h) / 2
    s.text((s.w / 2, cy - s.px(28)), "Building library", s.title, s.t["ink"], anchor="mm")
    s.progress((s.w * 0.15, cy - s.px(4), s.w * 0.85, cy + s.px(4)), 0.46)
    s.text((s.w / 2, cy + s.px(18)), "552 of 1,204 songs · /music/Aurora Lane", s.small, s.t["ink2"], anchor="mm", maxw=s.w * 0.9)
    s.wheel_hint([("Menu", "run in background"), ("Select", "cancel")])
    s.save("lib-building")


ALL = [home, home_nothing, lambda S, t: now_playing(S, t, "wired"), lambda S, t: now_playing(S, t, "bt"), seek, np_page2,
       lambda S, t: lib_songs(S, t, False), lambda S, t: lib_songs(S, t, True), lib_album, lib_coverflow, lib_playlists if False else playlists,
       bt_devices, bt_dropped, settings, hold_lock, power_hold, text_entry, files, licences, volume_hud, empty_state, busy_state]

if __name__ == "__main__":
    for prof in PROFILES:
        for fn in ALL:
            fn(prof, LIGHT)
    # dark variants of the two signature screens
    for prof in PROFILES:
        s = None
        for fn, name in ((lambda S, t: now_playing(S, t, "wired"), "np-nowplaying"), (home, "home-home")):
            fn(prof, DARK)
            os.rename(os.path.join(OUT, f"{name}-{prof}.png"), os.path.join(OUT, f"{name}-dark-{prof}.png"))
        # re-render light so the light files exist too
        now_playing(prof, LIGHT, "wired"); home(prof, LIGHT)
    print(sorted(os.listdir(OUT)))
