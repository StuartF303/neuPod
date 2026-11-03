# ADR-0007: Bluetooth audio path — on-MCU BTstack or an external transmitter module

## Status

Proposed — decision deferred to a bench comparison. Updated 3 Nov 2026: bare CYW43439 ruled out for the final board (see the update at the end).

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

## Update (3 Nov 2026, design response)

- **The final board will not use a bare CYW43439.** Two independent reasons: it cannot be assembled at
  JLCPCB (≈30 pcs at LCSC, no RM2 module listed), and the BTstack / cyw43-driver grant covers Raspberry Pi
  products (Pico W / Pico 2 W / RM2), not a bare chip on our board.
- Remaining options, now the main open architecture decision:

| | RM2 module | ESP32-WROOM-32E (original ESP32) | ESP32-S3 |
| --- | --- | --- | --- |
| Classic BT / A2DP source | yes (BTstack, grant covered) | yes (ESP-IDF Bluedroid, Apache-2.0) | **no** — BLE only |
| Assembly | hand-placed | JLCPCB | JLCPCB |
| Host MCU | RP2350B | the ESP32 itself, or as a radio beside an RP2350B | needs a QCC/BM83 A2DP module |

- **Review point:** the design response lists "ESP32-S3 / ESP32-WROOM-32E (ESP-IDF BT stack)" together. The
  S3 has no BR/EDR, so it can only be the host with an external A2DP transmitter (option B above); only the
  original ESP32 runs A2DP on ESP-IDF.
- Antenna: in the top plastic window of the 6G back, the only RF-transparent area (Requirements → Connectivity).
- The testbed stays on the Pico 2 W.
