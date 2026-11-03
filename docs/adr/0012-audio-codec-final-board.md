# ADR-0012: Audio codec for the final board

## Status

Proposed (design response, 3 Nov 2026). **Open review point:** the ES8311 is a *mono* codec (one DAC
channel, one ADC channel). The baseline below needs re-checking against the stereo headphone requirement
before it is accepted; see Consequences.

## Context

- The testbed DAC is the Pimoroni Pico Audio Pack: TI **PCM5100A** (no MCLK, internal PLL from BCK) with a
  **PAM8908** headphone amp on the "phones" jack (low/high gain switch); "line out" is the DAC directly.
- The recording requirement (Requirements → Audio, 31 Oct 2026) needs an ADC; the CS43131 is DAC-only.
- The JLCPCB/LCSC survey: CS43131 has about 42 pcs at LCSC, so it cannot be assembled in volume.
- A codec that needs MCLK ties the clock plan (ADR-0015) to the codec; one that can run its PLL from BCLK does not.

## Decision

- The final board **provides MCLK anyway**: `clk_gpout` on a spare GPIO at 256 fs (costs one pin, no parts).
- The codec must still **run without MCLK** (PLL from BCLK), so a clock problem never means silence.
- Candidates, in order:

| Part | DAC | ADC | MCLK-free | LCSC (survey) | Role |
| --- | --- | --- | --- | --- | --- |
| Everest **ES8311** | mono (see status) | mono | PLL from BCLK | $0.43, ~100k stock | proposed baseline |
| Everest **ES8388** | stereo | stereo | needs MCLK (provided) | stocked | alternative |
| TI **PCM5102A** | stereo | none | yes | stocked | DAC-only fallback (recording then needs a separate ADC) |
| Cirrus CS43131 | stereo | none | — | ~42 pcs | **dropped** |

- Format: standard I2S, 32-bit slots (64 fs), as the testbed.

## Consequences

- Requirements and Design move off the CS43131 (both pages updated).
- If the mono finding stands, the realistic stereo-plus-ADC options are the ES8388 (or another stereo codec)
  or PCM5102A + a separate mono ADC (the ES8311 used as ADC-only would do). Decision returns to design.
- A headphone driver is still needed after an ES83xx/PCM510x (none of them drive 16–32 Ω at useful level);
  pick it with the codec.
- **RAM budget:** none (codec control is a few hundred bytes of I2C driver).
