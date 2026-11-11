// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_fb.h
 * @brief Y2 framebuffer helpers, portable half: RGB565 -> XRGB8888 conversion, mode check and the
 *        round-robin page bookkeeping. The ioctl/mmap side is in hal_display.c. Unit-tested on the PC.
 *
 * mtkfb: 480x360 visible, 3 pages in the virtual area, 32 bpp XRGB8888 (red 16/8, green 8/8, blue 0/8,
 * transp 24/8, alpha written as 0xFF). Pages are shown by panning; writes that are not followed by a pan are
 * not guaranteed to reach the panel, so every present is a flip to the next page.
 */
#ifndef NEUPOD_Y2_FB_H
#define NEUPOD_Y2_FB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Y2_FB_MAX_PAGES 4u

/** Expand RGB565 to XRGB8888 with alpha 0xFF (low bits replicate the high bits, so white is 0xFFFFFFFF). */
uint32_t y2_fb_expand565(uint16_t p);
void y2_fb_convert_rgb565(const uint16_t *src, uint32_t *dst, size_t pixels);

/** One fb_bitfield: offset and length in bits. */
typedef struct {
    uint32_t offset, length;
} y2_fb_field_t;

/** True for exactly the layout the converter writes: 32 bpp, red 16/8, green 8/8, blue 0/8, transp 24/8. */
bool y2_fb_mode_is_xrgb8888(uint32_t bits_per_pixel, y2_fb_field_t red, y2_fb_field_t green, y2_fb_field_t blue,
                            y2_fb_field_t transp);

/** Whole pages that fit in both the mapped memory and the driver's virtual height (at least 1, at most Y2_FB_MAX_PAGES). */
unsigned y2_fb_page_count(size_t map_len, uint32_t yres_virtual, uint32_t line_length, uint32_t yres);

/** Page that follows @p cur in the round robin (0 if @p pages is 0 or 1). */
unsigned y2_fb_next_page(unsigned cur, unsigned pages);

/** Rows [lo, hi) of a page that no longer match the shadow frame; empty when lo >= hi. */
typedef struct {
    uint16_t lo, hi;
} y2_rows_t;

/** Page flipping with per-page stale-row tracking: only rows changed since a page was last written are converted. */
typedef struct {
    y2_rows_t stale[Y2_FB_MAX_PAGES];
    unsigned pages;
    unsigned visible; /**< page on screen now */
    uint16_t height;
} y2_pager_t;

/** Start with @p visible on screen; every page counts as entirely stale (their contents are unknown). */
void y2_pager_init(y2_pager_t *p, unsigned pages, unsigned visible, uint16_t height);

/** Rows [y0, y1) of the shadow frame changed: every page is stale there. Clamped to the height. */
void y2_pager_mark(y2_pager_t *p, uint16_t y0, uint16_t y1);

/** Mark the whole frame stale (wake from sleep, mode change). */
void y2_pager_mark_all(y2_pager_t *p);

/**
 * Choose the page to draw and show next. Returns false (and changes nothing) if nothing is stale on the next
 * page. On true: @p page is the page to convert @p rows into, then pan to; it becomes the visible page.
 */
bool y2_pager_next(y2_pager_t *p, unsigned *page, y2_rows_t *rows);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_FB_H
