# ADR-0002: Display: ST7789 SPI for Phase 1

## Status

Proposed

## Context

The Phase 1 testbed needs a display to develop the UI against. The final product candidates
are the EastRising ER-TFT024IPS-3 (2.4" ST7789V, 50-pin FPC) or a 2.4" 450×600 AMOLED
(RM690B0, QSPI), with the original iPod screen as an option.

## Decision

Use the Adafruit 2.0" 320×240 IPS TFT (ST7789, SPI) in landscape on SPI1 for Phase 1, driven
with DMA, backlight PWM on GP8. The display is accessed only through `hal_display.h`.

## Consequences

- The ST7789 driver carries over to the ER-TFT024IPS-3 candidate, which uses the same
  controller family.
- The AMOLED candidate would need a separate QSPI driver behind the same HAL.
- 320×240 is the UI's development resolution; the UI must not hard-code it.
