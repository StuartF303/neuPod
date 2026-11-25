# Phase 0 results: effect timings on the AMOLED boards

Measured 21 November 2026 for [IMPLEMENTATION-BRIEF.md](IMPLEMENTATION-BRIEF.md), phase 0. The bench code is throwaway and is
not merged: branch `chore/phase0-bench` (`app/ui/ui_bench.c`, CMake option `NEUPOD_UI_BENCH`).

| Board | Status |
| --- | --- |
| ESP32-S3-Touch-AMOLED-2.41 (ESP32-S3R8, 240 MHz, 8 MB octal PSRAM) | **Measured** (below) |
| RP2350-Touch-AMOLED-2.41 (RP2350B, 8 MB QSPI PSRAM at 62.5 MHz SCK) | **Measured** (below). Single UI core: the UI task's core shows almost no idle time |

## Method

- The 600x450 profile, the product's LVGL configuration (software renderer, one draw unit, partial mode with two 600 x 30
  render buffers in internal RAM, RGB565), the real flush path. Only `LV_GRADIENT_MAX_STOPS` is raised to 4 for the Classic bar.
- Each test changes the scene once per UI pass, as fast as it will go, for 5 s after a 300 ms warm-up. `fps` is therefore
  capacity, not a capped rate. `frame` is one `lv_timer_handler` pass that rendered, flush included. `flush` is the part of
  it spent in the flush (copy into the panel shadow in PSRAM, then QSPI to the panel). Render = frame - flush.
- `ui core` is the UI task's share of its core (`os_task_stats`); the other core stays about 95 % idle in every test.
- All pictures are synthesised (a 600x450 RGB565 backdrop, a 160x160 cover), held in PSRAM. No JPEG decode.
- Numbers are stable: two full rounds agree within 1 %.

## ESP32-S3: frame tests

| Test | What changes each frame | fps | frame ms avg / p95 | flush ms | render ms | ui core |
| --- | --- | --- | --- | --- | --- | --- |
| `grad_bar` | Classic 4-stop gradient bar moves one row (7-row list) | 35.7 | 27.0 / 27.8 | 13.5 | 13.5 | 64 % |
| `ring` | 3 px ring on the Modern bar appears / disappears | 55.5 | 17.1 / 18.1 | 8.3 | 8.8 | 64 % |
| `alpha_panel` | 452 px panel at 50 % and a 45 px 50 % status strip over a full-screen image | 13.2 | 74.5 / 75.4 | 27.0 | 47.5 | 68-74 % |
| `backdrop_list` | list ground at 40 % over a full-screen backdrop, list scrolls one row | 6.0 | 163 / 164 | 52.9 | 110 | 75 % |
| `backdrop_list_smooth` | same, LVGL animated scroll | 5.8 | 163 / 164 | 52.8 | 110 | 67-70 % |
| `flow_flat` | Modern Cover Flow: rounded tiles 220/150/100, opacity 1/.85/.6, one step over 250 ms | 5.3 | 185 / 189 | 52.8 | 132 | 78 % |
| `flow_angled` | Classic Cover Flow: side tiles turned (6 px slices), 30 % reflections, gradient ground | 2.6-2.8 | 349-376 / 361-388 | 49.7 | ~310 | 86 % |
| `backdrop_draw_scaled` | 75x56 or 150x112 backdrop stretched to full screen by LVGL, every frame | 4.4 | 226 / 227 | 54-55 | 171 | 80 % |
| `backdrop_draw_full` | pre-scaled 600x450 backdrop drawn 1:1, every frame | 9.6 | 103 / 103 | 60 | 43 | 63 % |

A full-screen flush costs about 53 ms (540 KB at QSPI 40 MHz is 27 ms on the wire; the rest is the byte-swapped copy into
the PSRAM shadow and back out through the bounce buffers). Small changes are fine: the Classic bar moving and the Modern
ring both stay above 30 fps. Anything that repaints the whole screen over a picture runs at 3 to 13 fps.

## ESP32-S3: making the backdrop (once per track)

From a 160x160 RGB565 cover: downscale, 3-pass box blur, scrim (30 % top to 55 % bottom). Average of 8 runs.

| Small buffer | Bytes | Blur radius | Scale | Blur | Scrim | Total | PSRAM vs internal |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 75x56 | 16,800 | 2 | 2.4 ms | 5.7 ms | 0.4 ms | **8.4 ms** | same |
| 150x112 | 67,200 | 5 | 6.0 ms | 23.3 ms | 1.5 ms | **30.8 ms** | same |

Upscaling either one into a 600x450 buffer (bilinear, 540,000 bytes of PSRAM) takes another **119 ms**. Per track this is
cheap either way. Drawing the result is what costs (above): a pre-scaled full-size backdrop draws in 43 ms of render, LVGL
scaling the small one on every frame in 171 ms.

## RP2350: frame tests

Same tests, same method. Two rounds agree within 1 %.

| Test | fps | frame ms avg / p95 | flush ms | render ms | vs ESP32-S3 |
| --- | --- | --- | --- | --- | --- |
| `grad_bar` | 44.0 | 21.9 / 22.2 | 4.9 | 17.0 | faster (flush) |
| `ring` | 62.5 | 15.1 / 16.8 | 2.7 | 12.4 | faster (flush) |
| `alpha_panel` | 13.1 | 75.2 / 75.7 | 9.2 | 66.0 | same |
| `backdrop_list` | 5.5 | 178 / 186 | 17.6 | 160 | slower |
| `backdrop_list_smooth` | 5.4 | 177 / 179 | 17.6 | 160 | slower |
| `flow_flat` | 4.4 | 218 / 232 | 19.0 | 199 | slower |
| `flow_angled` | 1.9 | 505 / 533 | 17.5 | 488 | slower |
| `backdrop_draw_scaled` | 4.0-4.2 | 235-244 / 243-253 | 19.1 | 216-225 | same |
| `backdrop_draw_full` | 11.9 | 82.7 / 88 | 19.4 | 63 | faster |

The flush is about three times cheaper than the ESP32's: 18 ms for a full frame. Rendering is slower, about 1.2 to 1.6
times, wherever pictures are read from PSRAM. The result is the same picture as the ESP32: small changes are fine, and
full-screen repaints over a picture run at 2 to 13 fps.

## RP2350: making the backdrop

| Small buffer | Where | Bytes | Scale | Blur | Scrim | Total |
| --- | --- | --- | --- | --- | --- | --- |
| 75x56 | internal | 16,800 | 8.1 ms | 10.1 ms | 0.6 ms | **18.8 ms** |
| 75x56 | PSRAM | 16,800 | 9.8 ms | 17.3 ms | 0.8 ms | **28.0 ms** |
| 150x112 | PSRAM | 67,200 | 20.6 ms | 89.7 ms | 6.9 ms | **117 ms** |
| 150x112 | internal | | | | | **does not fit** (2 x 33,600 B from an 80 KB RTOS heap) |

Upscaling to a 600x450 PSRAM buffer: **294-301 ms**, once per track. All of this is still cheap next to a track change.

## What this means for the design

At 30 fps the budget is 33 ms a frame. On both boards as configured today:

- **Classic Light is affordable** for lists, the selection bar and the status bar. Its Cover Flow is not: the angled version
  draws at 2 to 3 fps, the flat one at 4 to 5.
- **Modern Dark is not affordable as drawn.** Every scroll over the backdrop repaints the screen (5 to 6 fps). Panels and
  translucent chrome over a picture are around 13 fps.

Most of the cost is software rendering on one core, plus a flush that blocks it. Engineering options exist that do
**not** change the look; none has been tried yet:

1. Overlap the flush with rendering (asynchronous DMA flush, both render buffers in use). Saves up to the flush per frame:
   53 ms on the ESP32-S3, 18 ms on the RP2350.
2. A second LVGL draw unit on the other core (`LV_DRAW_SW_DRAW_UNIT_CNT 2` with an RTOS `LV_USE_OS`). Up to about half the
   render time. Free on the ESP32-S3 (its second core is 95 % idle); on the RP2350 core 1 runs audio, so only partly.
3. QSPI to the ESP32's panel at 80 MHz, if the RM690B0 and the board allow it (not checked).
4. Always pre-scale the backdrop to 600x450 per track (119 ms once), never let LVGL scale it per frame.
5. Bigger render buffers in internal RAM, and composing the static backdrop once with the list ground baked in, so a scroll
   blends rows over a flat picture instead of picture plus a 40 % layer.

Even all five together are unlikely to bring a full-screen repaint over a picture under 33 ms on either chip: the best
render alone is 43 ms (ESP32-S3) and 63 ms (RP2350) for a pre-scaled backdrop with nothing on it. Fallbacks that
**do** change the look (the owner's call, brief "Stop and ask"):

- Modern without the backdrop under lists (art wash only on Now Playing, where nothing scrolls), or the backdrop dimmed to a
  flat colour taken from the art.
- Cover Flow: flat in both styles; or the angled Classic version drawn only at rest, flat while moving.
- Fewer animated frames: jump a row instead of sliding, at both styles' cost.

## Not yet done

- Nothing was checked by eye on the panel (the bench prints numbers only).
- A real JPEG decode at 160x160 (the backdrop numbers start from an already-decoded cover).
