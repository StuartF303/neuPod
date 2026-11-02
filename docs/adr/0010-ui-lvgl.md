# ADR-0010: UI toolkit — LVGL v9

## Status

Accepted (decided by the project owner)

## Context

The UI (split menu, lists, now-playing, settings, carousel) needs a rendering and animation
toolkit that runs on a 125 MHz Cortex-M33 with a 320x240 SPI panel, and on the desktop for
development.

## Decision

- **LVGL v9.6.0** (MIT), submodule `lib/lvgl`, every `.c` under `src/` compiled with
  `app/ui/lv_conf.h` (derived from the v9.6.0 template).
- **One task only.** LVGL runs in the UI task (`LV_USE_OS = LV_OS_NONE`); other tasks reach it via
  the capabilities struct, the battery store (mutex) and the input queue. This avoids LVGL's
  global lock and keeps its non-thread-safe API single-threaded.
- **Rendering:** RGB565, partial mode, two 320x24 buffers (15 KB each); the flush callback sets the
  panel window, DMA-blits and waits (blocking for now; the second buffer allows async later);
  tick from the RTOS tick; its heap is a 64 KB static pool.
- **Fonts and widgets:** Montserrat 14, 20, 28, 48 and the built-in symbols; only label, image,
  arc, bar and line widgets are enabled. Enable more when stage 2 needs them.
- **Host:** the same LVGL renders into the host display framebuffer (PPM dump or SDL window).

## Consequences

- Measured on the RP2350 build: about 0.45 MB of flash (code plus the four fonts) and 94 KB of
  static RAM (64 KB pool + two 15 KB render buffers).
- Assertions in LVGL go to the common panic path.
- LVGL's symbol glyphs come from Font Awesome (CC BY 4.0 / OFL): recorded in `LICENSE.md`.
- The drawing code must never be called from another task; use the queues.
