# TJpgDec (Tiny JPEG Decompressor) R0.03

Baseline JPEG decoder by ChaN, used for album art (`app/audio/art.c`, core 0 only).

- **Source:** ChaN's TJpgDec R0.03 (https://elm-chan.org/fsw/tjpgd/). The copy here was taken from LVGL v9.6.0
  (`lib/lvgl/src/libs/tjpgd/`, the submodule this repository already pins), which is R0.03 plus restart-marker
  handling and a `jd_mcu_load()` / `jd_mcu_output()` split of the MCU loop. ChaN's own download page could not be
  fetched when this was vendored; replace with the upstream archive if exact provenance matters.
- **Licence:** ChaN's permissive BSD-style terms, in [LICENSE](LICENSE) ("no restriction on use ... personal,
  non-profit or commercial products"; redistributions of source must retain the copyright notice). Listed in
  `LICENSE.md`.
- **Changes from the LVGL copy:** (1) `#if LV_USE_TJPGD` guard and LVGL includes removed (`<stddef.h>` / `<stdint.h>`
  instead); (2) in `jd_mcu_output` the RGB bytes are emitted in R, G, B order again (LVGL's copy emits B, G, R, which
  made the RGB565 conversion swap red and blue) and (3) the 1/8-scale branch (one pixel per 8x8 block from the DC values)
  is restored, LVGL's copy has none and returns an empty image at that scale. Both (2) and (3) follow R0.03's
  published structure but were written here, not copied from ChaN's archive; `tests/test_art.c` checks the colour at every quadrant at every scale.
  `tjpgdcnf.h` is set to `JD_FORMAT 1` (RGB565), `JD_USE_SCALE 1`, `JD_FASTDECODE 1`, `JD_SZBUF 512`, `JD_TBLCLIP 1`.
- **Why level 1:** Cortex-M33 has the barrel shifter, level 2 adds 6 KB of Huffman tables for a small gain.
- **Limits:** baseline sequential JPEG only (grayscale, 4:4:4, 4:2:2 and 4:2:0). Progressive JPEG makes `jd_prepare`
  return `JDR_FMT3`; the caller shows the placeholder.
- **Work area:** the test pictures (4:2:0 with two quantisation tables) decode with a pool of 3,584 bytes and fail with 3,456
  (input buffer 512 B, quantisation and Huffman tables, MCU buffers); the caller uses 4,096 (`ART_POOL_BYTES` in `art.h`) for
  headroom (four tables, 4:2:2). Too small a pool reports `JDR_MEM1`, which the caller treats as "unsupported".
- LVGL's own `LV_USE_TJPGD` stays off; this copy is separate from LVGL's.
