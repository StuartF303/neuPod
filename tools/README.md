# tools/

Small Python helpers (Pillow only, no numpy). Run from the repo root.

## Audio test vectors

`gen_seek_vector.py tests/assets/audio` adds a SEEKTABLE to `flac16_44k_st.flac` (`flac16_44k_st_seek.flac`, for the resume-seek tests); see also `gen_trim_vectors.py`.

## Display assets and fonts

| Script | Purpose |
|--------|---------|
| `gen_font.py` | Regenerates `app/gfx/fonts/font8x8.c` (from `tools/data/font8x8_basic.h`, public domain) and `font16.c` (DejaVu Sans rendered 1-bit, `--px`, `--ttf`). Licences: `app/gfx/fonts/README.md`. |
| `gen_test_image.py` | Draws `app/tests/assets/test_image_640x480.png` (gradient + vignette, colour wheel, arrow, colour patches). |
| `png2rgb565.py` | PNG to a C array of RGB565 (`.c` + `.h`). `--downscale2` (2x box filter), `--width/--height` (size check), `--name`, `--swap`. |

Regenerate the shipped test image:

    python3 tools/gen_test_image.py
    python3 tools/png2rgb565.py app/tests/assets/test_image_640x480.png \
        -o app/tests/assets/test_image.c --name test_img --downscale2 --width 320 --height 240

### Byte order (`--swap`)

The RP2350 driver runs SPI1 in 16-bit frames, which send the high byte of each `uint16_t` first,
as the ST7789 expects. Arrays in this repo are therefore **native** `uint16_t` RGB565 and the
default is **no swap**; use them directly with `hal_display_blit()` / `gfx_image()`.
`--swap` (byte-swapped) is only for pushing the array as raw bytes over an 8-bit SPI path on a
little-endian MCU. Never use it for arrays this repo's driver consumes.
