// SPDX-License-Identifier: MPL-2.0
/**
 * @file now_playing.h
 * @brief Mock "Now Playing" screen (portable) used by the display tests and the host viewer.
 */
#ifndef NEUPOD_NOW_PLAYING_H
#define NEUPOD_NOW_PLAYING_H

#include "gfx.h"
#include "gfx_strip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *title, *artist, *album;
    uint32_t elapsed_s, total_s;
    const gfx_font_t *font;
    hal_pixel_t fg, bg;
} now_playing_t;

/** Draw the 320x240 screen into @p c (a gfx_draw_fn: ctx is a const now_playing_t *). */
void now_playing_draw(gfx_canvas_t *c, void *ctx);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_NOW_PLAYING_H
