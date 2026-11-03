# ADR-0015: Clock plan

## Status

Accepted (design response, 3 Nov 2026).

## Context

On the testbed the ST7789 runs SPI1 at 62.5 MHz = clk_peri / 2, which is what fixed clk_sys at 125 MHz
([display tests](../display-tests.md)). SD uses an integer PIO divider from the same clock; the I2S divider
is fractional (see [Audio](../audio.md#clocking-and-rate-accuracy)).

## Decision

- **Testbed:** clk_sys = 125 MHz; clk_peri sourced from clk_sys **explicitly** (not left to the SDK default).
- **Final board:** the AMOLED is QSPI at 40 MHz × 4 lanes, so the panel no longer dictates clk_sys;
  revisit 150 MHz then (SD and I2S dividers re-derived at that point).
- The codec gets MCLK at 256 fs from `clk_gpout` on the final board (ADR-0012).

## Consequences

- Any change to clk_sys changes the SD clock (integer dividers only, see [Storage](../storage.md)) and the
  I2S rate error; both must be re-checked.
- **RAM budget:** none.
