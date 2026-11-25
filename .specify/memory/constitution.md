# neuPod constitution

The principles every part of neuPod follows: firmware, hardware, the phone app and the tools. They outrank any single
plan, spec or PR. Specs, plans and reviews are checked against them (Spec Kit's `/speckit.analyze` reads this file through
`.specify/memory/constitution.md`). The detail behind each principle lives in the linked documents. Changing a principle
needs the owner's decision, recorded here with the date.

## I. One player UI everywhere

Every neuPod device and app shares one player: screens, navigation, the wheel and its acceleration, the letter jump, lists
and sort order, the text shown, Now Playing, album art and Cover Flow.

- Differences are allowed only for hardware or platform reasons: screen size, input, memory, radios, and OS services.
- Each difference is a setting or build option, not a redesign.
- A UI change lands on every target, or the gap is logged in the backlog.

[ADR-0022](adr/0022-one-player-ui.md).

## II. The firmware is the reference

`app/` defines how neuPod behaves. Every other implementation ports it exactly and proves the port against the compiled C
with vectors, snapshots or goldens. Nothing is approximated. Where the firmware is silent, the owner decides, and the decision
is written down for every target.

## III. The owner's music and devices are safe

- The phone app never writes to, moves or deletes a music file.
- The firmware writes to the card only under `/.neupod`.
- Nothing irreversible happens without the owner's say-so: replacing firmware that isn't ours, repartitioning, dropping
  data, force-pushing.
- On the Y2, the internal eMMC (`/dev/mmcblk0*`) and NVRAM are never touched.
- Only the owner flashes partitions.

[skill rules](https://github.com/StuartF303/neuPod/blob/main/.claude/skills/neupod/SKILL.md).

## IV. Fit the hardware

- Every feature states its RAM budget.
- The Pico 2 W is frozen at its current RAM, so any change reports its size deltas.
- Memory is a valid reason for a device to do less (principle I), never a reason to do it differently.

[ADR-0001 amendment](adr/0001-rp2350-phase1-platform.md), [ADR-0013](adr/0013-bluetooth-ram-psram.md).

## V. Sound is not compromised

- Wired and digital paths are lossless from end to end.
- Audio is decoded once, and resampled only when the output requires it.
- Playback is gapless.

[ADR-0003](adr/0003-audio-quality-policy.md), [ADR-0016](adr/0016-gapless-playback-and-play-queue.md).

## VI. Open, private and the owner's own

- Firmware is MPL-2.0, hardware CERN-OHL-S-2.0 and documentation CC BY 4.0.
- Third-party code keeps its licence in `LICENSE.md` and on the Licences screens. No vendor demo code is copied.
- No Apple marks or artwork in our products.
- The phone app makes no network connection in R1, with no analytics, crash reporting or ads, ever.
- Nothing personal is committed or sent to an outside service: no keys, music, owner details or private testbed notes.

## VII. Verified, reviewed, then merged

Every change goes through the same steps:

1. **Implement.**
2. **Verify:** tests, plus the bench or the phone where hardware matters.
3. **Review:** by someone who did not write it, against the spec and this constitution.
4. **Merge:** squash-merge once CI is green.

"Green" means tests really ran: counts are checked, and skips don't count. Branches are short-lived and are never
force-pushed or rebased.

[CONTRIBUTING](https://github.com/StuartF303/neuPod/blob/main/CONTRIBUTING.md).

## VIII. Decisions are written down

- Architecture choices go in an ADR.
- App choices go in `docs/app/decisions/`.
- Owner decisions go in the spec they settle, with the date.
- What is done goes in `CHANGELOG.md` and the progress checklists.
- Hardware results go in the feature's doc, with the date.

## Amendments

| Date | Change |
|---|---|
| 2026-11-25 | First version (owner). Principle I from ADR-0022 (24 Nov 2026); the rest gathered from the ADRs, CONTRIBUTING, the app team brief and the bench rules. |
