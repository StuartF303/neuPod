// SPDX-License-Identifier: MPL-2.0
/**
 * @file gfx.h
 * @brief Portable 2D drawing into a canvas (a window onto the panel). No malloc, no SDK.
 *
 * Every primitive takes PANEL coordinates and clips to the canvas, so the same scene callback
 * can draw into a full-size buffer, a small buffer, or one 320x16 strip at a time. Pixels are
 * native-endian RGB565 (hal_pixel_t), exactly what hal_display_blit() expects.
 */
#ifndef NEUPOD_GFX_H
#define NEUPOD_GFX_H

#include "gfx_font.h"
#include "hal_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A buffer covering panel rectangle [x0, x0+w) x [y0, y0+h); row stride is exactly @c w. */
typedef struct {
    hal_pixel_t *buf;
    int16_t x0, y0;
    uint16_t w, h;
} gfx_canvas_t;

/** Fill in a canvas descriptor. */
void gfx_canvas_init(gfx_canvas_t *c, hal_pixel_t *buf, int16_t x0, int16_t y0, uint16_t w,
                     uint16_t h);

/** Fill the whole canvas. */
void gfx_clear(gfx_canvas_t *c, hal_pixel_t colour);
void gfx_pixel(gfx_canvas_t *c, int x, int y, hal_pixel_t colour);
/** Horizontal line of @p len pixels starting at (x, y). */
void gfx_hline(gfx_canvas_t *c, int x, int y, int len, hal_pixel_t colour);
void gfx_vline(gfx_canvas_t *c, int x, int y, int len, hal_pixel_t colour);
/** Bresenham line, both end points included. */
void gfx_line(gfx_canvas_t *c, int x0, int y0, int x1, int y1, hal_pixel_t colour);
/** 1-px outline of the w x h rectangle at (x, y). */
void gfx_rect(gfx_canvas_t *c, int x, int y, int w, int h, hal_pixel_t colour);
void gfx_fill_rect(gfx_canvas_t *c, int x, int y, int w, int h, hal_pixel_t colour);
/** Midpoint circle outline, centre (cx, cy), radius r. */
void gfx_circle(gfx_canvas_t *c, int cx, int cy, int r, hal_pixel_t colour);
void gfx_fill_circle(gfx_canvas_t *c, int cx, int cy, int r, hal_pixel_t colour);

/**
 * Copy a w x h image with its top-left at (x, y). @p src is NATIVE-endian RGB565 (row-major,
 * stride w), i.e. the same format as hal_display_blit() -- the format tools/png2rgb565.py
 * writes by default.
 */
void gfx_image(gfx_canvas_t *c, int x, int y, const hal_pixel_t *src, int w, int h);

/** Advance width of @p s in pixels (unknown characters count as '?'). */
int gfx_text_width(const gfx_font_t *f, const char *s);
/** Draw @p s with its top-left at (x, y); only glyph pixels are written (transparent bg). */
void gfx_text(gfx_canvas_t *c, const gfx_font_t *f, int x, int y, const char *s, hal_pixel_t fg);
/** As gfx_text() but every glyph cell is filled with @p bg first (opaque). */
void gfx_text_bg(gfx_canvas_t *c, const gfx_font_t *f, int x, int y, const char *s, hal_pixel_t fg,
                 hal_pixel_t bg);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_GFX_H
