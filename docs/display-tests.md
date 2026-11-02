# Display bring-up tests

An interactive test menu for the Phase 1 display (Adafruit 2.0" 320x240 IPS TFT, ST7789, SPI1).
The menu runs on the Pico and is driven from the **PC's serial console**: you type the keys on
the PC, not on the Pico.

## Confirmed hardware settings

Confirmed on the bench (2026-11-02):

| Setting | Value |
|---------|-------|
| Orientation / colour order | MADCTL `0x60` (MX + MV, landscape), RGB order |
| Inversion | INVON (the panel needs it) |
| Backlight | GP8 PWM, active high |
| System clock | clk_sys = clk_peri = 125 MHz |
| SPI1 | 62.5 MHz exactly (clk_peri / 2), 16-bit frames, DMA |
| Console | UART0 GP0/GP1, 115200 8N1 |
| Column/row offset | 0 / 0 (test 3 border visible on all four edges) |

Measured on the bench (2026-11-02), all eight tests passing visual checks:

| Measurement | Result |
|-------------|--------|
| Full-screen blit from flash (test 5) | 22 ms |
| Same image via strip renderer (test 5) | 23 ms |
| Strip-rendered animation (test 7) | 46.05 fps, 7.07 MB/s — 90 % of the 7.81 MB/s / 50.8 fps ceiling |
| Random shapes (test 4) | 91 strip-rendered frames in 2012 ms |

## Build, flash, run

```bash
export PICO_SDK_PATH=$HOME/pico/pico-sdk
scripts/build.sh rp2350
scripts/flash.sh rp2350 display_test
scripts/console.sh          # minicom on the Debug Probe UART; type the keys here
```

Reset the board with the console already open to see the start-up line that reports clk_sys,
clk_peri and the real SPI baud rate. On the PC, `scripts/build.sh host` builds the same menu as
`build/host/hal/host/display_test` (keys from the terminal; with SDL installed it shows a
window, otherwise set `NEUPOD_DISPLAY_PPM=out.ppm` to capture each frame). The Now Playing
screen alone: `build/host/hal/host/now_playing [8|16] [light]`.

## Menu keys

| Key | Action |
|-----|--------|
| `1`-`8` | Run that test |
| `a` | Run all, about 3 s each |
| `q` | Stop the running test and return to the menu |
| `b` | Toggle BGR order (MADCTL bit 3) |
| `i` | Toggle inversion |
| `r` | Cycle rotation: MADCTL `0x60`, `0xA0`, `0xE0`, `0x20` |
| `m` | Print MADCTL and inversion state |
| `p` | Corner pattern: red top-left, green top-right, blue bottom-left, white bottom-right |
| `h` / Enter | Reprint the menu |

`b`, `i` and `r` also work while a test is showing a static screen. Each test prints a
`VISUAL CHECK:` line describing what you should see; if a display call fails it prints `FAIL`
instead of relying on your eyes.

## Tests and what to look for

| # | Test | Should look like |
|---|------|------------------|
| 1 | Backlight | White screen fades dark, full, dark over 4 s with no flicker or steps, then holds at 10 %, 50 %, 100 % |
| 2 | Colours | Red, green, blue, white, black fills, uniform; 8 bars white-yellow-cyan-green-magenta-red-blue-black; four 256-step gradients. RGB565 has only 32/64/32 levels, so visible steps are expected |
| 3 | Sharpness | 1-px checkerboard and gratings crisp; 1-px white border with all four edges visible; centred crosshair |
| 4 | Geometry | Random lines, rectangles and circles (round, not oval), then a 32 x 24 grid of square cells |
| 5 | Image | Same picture twice (one DMA from flash, then strip-rendered): arrow points right, yellow mark top-left, magenta top-right |
| 6 | Text | Both fonts legible; Now Playing screen white-on-black then black-on-white in both fonts |
| 7 | Animation | Bouncing ball with fading trail; prints fps and SPI MB/s against the 7.81 MB/s ceiling at 62.5 MHz |
| 8 | Sleep | Image goes blank for 2 s, then returns without being redrawn |

## Failure checklist

| Symptom | Likely cause / action |
|---------|-----------------------|
| Image mirrored or rotated (test 5 arrow not pointing right, corner pattern in wrong corners) | Wrong MADCTL orientation bits. Press `r`; confirmed value is `0x60` |
| Red and blue swapped (red fill looks blue; yellow mark looks cyan) | BGR bit. Press `b` |
| Negative colours (white fill is black, red looks cyan) | Inversion. Press `i`; the panel needs inversion on |
| Smearing, noise, random pixels or grey haze on the 1-px gratings (test 3) | SPI too fast for the wiring: shorten wires, add ground, or lower the SPI rate |
| Garbage or horizontally shifted image, commands appearing as pixels | D/C or CS timing: check GP16 (D/C) and GP13 (CS) wiring and that nothing else drives them |
| Border line missing on one edge, or wrapped to the opposite side | Column/row offset (`TFT_COL_OFFSET` / `TFT_ROW_OFFSET`) |
| Backlight brightest at 0 and dark at 255 | Backlight polarity inverted (it is active high on this board) |
| Backlight never changes | GP8 wiring, or PWM not attached to the backlight pin |
| Blank or white after wake (test 8) | Sleep/wake timing; the ST7789 should keep its frame memory in sleep |
| Test 7 well below ~46 fps (measured) / 50.8 fps raw ceiling | Clock not 125 MHz or SPI below 62.5 MHz: check the start-up line |

## Source layout

The tests live in `app/tests/display_tests.c` and use the portable drawing layer in `app/gfx/`
(primitives, two bitmap fonts, a strip renderer that draws 320x16 strips into two 10 KB buffers
while DMA sends the previous one). Fonts: `app/gfx/fonts/README.md`. Images: `tools/README.md`.
Pixels are native-endian RGB565 everywhere, matching the driver's 16-bit SPI frames.
