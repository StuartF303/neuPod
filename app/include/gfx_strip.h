// SPDX-License-Identifier: MPL-2.0
/**
 * @file gfx_strip.h
 * @brief Strip renderer: draw a full frame as 320x16 strips, double-buffered.
 *
 * Uses two static strip buffers (2 x 320 x 16 x 2 B = 20 KB) instead of a 150 KB framebuffer.
 * Strip N+1 is rendered while strip N is still being sent by DMA. The scene callback is
 * invoked once per strip with a canvas positioned at that strip; it just draws the whole scene
 * with panel coordinates and the primitives clip.
 */
#ifndef NEUPOD_GFX_STRIP_H
#define NEUPOD_GFX_STRIP_H

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_STRIP_W 320u
#define GFX_STRIP_H 16u

/** Scene callback: draw the scene into @p strip (panel coordinates; clipped). */
typedef void (*gfx_draw_fn)(gfx_canvas_t *strip, void *ctx);

/** Where finished strips go. hal_display_* for the device; a fake in unit tests. */
typedef struct {
    hal_status_t (*set_window)(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    /** May return before the data is consumed; set_window()/wait() must wait for it. */
    hal_status_t (*blit)(const hal_pixel_t *pixels, size_t count);
    void (*wait)(void);
} gfx_sink_t;

/** The real display: hal_display_set_window / blit / wait. */
extern const gfx_sink_t gfx_sink_display;

/**
 * Render a @p w x @p h frame (w <= GFX_STRIP_W) strip by strip into @p sink and wait for the
 * last strip. Returns HAL_EINVAL for bad sizes, or the first sink error.
 */
hal_status_t gfx_render_frame(uint16_t w, uint16_t h, gfx_draw_fn draw, void *ctx,
                              const gfx_sink_t *sink);

/** gfx_render_frame() to the panel (full width/height) and hal_display_present(). */
hal_status_t gfx_present_frame(gfx_draw_fn draw, void *ctx);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_GFX_STRIP_H
