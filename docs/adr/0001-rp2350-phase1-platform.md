# ADR-0001: RP2350 as Phase 1 platform

## Status

Proposed

## Context

neuPod needs a compute platform with Wi-Fi, Bluetooth (A2DP), microSD, I²S audio, a display
interface and the iPod 6G click-wheel interface. Two platforms are being evaluated side by
side: the Raspberry Pi Pico 2 W (RP2350 + CYW43439) and the ESP32-S3. A working RP2350 build
already exists, with a Debug Probe for flashing and console.

## Decision

Use the Raspberry Pi Pico 2 W (RP2350) as the Phase 1 testbed and primary platform. Keep an
ESP32-S3 build of the same portable `app/` sources in the repo as a comparison.

## Consequences

- microSD runs as 4-bit SDIO on PIO, and the click-wheel receiver is a PIO program.
- Bluetooth A2DP uses BTstack on the CYW43439, with an external Qualcomm QCC module as the
  alternative.
- Application code must stay portable behind the HAL so the ESP32-S3 comparison remains
  meaningful; platform code lives under `platforms/`.
- Two toolchains (Pico SDK, ESP-IDF) must be maintained in CI.
