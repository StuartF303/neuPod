# Donor teardown — iPod Classic 6G A1238 (5 Nov 2026)

Photos by Stuart, 5 Nov 2026 (original set: [workphotos report](https://reports.workphotos.com/6aec8184382a8749d5cac1fc)). Scale in the photos is a RasPiO GPIO ruler (cm). Board markings: Apple **820-2437**, "08/10/15 REV:06", © 2008; wheel sub-board **821-0545-A** © 2007; battery flex Foxconn © 2008; cell label GS837630F0953, Li-ion.

## Findings that change the design

1. **The mid-frame is ribbed.** The logic board is screwed to the back of a cast frame whose cross-ribs divide the board area into about six pockets (a 2 × 3 grid, each roughly 25 × 18 mm). Component height under a rib is the board-to-rib gap (≈ 1 mm by eye); only the pockets take tall parts. **Placement must follow the pocket grid** — rib positions and heights are the first targets for the 3D scan.
2. **The battery lives in the back shell**, stuck to the shell with its flex running down to the board's bottom edge. It does not compete with the board for depth: the board side has the frame plus the whole HDD cavity (the foam-wrapped 1.8" disk sits over the board in the front half). Without the HDD there is more depth on the board side than the "~6 mm thin" open-source estimate, and a thicker cell can go in the shell.
3. **Headphone jack + hold switch are one flex** screwed into the top-left corner of the back shell, with a long flex to a small connector at the board's bottom-right. Reusable as-is ([ADR-0018](adr/0018-headphone-jack-reuse.md)).
4. **Connectors on the board**: wide ZIF for the LCD (top-middle), narrow ZIF for the wheel (top-right, ~8–10 conductors), headphone/hold flex connector (bottom-right), battery connector (bottom-left), 40-pin HDD ZIF and the 30-pin dock along the bottom edge. Pitches and pin counts still to be measured.
5. **Wheel sub-board 821-0545-A**: five dome switches on the rear, flex tail on the left edge; the frame has a window for it.
6. Scale check: front plate ≈ 103 mm, board width ≈ 55 mm — consistent with the open-source figures in [Requirements](requirements.md#reused-parts-and-mechanical).

## Measurements still needed (before/with the scan)

- Rib positions and height above the board; board thickness; pocket dimensions.
- LCD ZIF and wheel ZIF pitch and pin count (count conductors over a known length with a loupe).
- Headphone/hold flex connector pitch and pin count; battery connector type.
- Battery pouch outline and thickness; shell-side free depth.
- Screw positions (board-to-frame, frame-to-shell).

## Photos

| | |
| --- | --- |
| ![Opened: back shell with battery and jack flex; front half with foam-wrapped HDD](assets/teardown/03-opened-back-shell-and-hdd.jpg) | ![Back shell: battery, jack/hold flex route](assets/teardown/05-back-shell-battery-flex.jpg) |
| ![Jack and hold switch flex, top-left corner](assets/teardown/04-jack-hold-flex-corner.jpg) | ![Front assembly: LCD and wheel 821-0545-A](assets/teardown/06-front-lcd-wheel.jpg) |
| ![Logic board 820-2437 behind the ribbed mid-frame](assets/teardown/07-logic-board-in-midframe.jpg) | ![Logic board in mid-frame with ruler](assets/teardown/10-logic-board-in-midframe-ruler.jpg) |
| ![Bottom edge: 30-pin dock, HDD ZIF flex](assets/teardown/08-bottom-edge-dock-hdd-zif.jpg) | ![Wheel sub-board rear: five dome switches](assets/teardown/09-wheel-subboard-rear.jpg) |
| ![Front assembly edge](assets/teardown/01-front-assembly-edge.jpg) | ![Mid-frame edge against the ruler](assets/teardown/02-midframe-edge-ruler.jpg) |
| ![Mid-frame edge, second view](assets/teardown/11-midframe-edge-2.jpg) | |
