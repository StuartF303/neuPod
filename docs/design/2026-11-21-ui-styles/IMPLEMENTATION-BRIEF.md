# Implementation brief: UI styles (Classic and Modern)

You are implementing the design in [README.md](README.md) in this folder. Read it in full first, and look at every PNG in
`mockups/`; open the matching `.html` when you need an exact colour, gradient or size.

This is a long job. You work through [PROGRESS.md](PROGRESS.md) one item at a time, in a loop, until every item is ticked
or a stop condition is met. `PROGRESS.md` is the only record of where you are: a new session, or one whose context was
compacted, starts by reading it and carries on from the first unticked item.

## Before the first item

1. Load the repo's `neupod` skill (`.claude/skills/neupod/SKILL.md`) and follow its rules for every target. They are not
   repeated here and they win over this brief. Read `CONTRIBUTING.md`.
2. Read `docs/ui.md`, `docs/adr/0018-ui-resolution-profiles.md`, `app/ui/ui_theme.[ch]`, `app/ui/ui_metrics*.h`,
   `app/ui/ui_frame.c`, `app/ui/ui_strips.c` and `tests/CMakeLists.txt` (the tours).
3. Build host and run `ctest`. Everything must pass before you change anything. If it does not, stop and report.
4. **Reconcile the checklist.** Walk `app/ui/*.c` and the tours and compare every screen and state you find with
   `PROGRESS.md`. Add what is missing, in the right phase, in a docs-only PR. Do not remove items.

## The loop

Repeat until done:

1. **Pick** the first unticked item in `PROGRESS.md` whose phase is open (a phase opens when the one before it is
   complete, unless the file says otherwise). Skip an item marked `BLOCKED` and note why it still is.
2. **Plan** it briefly: what the README says, which files change, which tours cover it, what could regress.
3. **Implement** on a branch named per `CONTRIBUTING.md`. Keep `app/` portable. Reuse `ui_metrics_t`; add fields to it
   for new lengths, never literal pixel values in screen code. Name colours by token; add skin functions to the style,
   never an `if (style == ...)` inside a screen.
4. **Verify**, all of:
   - host unit tests and every UI tour pass at **320x240, 480x360 and 600x450**, with the layout audit on;
   - the item is covered by a tour snapshot in **each style it applies to**, at each of the three sizes; add tour steps
     where there are none;
   - you have looked at the new snapshots next to the mock-up PNGs. They need not match pixel for pixel (LVGL is not a
     browser), but structure, colours, proportions and text must agree. Describe any difference you leave;
   - text contrast: at least 4.5:1 for anything that must be read; extend the existing contrast test to the new tables;
   - builds for every platform CI builds: `host`, `rp2350`, `rp2350-amoled241`, `esp32s3`, `y2`;
   - the frozen Pico 2 W image: report flash and static RAM deltas. It has about 100 bytes of static RAM spare. New
     style code must cost it nothing: gate it at compile time the way `NEUPOD_UI_HIRES` does. If an item cannot be done
     without growing that image, stop and ask.
5. **Review**: have a fresh reviewer (a subagent that has not seen the work) read the diff against the README.
6. **Document** in the same PR: `docs/ui.md` (it describes the UI as built), `CHANGELOG.md` (Unreleased), and any ADR
   the change touches. A new third-party asset goes into `LICENSE.md` and the Licences screen; the `licences` test must
   pass.
7. **PR**, per the repo's rules; squash-merge once CI is green.
8. **Tick** the item in `PROGRESS.md` with the PR number and the date, in that same PR or the next one. Add one line
   under "Notes" for anything the next session must know.
9. Go to 1.

Keep PRs small: one item, or a few that cannot be separated.

## Hardware

Host tours are the main check. On top of them:

- **Phase 0** runs on the two Waveshare AMOLED boards; it exists to measure them.
- At the end of each phase, deploy to whichever of the AMOLED boards and the Y2 are attached (`bench-status.sh`) and
  check that phase's screens by eye and by screenshot where the target supports it. Record what was verified, on which
  board, with the date, in the PR and in `PROGRESS.md`. If no board is attached, write "not hardware-tested" and carry on.
- Every device rule in the `neupod` skill applies, including the Y2's.

## Stop and ask the owner

Stop the loop, summarise, and wait, when:

- an item depends on one of the README's **Open questions**; do not decide them. You may prepare options (for example
  snapshots of two variants) to make the answer quick;
- the README and the code or an ADR conflict in a way the README does not address;
- phase 0's numbers say an effect cannot be drawn at an acceptable frame rate, and the fallback changes the look;
- a change would grow the frozen Pico 2 W image, change an owner decision, or is one the `neupod` skill says to ask about;
- the same item has failed verification three times.

When you stop, leave the tree clean, `PROGRESS.md` current, and say exactly what you need.

## Phases

The checklist in `PROGRESS.md` follows these.

**Phase 0: measure (throwaway).** On each AMOLED board, time these at 600x450 and report frame time and CPU load:
a vertical gradient bar one row high sliding one row; a full-width alpha-blended panel over an image; a full-screen
backdrop image with a list scrolling over it; five album tiles moving one Cover Flow step, flat, and with the two-plane
angle of the Classic mock-up; a 3 px ring appearing round a row. Also measure making the backdrop (a small blurred,
darkened copy of a decoded cover) once per track: time and memory. This code is not merged; the report is
(`docs/design/2026-11-21-ui-styles/PHASE0-RESULTS.md`). **Stop after phase 0 and show the owner the numbers.**

**Phase 1: the style layer.** A theme becomes style x mode. Each style supplies its colour tables and its skin
functions. Today's look is kept as the build default until both new styles are complete, so nothing changes for a user
yet. Settings > Display gains **Style** (Classic, Modern) next to Theme; stored like the other settings. Tours can select
a style. Classic Dark and Modern Light are `BLOCKED` on Open question 1.

**Phase 2: status bar.** The three zones, the play-state glyphs, the hardware icons and their rules, the clock removed
(including the Home pane; Open question 9 covers what replaces it), title swap for a message and for volume.

**Phase 3: feedback.** Assign every `ui_toast_show()` call to one of the three places per the README table, build the
row flash, the greyed-row reason and the centred panel, then remove the toast strip and the bottom HUD.

**Phase 4: Now Playing.** Volume swaps with the progress bar; the format line; pills and volume strip removed; seek mode
and page 2 in both styles; Modern's backdrop and accent from the art.

**Phase 5: lists and menus.** Selection bar, scroll bar and list ground skins; Home; the Music menu with its preview
pane; every library list; fast-scroll tile and rail.

**Phase 6: albums.** An artist's albums with thumbnails; the album header; Cover Flow in both styles.

**Phase 7: everything else**, in both styles: Settings and its pages, Bluetooth, Wi-Fi, Files, Playlists, the wheel
input widgets, library building / empty / error states, Hold and lock, the power-hold line, the boot splash, Licences,
Diagnostics.

**Phase 8: finish.** Make the new styles the default (Classic Light) where the build carries them; a full pass of every
screen in every style at every size, on host and on each attached board; `docs/ui.md` rewritten to match; this folder's
README updated where the build ended up different, with the reason.

## Done

Every item in `PROGRESS.md` is ticked or marked `BLOCKED` with an owner question attached, CI is green on `main`, and the
final report lists: what shipped, what is blocked and on what, the Pico 2 W size deltas, and what was and was not
verified on hardware.
