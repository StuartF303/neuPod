# ADR-0018: Reuse the donor headphone jack flex; park optical S/PDIF

Status: **accepted** (4 Nov 2026)

## Context

The 6G headphone flex carries a 3.5 mm TRRS jack (CTIA/Apple order: Tip L, Ring 1 R, Ring 2 ground, Sleeve mic), the hold switch and the "Mikey" inline-remote decoder (I²C 0x72). The requirements asked for a single jack serving headphones and line level, mono/stereo recording, and an optional mini-TOSLINK optical output in the Apple style. The case hole is sized for the donor jack.

## Decision

1. **Reuse the donor flex and jack.** No new jack on the board; the board carries the flex's FPC connector (pitch measured from the donor).
2. **Inline remote** is decoded either by Mikey over I²C (as Rockbox does) or by biasing the mic conductor (~2.2 kΩ to 2.0–2.7 V) and reading an ADC: centre ≈ 0 Ω, vol+ ≈ 240 Ω, vol− ≈ 620 Ω. Plug detect from the jack switch or Mikey.
3. **Recording** is mono mic through the jack. Stereo line-in on the same jack is possible only by switching the L/R conductors to the codec ADC with a dual SPDT analogue switch (TS5A23157 class, one GPIO) in a "line-in mode"; whether to fit it is decided with the codec ([ADR-0012](0012-audio-codec-final-board.md)). Stereo in is otherwise over USB-C audio.
4. **Optical S/PDIF is parked.** The combo jack body (Everlight PLT133/T10W class) is taller and longer than the donor jack and cannot share the flex. It returns only if a custom case is made. No GPIO is reserved.

## Consequences

* One GPIO freed on every board; the optical parts leave the parts list.
* Headphone-detect and hold-switch handling stay as in the Rockbox 6G port (hold on a PMU GPIO there; a plain GPIO here).
* The thin-shell depth constraint no longer depends on the jack.
