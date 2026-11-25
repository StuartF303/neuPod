# Feature Specification: Full album art on the AMOLED dev boards

**Feature Branch**: `feat/ui-25-amoled-art`

**Created**: 2026-11-25

**Status**: Draft

**Input**: User description: "Backlog UI-25 (ADR-0022 gap). Full album art on the AMOLED dev boards: the 160 x 160 thumbnails, the
24-album cache, browse art and Cover Flow. Today the ESP32-S3-Touch-AMOLED-2.41 and RP2350-Touch-AMOLED-2.41 builds keep the
frozen Pico's 64 x 64 thumbnail with no cache, because `NEUPOD_ART_HIRES` is on only for hosted builds. Turn it on where the
board has PSRAM, with the 24-album cache in PSRAM; report RAM deltas."

## Context

[ADR-0022](../../docs/adr/0022-one-player-ui.md) (one player UI) names this as a known gap: the two Waveshare AMOLED dev boards
have 8 MB of PSRAM each, so memory is not a reason for them to show less album art than the simulator, the Y2 testbed and the
phone app. Principle I of the [constitution](../../docs/constitution.md) makes such a gap a bug to close. Principle IV requires
a RAM budget and keeps the Pico 2 W frozen.

"The big-RAM player" below means everything the firmware's large-memory build option already switches on together on the
hosted builds: the 160 x 160 art, the 24-album cache, art while browsing the library, Cover Flow, and the features that share
that option (the Playlists menu with On-The-Go and the smart playlists, the play counts behind them, 128 remembered ratings
instead of 24, and the larger play-queue name pool).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Sharp album art on Now Playing (Priority: P1)

The owner plays a track on either AMOLED board. Now Playing shows the album's picture as sharp as the simulator shows it at
the same 600x450 profile, instead of a 64 x 64 picture blown up and soft.

**Why this priority**: Now Playing is the screen seen most, and the soft picture is the most visible difference from the other
neuPods. It is also the smallest slice: the larger picture alone, without the cache or Cover Flow, already closes most of
the visible gap.

**Independent Test**: Put a card with tagged music and cover pictures in a board, play a track, compare the screenshot with
the simulator's at 600x450 for the same track.

**Acceptance Scenarios**:

1. **Given** a track whose album has a baseline JPEG picture (embedded or a cover file in its folder), **When** the owner
   plays it on either AMOLED board, **Then** Now Playing shows that picture decoded at up to 160 x 160, the same picture the
   simulator chooses for that track.
2. **Given** a track with no usable picture (none, PNG, progressive JPEG), **When** it plays, **Then** the same placeholder as
   on the other targets is shown.
3. **Given** the owner skips back to a track of an album seen in the last 24 albums, **When** Now Playing opens, **Then** its
   picture appears without being decoded again.

---

### User Story 2 - Cover Flow and browse art (Priority: P2)

The owner opens Music > Albums (or an artist's albums) on either AMOLED board and sees each album's art in the album header,
and can open Cover Flow and step through the albums with their pictures, exactly as on the simulator.

**Why this priority**: Cover Flow is a named part of the one player UI (constitution I). It depends on the cache of
User Story 1.

**Independent Test**: The library Cover Flow tour steps (`lib-coverflow*`) driven over the console on each board give the
same screens as the simulator's tour at 600x450 (allowing for the pictures still loading).

**Acceptance Scenarios**:

1. **Given** a library with at least 10 albums with pictures, **When** the owner opens Cover Flow and steps one album at a
   time, **Then** the centre and neighbouring tiles show their albums' pictures, and a step decodes only the one new tile.
2. **Given** Cover Flow is open, **When** the owner steps quickly through many albums, **Then** the screen keeps up with the
   input and the tiles fill in once stepping slows; nothing freezes and playback does not stutter.
3. **Given** an album with no picture, **When** its tile is shown, **Then** it shows the placeholder and is not decoded again
   until the browse screen is left.

---

### User Story 3 - The rest of the big-RAM player (Priority: P3)

On the AMOLED boards, the Playlists menu (On-The-Go, Recently Played, Most Played, Top Rated, Unplayed, playlist files),
play counts, and 128 remembered ratings work as on the simulator, because they come with the same build option.

**Why this priority**: These ride on the same option and are gaps of the same kind (ADR-0022); they are not the headline of
UI-25, so they come last, but leaving them off would need a new option that splits the player in two.

**Independent Test**: The playlists and ratings tours / unit tests pass on the host; on each board, Music > Playlists lists the
smart playlists, a played track shows up in Recently Played, and an On-The-Go list survives a restart.

**Acceptance Scenarios**:

1. **Given** a card with music, **When** the owner plays three tracks and opens Music > Playlists > Recently Played, **Then**
   those tracks are listed newest first.
2. **Given** a write-protected card, **When** the owner rates tracks, **Then** ratings are kept in memory and written when the
   card allows, as on the other targets.

---

### Edge Cases

- **PSRAM missing or failing at start-up** (a fault, or a board variant without it): the board must not silently run with
  memory it does not have. It stops at start-up with a clear message naming the PSRAM, on the screen where the display is up
  and on the console (see FR-007).
- **A warm restart** (reset button, a firmware load that reboots a running image, a watchdog reset) must find the PSRAM again,
  not only a cold power-on.
- **Very large or unusual pictures**: pictures above the existing size limit, progressive JPEGs and PNGs show the placeholder,
  as on every target.
- **More than 24 albums browsed**: the least recently used picture is replaced; pictures on screen are never replaced.
- **Playback during art decoding**: decoding pictures never causes an audible gap (the AMOLED boards have no audio output; the
  measure is the null sink's under-run counter staying at 0).
- **The frozen Pico 2 W**: it keeps the 64 x 64 picture, no cache, no Cover Flow, no playlists; its image is unchanged.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The ESP32-S3-Touch-AMOLED-2.41 and RP2350-Touch-AMOLED-2.41 builds MUST provide the big-RAM player: 160 x 160
  album art, the 24-album cache, art in the library album header, and Cover Flow, behaving as the simulator does at 600x450.
- **FR-002**: Those builds MUST also provide the other features of the same build option (Playlists, play counts, 128 ratings,
  the larger play-queue name pool), behaving as the simulator does.
- **FR-003**: The picture chosen for a track and its decoded pixels MUST be the same on the AMOLED boards as on the simulator
  (the same firmware code decides; no board-specific art logic).
- **FR-004**: The album-art cache and the other large buffers this feature adds MUST live in the board's PSRAM, not in its
  internal RAM, so that the internal RAM the rest of the firmware relies on is not reduced by more than the budget in the plan.
- **FR-005**: The Pico 2 W image MUST be unchanged: the same static RAM and flash section sizes before and after, reported in
  the pull request.
- **FR-006**: The RP2350 AMOLED build MUST bring up its PSRAM before any code that uses it runs, at the clock the firmware
  actually runs at, after both cold and warm starts.
- **FR-007**: If the PSRAM is missing or fails its start-up check on an AMOLED board, the firmware MUST stop at start-up with
  a message naming the PSRAM (console, and screen when available), and MUST NOT run with the big-RAM player on memory it does
  not have.
- **FR-008**: The start-up log of each AMOLED board MUST report the PSRAM found (size) and how much of it the firmware uses.
- **FR-009**: Art decoding and Cover Flow MUST NOT cause audio under-runs or freeze the UI on the AMOLED boards (measured on
  the bench: the null sink's under-run counter stays at 0 and the UI stays responsive during a Cover Flow run).
- **FR-010**: The build option MUST stay one option (the existing one), on by default for the AMOLED board builds, and
  overridable off for comparison; the build documentation of each board MUST say so.
- **FR-011**: The feature MUST state a RAM budget per board (internal RAM and PSRAM, static and transient) and the measured
  deltas, in the plan, the boards' docs and the PR (constitution IV).

### Key Entities

- **Album-art cache**: up to 24 decoded pictures (160 x 160, 2 bytes a pixel), each tagged with the album it belongs to and
  when it was last used; the pictures on screen are pinned.
- **Board memory map**: for each AMOLED board, what lives in internal RAM and what in PSRAM, and the budget of each.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On both AMOLED boards, Now Playing for the same track shows the same picture as the simulator at 600x450,
  at the same (160 x 160 or smaller) decoded size.
- **SC-002**: On both boards, the Cover Flow tour runs from start to end with every tile that has a picture filled within
  2 seconds of the stepping stopping, and the UI accepts input throughout.
- **SC-003**: Browsing back to any of the last 24 albums shows its picture without a new decode (confirmed by the decode
  counter on the console).
- **SC-004**: The Pico 2 W image's static RAM and flash section sizes are byte-for-byte the same as before the change.
- **SC-005**: Each AMOLED board's internal RAM left free after start-up is no less than the budget in the plan, and its PSRAM
  use matches the plan within 5%.
- **SC-006**: 0 audio under-runs during a 5-minute Cover Flow and Now Playing session on each board.
- **SC-007**: A board whose PSRAM does not answer stops at start-up with a message naming the PSRAM within 2 seconds, and a
  warm restart of a healthy board finds its PSRAM 10 times out of 10.

## Clarifications

### Session 2026-11-25

- Q: Does UI-25 also turn on the playlists, play counts, 128 ratings and the larger queue pool that share `NEUPOD_ART_HIRES`?
  → A: Yes (FR-002, User Story 3). Settled from the written record, not by asking: the owner's backlog item says "turn
  `NEUPOD_ART_HIRES` on where the board has PSRAM", and ADR-0022 says a gap with no hardware reason is a bug to close.
  Splitting the option would create two players. The lead may overturn this; then User Story 3 and FR-002 drop out.
- Q: What happens when the PSRAM is missing or fails? → A: Stop at start-up with a message naming the PSRAM (FR-007). This is
  what the ESP32-S3 build already does today (its PSRAM holds the codec arena, LVGL pool and display shadow, so it cannot boot
  without it), and the RP2350 AMOLED build has no internal RAM left to fall back on (about 11 KB free). Recorded as a
  decision for the lead's review, see plan.md.

## Assumptions

- Both boards have 8 MB of PSRAM fitted (board docs and `.claude/skills/neupod/reference/boards.md`; the RP2350 board's
  Waveshare page says 2 MB, the bring-up probe measured 8 MB).
- The boards run the 600x450 UI profile (ADR-0018 amendment of 14 Nov 2026); the simulator at 600x450 is the reference.
- The AMOLED boards have no audio output; "no under-runs" is measured on the null sink's real-time clock.
- The phone app, Y2 and simulator are unaffected (they already have the big-RAM player).
- The bench card used for testing holds tagged music with baseline JPEG art (embedded and cover files).
