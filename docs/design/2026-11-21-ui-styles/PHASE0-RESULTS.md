# Phase 0 results: effect timings on the AMOLED boards

Measured 21 November 2026 for [IMPLEMENTATION-BRIEF.md](IMPLEMENTATION-BRIEF.md), phase 0. The bench code is throwaway and is
not merged: branch `chore/phase0-bench` (`app/ui/ui_bench.c`, CMake option `NEUPOD_UI_BENCH`).

| Board | Status |
| --- | --- |
| ESP32-S3-Touch-AMOLED-2.41 (ESP32-S3R8, 240 MHz, 8 MB octal PSRAM) | **Measured** (below) |
| RP2350-Touch-AMOLED-2.41 (RP2350B, 8 MB PSRAM) | **Not measured: board not attached.** The bench image builds and links (`build/rp2350-amoled241-bench`) |

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

## What this means for the design

At 30 fps the budget is 33 ms a frame. On the ESP32-S3 as it is configured today:

- **Classic Light is affordable** for lists, the selection bar and the status bar. Its Cover Flow is not: the angled version
  draws at under 3 fps, the flat one at about 5.
- **Modern Dark is not affordable as drawn.** Every scroll over the backdrop repaints the screen (6 fps). Panels and
  translucent chrome over a picture are around 13 fps.

Most of the cost is software rendering on one core, plus a flush that blocks it. Engineering options exist that do
**not** change the look; none has been tried yet:

1. Overlap the flush with rendering (asynchronous DMA flush, both render buffers in use). Saves up to the 53 ms flush per frame.
2. A second LVGL draw unit on the idle core (`LV_DRAW_SW_DRAW_UNIT_CNT 2` with an RTOS `LV_USE_OS`). Up to about half the render time.
3. QSPI at 80 MHz, if the RM690B0 and the board allow it (not checked).
4. Always pre-scale the backdrop to 600x450 per track (119 ms once), never let LVGL scale it per frame.
5. Bigger render buffers in internal RAM, and composing the static backdrop once with the list ground baked in, so a scroll
   blends rows over a flat picture instead of picture plus a 40 % layer.

Even all five together are unlikely to bring a full-screen repaint over a picture under 33 ms on this chip. Fallbacks that
**do** change the look (the owner's call, brief "Stop and ask"):

- Modern without the backdrop under lists (art wash only on Now Playing, where nothing scrolls), or the backdrop dimmed to a
  flat colour taken from the art.
- Cover Flow: flat in both styles; or the angled Classic version drawn only at rest, flat while moving.
- Fewer animated frames: jump a row instead of sliding, at both styles' cost.

## Not yet done

- RP2350-Touch-AMOLED-2.41: every test above. Needs the board on USB.
- Nothing was checked by eye on the panel (the bench prints numbers only).
- A real JPEG decode at 160x160 (the backdrop numbers start from an already-decoded cover).
