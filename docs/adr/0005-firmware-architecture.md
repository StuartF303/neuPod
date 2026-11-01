# ADR-0005: Firmware architecture — portable app over a HAL, with a host build

## Status

Proposed

## Context

The firmware is written from scratch and two MCU platforms (RP2350 primary, ESP32-S3
comparison) are evaluated from one tree. Most of the work — UI, library index, playback,
codecs, filesystem — does not depend on the chip; the radio, USB and bus drivers do.

## Decision

- `app/` holds portable C only (UI, library, playback logic, codecs, FatFs). It never includes
  a platform SDK header.
- `hal/include/` defines the hardware interface (`hal_display.h`, `hal_input.h`,
  `hal_storage.h`, `hal_audio.h`, `hal_i2c.h`, `hal_power.h`, `hal_radio.h`, `hal_system.h`).
- `platforms/rp2350/` and `platforms/esp32s3/` implement the HAL with their SDKs;
  `hal/host/` implements it with SDL2 for desktop development and `tests/` runs unit tests
  against the portable code.
- Day-to-day UI and logic development happens in the host build; driver bring-up on the Pico 2
  W with the Debug Probe; radio, USB and audio only on real hardware.
- The display is accessed only through `hal_display.h` so the stock-panel and new-panel
  drivers are interchangeable, and the radio only through `hal_radio.h` so the CYW43439 and
  an external QCC module are interchangeable.

## Consequences

- Both platforms build the same `app/` sources, so the comparison is like-for-like.
- Every new feature must first be expressible through the HAL; platform-specific shortcuts are
  not allowed in `app/`.
- CI builds rp2350 and host on every push; ESP32-S3 is allowed to fail until its skeleton
  builds.
