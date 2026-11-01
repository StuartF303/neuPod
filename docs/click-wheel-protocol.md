# Click-wheel protocol (iPod Classic 6G)

Source: Rockbox ipod6g reverse-engineering. Items marked **TBC** are unconfirmed.

## Electrical

- Four lines to the wheel: **CLK**, **SDO**, **SDI**, and one more (**TBC**).
- Open-drain signalling with **18 kΩ pull-ups**.
- The wheel generates its own clock at **~53 kHz**.

## Frames (wheel → host)

The wheel streams **32-bit frames, LSB first**.

A frame `x` is valid when:

```c
(x & 0x800000FF) == 0x8000001A
```

| Bits | Field |
|------|-------|
| 0–7 | `0x1A` (frame marker, part of the validity check) |
| 8 | Select |
| 9 | Right |
| 10 | Left |
| 11 | Play |
| 12 | Menu |
| 16–22 | Finger position, 0–95 |
| 30 | Finger on wheel |
| 31 | `1` (part of the validity check) |

## Host → wheel

The host sends one init packet, **`0x8000023A`**, at start-up and again after Hold is released.

## Open questions

- Identity and function of the fourth line.
- Which edge of CLK data is valid on, and how frame boundaries are delimited.
- How the host clocks out the init packet (on the wheel's clock or its own).
- Where the 18 kΩ pull-ups sit (wheel flex or logic board).

Firmware: `app/include/clickwheel_frame.h` (decoder), `platforms/rp2350/hal/hal_input.c` (PIO
receiver), tests in `tests/`.
