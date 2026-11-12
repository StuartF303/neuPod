# ADR-0018: UI resolution profiles

Status: **accepted** (owner decision 11 Nov 2026)

## Context

The UI was written for the Phase 1 panel, 320x240: `UI_W` / `UI_H` / `UI_ROW_H` constants plus pixel literals in every screen,
Montserrat 14 / 20 / 48, two 320 x 24 render buffers. The Y2 Linux testbed (`testbed/y2`) has a 480x360 panel and runs the same
firmware as a hosted build. Two ways to get the UI there: render at 320x240 and scale the frame up (blurry text, wasted pixels, a
scaler in the display path), or render natively at 480x360.

## Decision

* **The UI is resolution-independent through a metrics profile.** `ui_metrics_t` (`app/ui/ui_metrics.h`) holds every pixel size
  and the fonts; `ui_task` selects the profile once from `hal_display_width()` / `hal_display_height()` (the largest compiled-in
  profile that fits; 320x240 otherwise). Screens read `UIM->...`; nothing else scales a length.
* **320x240 is the product panel** and its profile holds the exact numbers the UI had before: the host snapshots at that size are
  unchanged (compared byte for byte against the previous build, apart from the build hash text and timing animation).
* **480x360 is for the Y2 testbed** (and for testing the hires layout on the host): every length x 3/2, fonts one step up
  (Montserrat 14 to 20, 20 to 28, 48 stays; it is the largest size built; the fonts are the project's own Latin-1 builds, see [UI](../ui.md#fonts-and-display-text)), 9 rows per page in both.
* **Hires fonts and buffers are compiled only for hosted builds.** CMake option `NEUPOD_UI_HIRES` (default ON for `host` and `y2`,
  OFF for `rp2350` and `esp32s3`) gates the 480x360 profile, Montserrat 28 (`lv_conf.h`), the 480 x 36 render buffers, a 192 KB LVGL
  pool and the layout audit. The product build keeps its 72 KB pool and its 2 x 15 KB render buffers.
* **Native over scaling.** The owner chose native rendering on 11 Nov 2026.
* The host HAL display size is selectable at run time (`NEUPOD_DISPLAY_SIZE=WxH`, default 320x240), so the hires layout runs in
  the simulator and in CI. That, `hal_display.h` wording and nothing else changed under `hal/`.

## Consequences

* **RAM budget:** RP2350 unchanged. The render buffers, LVGL pool, list row pool (`UI_ROWS_MAX` = 9) and every other static
  allocation of the product build keep their sizes. Hosted builds: render buffers 2 x 34 KB, pool 192 KB (measured peak 69 KB across
  every tour at 480x360).
* **Flash budget (RP2350):** the product build has one profile, defined as a header constant, so every field read folds into an
  immediate and the metrics cost nothing; measured on 11 Nov 2026 (Release, `neupod.elf`): text 1,289,412 B before and after (0 B), .data 0 and .bss 489,348 B unchanged (the display, i2c and storage test images are byte-for-byte the same size).
  Montserrat 28 is now compiled out of the product build; it was enabled but unreferenced, so the linker had already dropped it (no size effect, it just keeps the guarantee explicit). An earlier version with a run-time pointer to the
  table grew the image by 2.8 KB, which is why the product build does not select a profile at run time.
* Layout is checked, not eyeballed: `tests/test_ui_metrics.c` (static invariants of every profile), the tours at 480x360 with the
  layout audit (`ui_audit.c`, `NEUPOD_UI_AUDIT=1`) and `hires_art_snapshot` (pixel checks of the art boxes).
* **Known limits at 480x360:** album art is still decoded to 64 x 64 (`ART_MAX_DIM`; raising it costs 8 KB per step of static RAM
  on the product build and changes the art snapshots) and scaled up 2.6x by LVGL; the splash wordmark stays at 48 pt; the bring-up
  scenes (`gfx_strip`, display tests) are 320x240 only.
* Another panel size needs one profile (one initialiser in `ui_metrics.c`) and, if it is larger than 480x360, bigger
  `UI_BUF_W_MAX` / `UI_BUF_LINES_MAX` and the host HAL's 640x480 ceiling raised.
* The input path is untouched: wheel, keys and the Y2 buttons all deliver the same events.
