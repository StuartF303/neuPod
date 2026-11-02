# ADR-0007: Bluetooth audio path — on-MCU BTstack or an external transmitter module

## Status

Proposed — decision deferred to a bench comparison

## Context

AirPods and most headphones need classic Bluetooth A2DP. The CYW43439 on the Pico 2 W can do
A2DP source via BTstack, but only SBC/SBC-XQ (AAC encoding is licence-encumbered and heavy on a
Cortex-M33; aptX and LDAC are not available in open stacks). A dedicated transmitter module
(Qualcomm QCC3008/3040/5125, Microchip BM83) takes I²S in, negotiates AAC/aptX HD/(LDAC) and
removes the whole stack from the MCU — which also makes the ESP32-S3 (BLE-only) a viable host.

## Decision

Keep the radio behind `hal_radio.h` and choose between:

| | A: RP2350 + CYW43439 | B: ESP32-S3 + QCC module | C: RP2350 + QCC module |
| --- | --- | --- | --- |
| BT codecs | SBC / SBC-XQ | AAC, aptX HD, (LDAC) | AAC, aptX HD, (LDAC) |
| Wi-Fi | CYW43439 | on-chip | CYW43439 |
| Extra parts | none | QCC module + antenna | QCC module (two radios) |
| Power (playing, screen off) | lowest, ~80–110 mA | ~110–160 mA | moderate |

after a listening test: Pico 2 W running BTstack SBC-XQ against a QCC3008 transmitter board fed
from the Audio Pack, both paired to the reference AirPods Pro.

## Consequences

- Phase 1 proceeds on option A so A2DP can be proven early.
- Option B flips the processor choice (ADR-0001) to the ESP32-S3; option C is only sensible if
  the RP2350's PIO/SDIO advantages outweigh carrying two radios.
- The policy in ADR-0003 is unaffected: Bluetooth is the one lossy path whichever option wins.
- HFP (headset mic, neuPod as Audio Gateway) and future LE Audio support depend on the same choice.
- Required roles (corrected 2 Nov 2026): neuPod is the **A2DP source** (the headset is the sink)
  and the **AVRCP target** with absolute volume; an earlier draft of the requirements said
  "A2DP sink" in error. Option A implements these in BTstack; with a QCC module the module's
  firmware must provide them.
