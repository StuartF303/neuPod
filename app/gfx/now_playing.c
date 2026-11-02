// SPDX-License-Identifier: MPL-2.0
#include "now_playing.h"

#include <stdio.h>

static void fmt_time(char *out, size_t n, uint32_t s) {
    snprintf(out, n, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
}

void now_playing_draw(gfx_canvas_t *c, void *ctx) {
    const now_playing_t *np = ctx;
    const gfx_font_t *f = np->font;
    int lh = f->height;
    int w = 320;
    int margin = 16;

    gfx_clear(c, np->bg);

    // Header strip with a mock battery icon.
    gfx_text(c, f, margin, 8, "Now Playing", np->fg);
    gfx_rect(c, w - margin - 26, 8, 24, lh - 2, np->fg);
    gfx_fill_rect(c, w - margin - 24, 10, 16, lh - 6, np->fg);
    gfx_fill_rect(c, w - margin - 2, 8 + 3, 2, lh - 8, np->fg);
    gfx_hline(c, margin, 8 + lh + 4, w - 2 * margin, np->fg);

    // Album art placeholder (concentric rings, offset so a mirrored screen is obvious).
    int art = 96;
    int ax = margin;
    int ay = 56;
    gfx_rect(c, ax, ay, art, art, np->fg);
    gfx_circle(c, ax + art / 2, ay + art / 2, 38, np->fg);
    gfx_circle(c, ax + art / 2, ay + art / 2, 24, np->fg);
    gfx_fill_circle(c, ax + art / 2, ay + art / 2, 10, np->fg);

    // Track text, to the right of the art.
    int tx = ax + art + 16;
    gfx_text(c, f, tx, ay + 4, np->title, np->fg);
    gfx_text(c, f, tx, ay + 4 + lh + 6, np->artist, np->fg);
    gfx_text(c, f, tx, ay + 4 + 2 * (lh + 6), np->album, np->fg);

    // Progress bar + elapsed / remaining.
    int bx = margin;
    int bw = w - 2 * margin;
    int by = 184;
    uint32_t total = np->total_s ? np->total_s : 1;
    uint32_t el = np->elapsed_s > total ? total : np->elapsed_s;
    gfx_rect(c, bx, by, bw, 10, np->fg);
    gfx_fill_rect(c, bx + 2, by + 2, (int)((uint32_t)(bw - 4) * el / total), 6, np->fg);

    char t[16];
    fmt_time(t, sizeof t, np->elapsed_s);
    gfx_text(c, f, bx, by + 18, t, np->fg);
    char rem[20];
    char tmp[16];
    fmt_time(tmp, sizeof tmp, np->total_s > np->elapsed_s ? np->total_s - np->elapsed_s : 0);
    snprintf(rem, sizeof rem, "-%s", tmp);
    gfx_text(c, f, bx + bw - gfx_text_width(f, rem), by + 18, rem, np->fg);
}
