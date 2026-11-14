# UI design review, 12 November 2026

An owner-commissioned design review of the firmware UI (164 simulator screenshots reviewed). This folder holds the review as delivered:

- [REPORT.md](REPORT.md): findings, the design system (colour tokens, type scale, layout metrics, icons, motion), a proposal per screen and the backlog (`DS-*`, `NP-*`, `LB-*`, `BT-*`, `ST-*`, `IN-*`, `HL-*`, `FL-*`, `SP-*`).
- `mockups/`: every proposed screen at 320x240 and 480x360 (`<area>-<screen>-<WxH>.png`).
- `render.py`: regenerates the mock-ups; every length comes from the metric table, so it doubles as a layout reference.

The backlog is tracked in [Backlog](../../backlog.md#design-review-2026-11-12); the implemented design system is described in [UI mock-ups](../../ui.md#design-system-2026-11-12-review).

The mock-ups are drawn with Inter as a stand-in. **Implementations use Montserrat Medium** (the fonts the firmware already builds), which runs about 4 % wider; the review left that margin.

## Owner decisions (12 November 2026)

Where the review conflicts with earlier decisions, these win, in every phase:

1. **Now Playing.** Select opens page 2; **hold Select** enters seek (the wheel seeks, Select commits, Menu cancels). This supersedes the earlier "Select toggles volume / seek". Built in phase 2.
2. **Bluetooth link lost.** ADR-0019 stands: playback pauses and stays on Bluetooth; the firmware never switches the output by itself. The review's "Couldn't reconnect, playing on wired" toast becomes **"Couldn't reconnect — paused"**.
3. **Theme.** Light by default. Settings > Display > Theme is Light / Dark / Auto; Auto means dark once the backlight dims.
4. **Order.** Phase 1 foundation (`DS-*`, `HL-1`), phase 2 Now Playing and library rows / fast scroll, phase 3 Bluetooth screens, phase 4 the rest.
5. **Platform.** The Pico 2 W is frozen ([ADR-0001 amendment](../../adr/0001-rp2350-phase1-platform.md#amendment-12-november-2026-the-pico-2-w-is-frozen-the-product-target-is-the-rp2350b-with-psram)). Design for the RP2350B with PSRAM and the Y2; ignore the review's "Pico 2 W" fallbacks, but keep that build compiling.
6. **Licences.** Standing rule: every third-party asset added (Lucide icons, ISC; new font sizes, OFL) goes into [LICENSE.md](https://github.com/StuartF303/neuPod/blob/main/LICENSE.md) and the on-device Licences screen (`app/ui/ui_licence_data.c`); the `licences` ctest checks they agree.
7. **Dark accent.** The dark theme's accent is `#2868D8` (RGB565-safe, about 5:1 with white text); the review's `#3078E8` gave 4.2:1. The contrast test asserts at least 4.5 in both themes.
8. **Toast height.** The toast strip covers exactly one list row (30 px at 320x240, 45 px at 480x360), not 22 / 33 px.
9. **Auto theme stays as built.** Auto is dark while the backlight is dimmed or off and light otherwise. With Auto-dim turned off the backlight never dims, so Auto behaves like Light (until the screen goes off).
10. **Text that does not fit scrolls (supersedes the review's "always an ellipsis").** One-line text wider than its box runs as a ticker: it waits 1 s, then scrolls round at about 35 px/s (52 px/s on 480x360) with a small gap between repeats, and only while it overflows. Where it applies: the Home track title and artist, the **focused** list row's main text (unfocused rows keep the ellipsis, like classic wheel players), the status-bar title; and, as a standing rule, Now Playing title / artist / album in phase 2 and any later one-line text. It stops and rewinds when focus leaves the row, and stands still while the screen is off. Tours snapshot every ticker at its start position.
