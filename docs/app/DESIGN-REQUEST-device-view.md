# Design request: the app's device view

For the designer. Context first, then the brief.

> **Superseded in part (22 November 2026):** Search no longer opens the phone's keyboard; it is typed with the
> wheel (owner decision 12 in [README.md](README.md)). The statements about the keyboard below are kept as the
> original request.

Project: neuPod, a public open-source repo: https://github.com/StuartF303/neuPod
Read the app spec from branch `docs/app-phase0` (PR #101, merging soon; after that `main` has the same files) and everything
else from `main`. If you can clone, clone the repo and check out `docs/app-phase0`. If you can only fetch URLs, use these raw links.

App spec (branch docs/app-phase0):
- https://raw.githubusercontent.com/StuartF303/neuPod/docs/app-phase0/docs/app/README.md
- https://raw.githubusercontent.com/StuartF303/neuPod/docs/app-phase0/docs/app/TEAM-BRIEF.md

Design template to follow: the UI styles design (main):
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/README.md
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-album.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-artist-albums.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-cover-flow.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-feedback-panel.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-feedback-row.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-feedback-status.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-home.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-music-menu.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-now-playing-volume.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-now-playing.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-play-states.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-songs.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/classic-status-states.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-album.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-artist-albums.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-cover-flow.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-feedback-panel.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-feedback-row.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-feedback-status.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-home.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-music-menu.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-now-playing-volume.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-now-playing.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-play-states.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-songs.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/modern-status-states.html
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/design/2026-11-21-ui-styles/mockups/play-glyph-options.html

Firmware behaviour and the device (main):
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/ui.md
- https://raw.githubusercontent.com/StuartF303/neuPod/main/docs/adr/0004-donor-ipod-classic-6g.md

The PNG of each mock-up has the same path as its .html with .png instead.

---

You are the designer for neuPod, an open-source music player (repo: StuartF303/neuPod, public). You already
produced the firmware UI styles design in `docs/design/2026-11-21-ui-styles/` (README with exact values, HTML
mock-ups at 600x450 and their PNGs). Now design the **phone app's device view**: the layout framework the app team
will build the iOS and Android app on. Work in the same way and the same format as that folder.

## Read first

- `docs/app/README.md`: the app specification, especially owner decisions 9 to 11 ("The app is the device", wheel
  volume, sort rule), Look and feel, Browsing, Now Playing, Accessibility, and Open questions 1 and 3.
- `docs/design/2026-11-21-ui-styles/README.md` and its `mockups/`: Classic Light and Modern Dark, the status bar, the
  three feedback places, the music screens. These run unchanged inside the app's 4:3 screen.
- `docs/ui.md` (wheel behaviour, acceleration, fast scroll, Now Playing seek and volume as built in the firmware) and
  `docs/adr/0004-donor-ipod-classic-6g.md` (the physical device's shell).

## The owner's decision (21 November 2026)

The app's full-screen view is the front of a neuPod: the 4:3 screen above a touch click wheel with the four click
buttons (Menu, previous, next, play/pause) and the centre button, in the device's proportions, scaled to the phone's
width. Turning the wheel scrolls with haptic detents, and presses give a haptic click. On Now Playing the wheel sets
the phone's system volume and shows the device's volume bar. Search opens the phone's keyboard.

## Hard rules

- **Drawn fresh.** No Apple logo, no "iPod" anywhere, nothing traced or copied from Apple's product photography or
  artwork. Our own geometry, our own rendering, the neuPod name. The button glyphs are Lucide (ISC) or drawn for this
  design; the type is Montserrat (SIL OFL).
- The screen content is the existing UI styles design: do not redesign the screens, only how they sit in the device.
- Contrast at least 4.5:1 for text; nothing by colour alone; touch targets at least 44 pt.

## What to produce

A new folder, `docs/design/<date>-app-device-view/`, with a README giving decisions, rules and **exact values**, plus
`mockups/` as standalone HTML and PNG. Draw at three phone sizes in points: 375x667 (short, home button),
393x852 (common), 440x956 (large), plus the largest-text variant where it matters.

1. **Device geometry.** Body outline, corner radii, and the 4:3 screen rectangle with its bezel. All expressed as
   ratios of the phone's width so the app can lay it out at any size. State the scale from the 600x450 screen
   designs to the on-phone screen, and the minimum the screen may shrink to.
2. **Fitting the phone.** How the device sits on tall and on short phones, with the safe areas, notch, Dynamic Island,
   home indicator and Android gesture bar. Draw **two candidates**: the body stretched to fill the height, and the
   device at its true proportions with the surround filled. Say what the surround is in each style and mode.
3. **Body finishes.** One per style and mode drawn (Classic Light, Modern Dark), and a rule for the missing ones
   (Classic Dark and Modern Light are not designed yet; UI styles Open question 1). Whether the body follows the
   style and mode, or is a separate setting.
4. **The click wheel.** Diameter and centre-button diameter as ratios. The four button positions and glyphs. The hit
   zones, including the dead band between the ring and the centre, and the angle that counts as a press versus a
   turn. Visual states: idle, finger down on the ring, button pressed, centre pressed, disabled.
5. **Wheel behaviour, as numbers the engineers can implement.**
   - Degrees per detent, i.e. per list step.
   - The acceleration curve matching the firmware's, and when the fast-scroll letter tile appears.
   - Press-and-hold times for previous and next (scan) and for play/pause.
   - Which haptic fires on what: detent tick vs press click, and the iOS and Android haptic styles to use.
   - The optional click sound.
   - What a two-finger or edge touch does.
6. **Direct touch on the screen.** Two candidates: the wheel only, or the wheel plus tapping rows and dragging lists on
   the screen. Draw how each looks when used, and give your recommendation.
7. **Screens in the device.** Home, Songs (a long list with the letter tile), an album with disc headings, Cover Flow,
   Now Playing (at rest, while the wheel sets volume, and in seek), a centred panel, and Search with the keyboard up
   (where the device goes when the keyboard covers the wheel). Each in Classic Light and Modern Dark, at 393x852,
   using real names from the existing mock-ups.
8. **Largest text size.** How the 4:3 screen honours the phone's largest standard text setting without clipping.
   The proposal is fewer, larger rows; draw it and give the row counts per size.
9. **Accessibility.** How VoiceOver and TalkBack users drive it: row-by-row navigation, and the wheel as an adjustable
   control with named actions for Select, Menu, previous, next and play/pause. The labels to use.
10. **The phone's own status bar** vs the device's status bar: whether the phone's is hidden, shown above the device,
    or merged. Recommend one.

End the README with **Open questions**, as the UI styles README does: anything for the owner to decide, each with
what you drew meanwhile. Do not settle owner questions by guessing.

## Out of scope

App code, the firmware, the store listing, and R2 and R3. When you're done, the app team builds the design-gate
screens from this folder (`docs/app/TEAM-BRIEF.md`, gate 2) and the owner chooses between your candidates on real
screens.

Branch `docs/app-device-view`, a commit with scope `design` or `app` per `CONTRIBUTING.md`, and a PR to main.
