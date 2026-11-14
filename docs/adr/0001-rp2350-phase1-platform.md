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

## Amendment, 12 November 2026: the Pico 2 W is frozen; the product target is the RP2350B with PSRAM

Owner decision. The original text above is left as it was.

* **The Pico 2 W (RP2350A, 520 KB SRAM, CYW43439) target is frozen.** It stays in the repository and in CI as a **compile-only regression
  check** (`firmware.yml`, job `rp2350`): the build must keep passing, and it is still what the bench rig runs. It receives fixes, not
  features, and its **520 KB of SRAM is no longer a budget limit**: a change is not refused, redesigned or squeezed because the Pico 2 W
  would run out of RAM. The free-SRAM figures in [Bluetooth](../bluetooth.md#memory-budget-rp2350-main-sram-512-kb) and the "no new static RAM"
  rule of [ADR-0013](0013-bluetooth-ram-psram.md) are history, kept for the numbers.
* **The product target is the RP2350B with PSRAM.** A Waveshare RP2350B development board is on order; the final board follows
  [ADR-0004](0004-donor-ipod-classic-6g.md) and [Design](../design.md) (RP2350B, 8 MB APS6404L PSRAM on QSPI CS1, 16 MB flash).
* **New features may assume the RP2350B and PSRAM** and be compiled out of the Pico 2 W build (a build option or a platform macro), so the
  frozen build keeps compiling. Features that only exist with PSRAM (Cover Flow, large art, PNG and progressive JPEG art, AAC, an HTTP
  client, bigger library caches) no longer wait for "the final board" as a vague date: they wait for the RP2350B board and its HAL.
* **What does not change:** the portable `app/` over a HAL, the ESP32-S3 skeleton (still compile-only), the core-1 rules, and the
  requirement that a feature ADR states its RAM budget (now against the RP2350B and its PSRAM, with the Pico 2 W's effect noted when the
  frozen build is touched).
