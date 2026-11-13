// SPDX-License-Identifier: MPL-2.0
#include "y2_chargeview.h"

#include <string.h>

// Dark theme tokens (app/ui/ui_theme.c), repeated here because this mode links no LVGL.
#define C_BG 0xFF101010u
#define C_BAR 0xFF202020u
#define C_LINE 0xFF303030u
#define C_INK 0xFFF0F0F0u
#define C_INK2 0xFFA0A0A0u
#define C_GREEN 0xFF30B870u
#define C_AMBER 0xFFE89828u
#define C_RED 0xFFE84840u

bool y2_cview_is_key_press(const y2_event_t *ev) { return ev->type == Y2_EV_KEY && ev->value == 1; }

bool y2_cview_is_wake_event(const y2_event_t *ev) {
    if (ev->type == 0u || ev->type == 4u) { // EV_SYN, EV_MSC
        return false;
    }
    if (ev->type == Y2_EV_KEY) {
        return ev->value == 1; // a press; a release or auto-repeat alone is not one
    }
    return true; // wheel ticks, touch position
}

unsigned y2_cview_target_cells(unsigned pct) {
    if (pct > 100u) {
        return 0;
    }
    unsigned n = (pct + 19u) / 20u;
    return n > Y2_CVIEW_CELLS ? Y2_CVIEW_CELLS : n;
}

unsigned y2_cview_frame_count(unsigned target) { return target + 2u; }

unsigned y2_cview_cells_shown(unsigned frame, unsigned target, bool full) {
    if (full) {
        return Y2_CVIEW_CELLS;
    }
    return frame < target ? frame : target;
}

// 5x7 glyphs, 7 rows of 5 columns each ('#' lit). Only what the screen says: digits, %, and A C D E G H I L N O R S T.
typedef struct {
    char ch;
    const char *rows;
} glyph_t;

static const glyph_t k_font[] = {
    {'0', ".###.#...##..###.#.###..##...#.###."}, {'1', "..#...##....#....#....#....#...###."},
    {'2', ".###.#...#....#...#...#...#...#####"}, {'3', ".###.#...#....#..##.....##...#.###."},
    {'4', "...#...##..#.#.#..#.#####...#....#."}, {'5', "######....####.....#....##...#.###."},
    {'6', "..##..#...#....####.#...##...#.###."},   {'7', "#####....#...#...#...#....#....#..."},
    {'8', ".###.#...##...#.###.#...##...#.###."},   {'9', ".###.#...##...#.####....#...#..##.."},
    {'%', "##..###..#...#...#...#...#..###..##"},    {'-', "...............#####..............."},
    {'A', ".###.#...##...#######...##...##...#"},
    {'C', ".###.#...##....#....#....#...#.###."},   {'D', "####.#...##...##...##...##...#####."},
    {'E', "######....#....####.#....#....#####"},   {'G', ".###.#...##....#.####...##...#.####"},
    {'H', "#...##...##...#######...##...##...#"},   {'I', ".###...#....#....#....#....#...###."},
    {'L', "#....#....#....#....#....#....#####"},   {'N', "#...###..##.#.##..###...##...##...#"},
    {'O', ".###.#...##...##...##...##...#.###."},   {'R', "####.#...##...#####.#.#..#..#.#...#"},
    {'S', ".###.#...##.....###.....##...#.###."},   {'T', "#####..#....#....#....#....#....#.."},
};

static const char *glyph_rows(char c) {
    for (size_t i = 0; i < sizeof k_font / sizeof k_font[0]; i++) {
        if (k_font[i].ch == c) {
            return k_font[i].rows;
        }
    }
    return NULL; // space and anything unknown: blank
}

typedef struct {
    uint32_t *px;
    unsigned w, h, stride;
} canvas_t;

static void fill_rect(const canvas_t *cv, int x, int y, int w, int h, uint32_t c) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > (int)cv->w) {
        w = (int)cv->w - x;
    }
    if (y + h > (int)cv->h) {
        h = (int)cv->h - y;
    }
    for (int j = 0; j < h; j++) {
        uint32_t *row = cv->px + (size_t)(y + j) * cv->stride + (unsigned)x;
        for (int i = 0; i < w; i++) {
            row[i] = c;
        }
    }
}

static int text_width(const char *s, int scale) {
    int n = (int)strlen(s);
    return n > 0 ? n * 6 * scale - scale : 0;
}

static void draw_text(const canvas_t *cv, const char *s, int cx, int y, int scale, uint32_t c) {
    int x = cx - text_width(s, scale) / 2;
    for (; *s != '\0'; s++, x += 6 * scale) {
        const char *g = glyph_rows(*s);
        if (g == NULL) {
            continue;
        }
        for (int r = 0; r < 7; r++) {
            for (int col = 0; col < 5; col++) {
                if (g[r * 5 + col] == '#') {
                    fill_rect(cv, x + col * scale, y + r * scale, scale, scale, c);
                }
            }
        }
    }
}

static int imax(int a, int b) { return a > b ? a : b; }

void y2_cview_render(uint32_t *px, unsigned w, unsigned h, unsigned stride_px, const y2_cview_scene_t *s) {
    canvas_t cv = {px, w, h, stride_px};
    fill_rect(&cv, 0, 0, (int)w, (int)h, C_BG);

    bool unknown = s->pct == Y2_CVIEW_PCT_UNKNOWN;
    unsigned pct = s->pct > 100u ? 100u : s->pct;
    unsigned target = s->full ? Y2_CVIEW_CELLS : y2_cview_target_cells(s->pct);
    unsigned lit = y2_cview_cells_shown(s->frame, target, s->full);
    uint32_t fill = s->full ? C_GREEN : (pct <= Y2_CVIEW_LOW_PCT ? C_RED : C_GREEN);

    int bw = (int)w / 2;
    int bh = bw * 5 / 9;
    int t = imax(2, (int)h / 60);              // outline
    int scale_big = imax(2, (int)h / 45);      // percentage
    int scale_small = imax(1, (int)h / 90);    // caption
    int gap = (int)h / 18;
    int total = bh + gap + 7 * scale_big + gap / 2 + 7 * scale_small;
    int top = ((int)h - total) / 2;
    if (top < 0) {
        top = 0;
    }
    int left = ((int)w - bw) / 2;
    int cx = (int)w / 2;

    // Body outline, the terminal nub on the right, a dark well inside.
    int nub_w = imax(3, bw / 14);
    int nub_h = bh / 3;
    fill_rect(&cv, left + bw, top + (bh - nub_h) / 2, nub_w, nub_h, C_INK2);
    fill_rect(&cv, left, top, bw, bh, C_INK2);
    fill_rect(&cv, left + t, top + t, bw - 2 * t, bh - 2 * t, C_BAR);

    // Segments.
    int pad = t + t / 2 + 1;
    int inner_w = bw - 2 * pad;
    int cell_gap = imax(2, t);
    int cell_w = (inner_w - (int)(Y2_CVIEW_CELLS - 1u) * cell_gap) / (int)Y2_CVIEW_CELLS;
    int cell_h = bh - 2 * pad;
    int x0 = left + pad + (inner_w - ((int)Y2_CVIEW_CELLS * cell_w + (int)(Y2_CVIEW_CELLS - 1u) * cell_gap)) / 2;
    for (unsigned i = 0; i < Y2_CVIEW_CELLS; i++) {
        fill_rect(&cv, x0 + (int)i * (cell_w + cell_gap), top + pad, cell_w, cell_h, i < lit ? fill : C_LINE);
    }

    char buf[8];
    unsigned p = pct;
    int n = 0;
    if (unknown) {
        buf[n++] = '-';
        buf[n++] = '-';
    } else if (p >= 100u) {
        buf[n++] = '1';
        buf[n++] = '0';
        buf[n++] = '0';
    } else {
        if (p >= 10u) {
            buf[n++] = (char)('0' + p / 10u);
        }
        buf[n++] = (char)('0' + p % 10u);
    }
    buf[n++] = '%';
    buf[n] = '\0';
    int ty = top + bh + gap;
    draw_text(&cv, buf, cx, ty, scale_big, C_INK);

    int cy = ty + 7 * scale_big + gap / 2;
    if (s->hold_ms > 0u) {
        draw_text(&cv, "HOLD TO RESTART", cx, cy, scale_small, C_AMBER);
        uint32_t hm = s->hold_ms > Y2_CVIEW_HOLD_FULL_MS ? Y2_CVIEW_HOLD_FULL_MS : s->hold_ms;
        int by = cy + 7 * scale_small + scale_small * 2;
        int bar_h = imax(2, t / 2);
        fill_rect(&cv, left, by, bw, bar_h, C_LINE);
        fill_rect(&cv, left, by, (int)((uint32_t)bw * hm / Y2_CVIEW_HOLD_FULL_MS), bar_h, C_AMBER);
    } else {
        draw_text(&cv, s->full ? "CHARGED" : "CHARGING", cx, cy, scale_small, s->full ? C_GREEN : C_INK2);
    }
}

void y2_cview_init(y2_cview_t *v, uint32_t now_ms) {
    memset(v, 0, sizeof *v);
    v->awake = true;
    v->wake_ms = v->anim_ms = v->session_ms = v->motion_ok_ms = now_ms;
}

static void wake_from_dark(y2_cview_t *v, uint32_t now_ms) {
    v->awake = true;
    v->have_drawn = false; // the panel content is stale; draw before lighting it
    v->anim_ms = now_ms;   // the animation restarts from empty only when the screen was dark, not on every extension
    v->session_ms = now_ms;
}

void y2_cview_wake(y2_cview_t *v, uint32_t now_ms) {
    if (!v->awake) {
        wake_from_dark(v, now_ms);
    }
    v->wake_ms = now_ms;
    v->session_ms = now_ms;
    v->motion_ok_ms = now_ms;
}

void y2_cview_wake_motion(y2_cview_t *v, uint32_t now_ms) {
    if (!v->awake) {
        if ((int32_t)(now_ms - v->motion_ok_ms) < 0) {
            return; // recently cut off for endless motion
        }
        wake_from_dark(v, now_ms);
        v->wake_ms = now_ms;
        return;
    }
    if ((uint32_t)(now_ms - v->session_ms) >= Y2_CVIEW_MAX_MOTION_MS) {
        v->motion_ok_ms = now_ms + Y2_CVIEW_MAX_MOTION_MS; // no more extension; it goes dark within 10 s, then stays so
        return;
    }
    v->wake_ms = now_ms;
}

bool y2_cview_is_awake(const y2_cview_t *v, uint32_t now_ms) {
    return v->awake && (uint32_t)(now_ms - v->wake_ms) < Y2_CVIEW_AWAKE_MS;
}

unsigned y2_cview_tick(y2_cview_t *v, uint32_t now_ms, y2_cview_scene_t *scene) {
    if (!v->awake) {
        return 0;
    }
    if (!y2_cview_is_awake(v, now_ms)) {
        v->awake = false;
        return Y2_CVIEW_BACKLIGHT_OFF;
    }
    unsigned mask = 0;
    unsigned target = scene->full ? Y2_CVIEW_CELLS : y2_cview_target_cells(scene->pct);
    scene->frame = scene->full ? 0u : ((now_ms - v->anim_ms) / Y2_CVIEW_FRAME_MS) % y2_cview_frame_count(target);
    // Quantise the hold bar so it redraws about every 100 ms, not every pass.
    scene->hold_ms = scene->hold_ms / 100u * 100u + (scene->hold_ms > 0u && scene->hold_ms < 100u ? 100u : 0u);
    if (!v->have_drawn) {
        mask |= Y2_CVIEW_BACKLIGHT_ON; // drawn first by the caller, lit after
    }
    if (!v->have_drawn || scene->pct != v->drawn.pct || scene->full != v->drawn.full || scene->frame != v->drawn.frame ||
        scene->hold_ms != v->drawn.hold_ms) {
        mask |= Y2_CVIEW_DRAW;
        v->drawn = *scene;
        v->have_drawn = true;
    }
    return mask;
}
