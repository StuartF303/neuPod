#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Builds index.html for the gate 2 set in OUT_DIR (sim/ and renders/ PNGs).

Usage: make_index.py OUT_DIR
Standalone page: relative image paths, plain CSS, no scripts, no external files.
"""
import html
import os
import sys

out = sys.argv[1]
STYLES = [("classic_light", "Classic Light"), ("modern_dark", "Modern Dark")]
PHONES = [
    ("iphone-16e", "iPhone 16e (390x844 pt, smallest)"),
    ("iphone-16", "iPhone 16 (393x852 pt, standard)"),
    ("iphone-16-pro-max", "iPhone 16 Pro Max (440x956 pt, largest)"),
]
VARIANTS = [
    ("base", "Fit A, mode's own body, status bar, wordmark (the defaults)"),
    ("fit-b", "Fit B: fixed proportions on a dark surround"),
    ("other-body", "Other body: black under Classic Light, silver under Modern Dark"),
    ("no-statusbar", "Phone status bar hidden"),
    ("no-wordmark", "Wordmark off"),
]
RENDERS = [
    ("home-idle", "Home, nothing playing (idle pane: placeholder cover, 'Nothing playing', song count)"),
    ("home-playing", "Home, playing"),
    ("music-menu", "Music menu (Albums selected; preview pane)"),
    ("songs", "Songs, with the selection"),
    ("songs-fast-scroll", "Songs, fast turn: letter tile and index rail"),
    ("artist", "An artist (All Songs, then albums by year)"),
    ("album-discs", "An album with disc headings"),
    ("cover-flow", "Cover Flow"),
    ("now-playing", "Now Playing at rest. Stars candidate 1: no stars here, a Rating row on page 2 only"),
    ("now-playing-stars", "Now Playing at rest. Stars candidate 2: stars also under the format line"),
    ("now-playing-volume", "Now Playing while the wheel sets the volume"),
    ("now-playing-seek", "Now Playing, seek mode (hold the centre 500 ms)"),
    ("now-playing-page2", "Now Playing, page 2 (Rating row = candidate 1)"),
    ("panel", "The centred panel"),
    ("search-typing", "Search, typing 'sal' with live results"),
    ("search-results", "Search, results (Play/Pause)"),
    ("empty-list", "An empty list ('None yet')"),
]
TIERS = [("", "7 rows (standard)"), ("-large", "6 rows (large)"), ("-largest", "5 rows (largest)")]

css = """
body{font:15px/1.45 -apple-system,Helvetica,Arial,sans-serif;margin:24px;color:#1c1c1e;background:#f4f4f6;max-width:1500px}
h1{margin:0 0 4px}h2{margin:40px 0 8px;border-bottom:2px solid #c8c8cc;padding-bottom:4px}h3{margin:24px 0 6px}
.row{display:flex;gap:18px;align-items:flex-start;overflow-x:auto;padding-bottom:8px}
figure{margin:0;flex:0 0 auto}
img{display:block;height:560px;width:auto;border:1px solid #bbb;border-radius:6px;background:#fff}
.tall img{height:620px}
figcaption{max-width:260px;font-size:13px;margin-top:4px;color:#333}
table{border-collapse:collapse;margin:8px 0}td,th{border:1px solid #bbb;padding:4px 8px;vertical-align:top;font-size:14px}th{background:#e6e6ea;text-align:left}
code{background:#e6e6ea;padding:0 3px}.note{background:#fff;border-left:4px solid #888;padding:6px 12px;margin:8px 0}
"""

def fig(path, cap):
    if not os.path.exists(os.path.join(out, path)):
        return ""
    return f'<figure><a href="{path}"><img src="{path}" alt="{html.escape(cap)}"></a><figcaption>{html.escape(cap)}</figcaption></figure>'

def row(figs, cls=""):
    figs = [f for f in figs if f]
    return f'<div class="row {cls}">' + "".join(figs) + "</div>" if figs else ""

p = []
p.append(f"<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'><title>neuPod app: gate 2 screenshots</title><style>{css}</style></head><body>")
p.append("<h1>neuPod app: gate 2</h1><p>Candidates side by side for the owner to choose from. Click an image for full size. "
         "Part A is the real app on iOS simulators (real status bar, Dynamic Island or notch, home indicator). "
         "Part B is exact renders at 393x852 pt (drawn at 2x, Montserrat bundled, safe area 59 pt top / 34 pt bottom). "
         "Library names come from the bundled gate manifest; covers are placeholders. Mock-ups in "
         "<code>docs/design/2026-11-21-app-device-view/mockups</code> are design intent, not app output.</p>")
p.append("<h2>Behaviours, not pictures (felt at gate 1 on the phone)</h2><ul>"
         "<li><b>Touch candidate 1 (wheel only):</b> the screen is display only; every action goes through the wheel, as on the device.</li>"
         "<li><b>Touch candidate 2 (wheel plus screen, recommended):</b> you can also tap a row to open it (it flashes a pressed tint) and drag on the screen; one-line rows are under 44 pt at the default text size (OQ2).</li>"
         "<li><b>Drag-to-seek:</b> off, you can only seek by holding the centre (seek mode); on, you can also drag the Now Playing progress bar. The spec says draggable, the design says not; both are built behind a gate switch.</li>"
         "<li><b>Gate switches:</b> long-press the device body to open the panel with fit, body, touch, status bar, wordmark, drag-to-seek and rating placement.</li></ul>")

p.append("<h2>A. iOS simulator: fit, body, status bar, wordmark</h2>")
for slug, pname in PHONES:
    p.append(f"<h3>{html.escape(pname)}</h3>")
    for scene, sname in [("home", "Home"), ("nowplaying", "Now Playing")]:
        for sty, styname in STYLES:
            figs = [fig(f"sim/sim-{slug}-{scene}-{sty}-{v}.png", f"{styname}, {sname}: {vc}") for v, vc in VARIANTS]
            if any(figs):
                p.append(f"<p><b>{sname}, {styname}</b></p>" + row(figs))

p.append("<h2>B. Exact renders, 393x852</h2>")
for key, cap in RENDERS:
    figs = [fig(f"renders/render-{key}-{sty}.png", f"{styname}: {cap}") for sty, styname in STYLES]
    if any(figs):
        p.append(f"<p><b>{html.escape(cap)}</b></p>" + row(figs, "tall"))
p.append("<h3>Text tiers (7, 6 and 5 rows)</h3>")
for key, name in [("home-playing", "Home"), ("songs", "Songs")]:
    for sty, styname in STYLES:
        figs = [fig(f"renders/render-{key}-{sty}{suf}.png", f"{styname}, {name}: {t}") for suf, t in TIERS]
        if any(figs):
            p.append(f"<p><b>{name}, {styname}</b></p>" + row(figs, "tall"))

p.append("""<h2>C. Differences and choices</h2>
<p>The build follows the specification where the mock-ups differ; the owner decides.</p>
<h3>Mock-ups versus the specification</h3><ul>
<li><b>Home items:</b> mock-ups show the firmware's Podcasts, Photos and Extras and no Playlists; the spec's Home is Music, Playlists (a stand-in screen for now), Settings, Shuffle Songs, plus Now Playing while something is loaded.</li>
<li><b>Wi-Fi icon:</b> some mock-ups show one; the design README says R1 omits it. Omitted.</li>
<li><b>Rating stars:</b> the mock-ups have none; the spec requires them. Two placements are built: candidate 1 a Rating row on page 2 only, candidate 2 also stars under the format line at rest (display only; set on page 2).</li>
<li><b>Album header:</b> the mock-up shows the disc count; the spec asks for song count and total time. The build lacks the total time in the header.</li>
<li><b>Seek:</b> the spec says drag the progress bar; the design says seek mode only. Both built, switchable (default: seek mode only).</li>
<li><b>Music-menu preview pane</b> for items other than Albums is this build's own; Modern recolours from the album on screen in Cover Flow or the album view, else the playing track (OQ5 as drawn); album rows show thumbnails.</li></ul>
<h3>Contrast and colour</h3><ul>
<li><b>Classic Light, the designer's exact values fall short:</b> white text over the selection bar's upper gradient stops (2.2 to 4.0:1), rail letters #a0a0a0 (2.6:1), ink3 on grey surfaces (4.1:1), and Bluetooth saved versus active differ mostly by colour. Built as designed so you can judge.</li>
<li><b>Modern accent is darker than the mock-ups:</b> the rule (4.5:1 against the glossed colour) is applied. The design's own example #b8502c is 4.97:1 plain and only 4.04:1 under the gloss, so app accents are a little darker. Dark covers can make Modern's selection bar hard to see. Where the design is silent: no-art accent #2868d8, the progress tint rule, a missing-cover square, the rating-star style are this build's choices.</li></ul>
<h3>Behaviour choices</h3><ul>
<li><b>Search text is capped at 24 characters</b> so the caret never clips; the firmware default is longer. Keep, raise or scroll the field? Search also has six chips (including #!?) as the firmware, where the design has five.</li>
<li><b>Home idle pane</b> (nothing playing): placeholder cover, 'Nothing playing' and the song count (UI styles OQ9, choose).</li>
<li><b>Cover Flow:</b> tapping a side cover centres it. The angled Classic Cover Flow has no performance test yet.</li>
<li><b>Settings</b> has only Style and Mode for now. Empty lists show a greyed 'None yet' row (firmware wording).</li>
<li><b>Letter tile:</b> appears after 6 steps without a 400 ms gap and hides 250 ms after rest (the code decides over the docs).</li></ul>
<h3>Device view design, Open questions and what is shown meanwhile</h3><table><tr><th>#</th><th>Question</th><th>Shown meanwhile</th></tr>
<tr><td>1</td><td>Fit A or B</td><td>Both (Part A); screens use A</td></tr>
<tr><td>2</td><td>Direct touch: wheel only, or plus the screen (one-line rows under 44 pt)</td><td>Both switchable; 2 default</td></tr>
<tr><td>3</td><td>Text beyond the largest tier (capped at 5 rows): acceptable, or a plain reflowing list?</td><td>Capped</td></tr>
<tr><td>4</td><td>Holding Play/Pause: should it do anything?</td><td>Nothing (a long hold acts as a press on release)</td></tr>
<tr><td>5</td><td>Body setting: automatic only, or Silver/Black too</td><td>Automatic; both cross-combinations in Part A</td></tr>
<tr><td>6</td><td>Proportions: our wider screen, or nearer the real device (about 0.82 W)</td><td>Our own</td></tr>
<tr><td>7</td><td>AirPlay and other routes' icons</td><td>Not drawn; speaker shows no icon</td></tr>
<tr><td>8</td><td>Wordmark on the body</td><td>On, small and low-contrast; off variants in Part A</td></tr>
<tr><td>9</td><td>Left-edge swipe as Menu on iOS</td><td>Menu</td></tr>
<tr><td>10</td><td>Click sound through headphones or speaker only</td><td>System sound, follows the ringer switch (off by default)</td></tr>
<tr><td>11</td><td>Search placement: Music menu or Home</td><td>Music menu</td></tr>
<tr><td>12</td><td>Classic Dark and Modern Light screens (undrawn)</td><td>Bodies only</td></tr>
<tr><td>13</td><td>Search: three live results (two, then one at larger tiers), fewer and larger, or none until Play/Pause</td><td>Three</td></tr></table>
<p class="note">Not captured: the simulator's text sizes (Part B shows the three tiers); Android; real wheel feel, haptics and system volume (gate 1, on the phone).</p>
</body></html>""")
open(os.path.join(out, "index.html"), "w").write("\n".join(p))
