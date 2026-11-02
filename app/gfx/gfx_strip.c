// SPDX-License-Identifier: MPL-2.0
#include "gfx_strip.h"

// Two strips: one is rendered while the other is on the wire.
static hal_pixel_t g_strip[2][GFX_STRIP_W * GFX_STRIP_H];

const gfx_sink_t gfx_sink_display = {
    .set_window = hal_display_set_window,
    .blit = hal_display_blit,
    .wait = hal_display_wait,
};

hal_status_t gfx_render_frame(uint16_t w, uint16_t h, gfx_draw_fn draw, void *ctx,
                              const gfx_sink_t *sink) {
    if (draw == NULL || sink == NULL || w == 0 || h == 0 || w > GFX_STRIP_W) {
        return HAL_EINVAL;
    }
    unsigned which = 0;
    hal_status_t st = HAL_OK;
    for (uint16_t y = 0; y < h && st == HAL_OK; y = (uint16_t)(y + GFX_STRIP_H)) {
        uint16_t rows = (uint16_t)((unsigned)(h - y) < GFX_STRIP_H ? (unsigned)(h - y) : GFX_STRIP_H);
        gfx_canvas_t strip;
        // g_strip[which] was last sent two strips ago; the previous strip is still in flight
        // in the other buffer and is not touched until the next iteration.
        gfx_canvas_init(&strip, g_strip[which], 0, (int16_t)y, w, rows);
        draw(&strip, ctx);
        st = sink->set_window(0, y, w, rows); // waits for the previous strip's transfer
        if (st == HAL_OK) {
            st = sink->blit(g_strip[which], (size_t)w * rows);
        }
        which ^= 1u;
    }
    sink->wait();
    return st;
}

hal_status_t gfx_present_frame(gfx_draw_fn draw, void *ctx) {
    hal_status_t st = gfx_render_frame(hal_display_width(), hal_display_height(), draw, ctx,
                                       &gfx_sink_display);
    if (st == HAL_OK) {
        hal_display_present();
    }
    return st;
}
