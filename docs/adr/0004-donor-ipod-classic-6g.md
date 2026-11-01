# ADR-0004: Donor platform — iPod Classic 6G (A1238)

## Status

Proposed (decision taken 25 Oct 2026; donor purchased)

## Context

A replacement logic board has to be designed around one iPod family's shell, mid-frame, click
wheel, display and flex cables. Candidates were the 4G (Sprout's target), the 5G/5.5G Video and
the 6G/7G Classic.

## Decision

Design for the **iPod Classic 6G/7G**, starting from a thin-shell 120 GB A1238 (EMC 2173, board
revision A, 2008). The thick 160 GB shell is treated as a variant that only changes the back
cover and available battery depth.

## Consequences

- Cheapest and most plentiful donors (£20–40 with dead drives); same shell, screen and wheel
  across 6G and 7G.
- 320×240 colour panel and the larger wheel; the LCD, wheel, PMU and codec interfaces are
  documented in Rockbox's `ipod6g` target.
- The all-metal shell is a Faraday cage: antenna placement is constrained to the plastic
  inserts (headphone jack area, under the wheel).
- Usable internal depth is ~6 mm (thin) or ~9 mm (thick) for board plus cell once the HDD is
  removed.
- Nothing published gives the 6G connector pitches, board outline or mid-frame geometry; these
  come from measuring and 3D-scanning the donor.
