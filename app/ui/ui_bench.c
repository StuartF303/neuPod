// SPDX-License-Identifier: MPL-2.0
// THROWAWAY phase-0 measurement mode (docs/design/2026-11-21-ui-styles/IMPLEMENTATION-BRIEF.md, "Phase 0"). Not merged. Built only with
// -DNEUPOD_UI_BENCH=ON; ui.c calls ui_bench_main() after the splash instead of pushing Home. Runs in the UI task, so every number includes
// the real flush path. Loops forever: all tests, "bench: done", 2 s gap, again.
#ifdef NEUPOD_UI_BENCH

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal_display.h"
#include "hal_system.h"
#include "os.h"
#include "ui_bench.h"
#include "ui_internal.h"

#ifndef UI_BENCH_MS
#define UI_BENCH_MS 5000u /* measured window per frame test */
#endif
#define WARM_MS 300u
#define MAX_FRAMES 2048u
#define FX 256

__attribute__((weak)) void *ui_bench_alloc(size_t bytes, bool psram) {
    (void)psram;
    return malloc(bytes);
}
__attribute__((weak)) void ui_bench_free(void *p, bool psram) {
    (void)psram;
    free(p);
}

static lv_obj_t *g_scr;
static uint32_t g_rendered;
static uint32_t g_dur[MAX_FRAMES];

static uint16_t *g_bg;       /* 600x450 synthetic picture, PSRAM */
static uint16_t *g_tile;     /* 160x160 cover */
static uint16_t *g_tile_flip; /* the same flipped vertically (the reflection source) */
static lv_image_dsc_t g_bg_dsc, g_tile_dsc, g_flip_dsc;

static int32_t W(void) { return UIM->w; }
static int32_t H(void) { return UIM->h; }
static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }
static bool area_isect(lv_area_t *r, const lv_area_t *a, const lv_area_t *b) {
    r->x1 = imax(a->x1, b->x1);
    r->y1 = imax(a->y1, b->y1);
    r->x2 = imin(a->x2, b->x2);
    r->y2 = imin(a->y2, b->y2);
    return r->x1 <= r->x2 && r->y1 <= r->y2;
}
static int32_t lerp(int32_t a, int32_t b, int32_t f) { return a + (b - a) * f / FX; }

static uint16_t pack(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)); }

// ---- synthetic pictures ----

static void synth(uint16_t *px, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r = x * 255 / w, g = y * 255 / h, b = ((x * 7) ^ (y * 5)) & 255;
            int dx = x - w / 2, dy = y - h / 2;
            int t = (dx * dx + dy * dy) * 64 / (w * w / 4 + 1);
            if ((t & 7) < 3) {
                r = 255 - r;
                g = 255 - g;
            }
            if (((x / 20) + (y / 20)) & 1) {
                b = b / 2 + 64;
            }
            px[y * w + x] = pack(r, g, b);
        }
    }
}

static void dsc_init(lv_image_dsc_t *d, const uint16_t *px, int w, int h) {
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = (uint32_t)w;
    d->header.h = (uint32_t)h;
    d->header.stride = (uint32_t)w * 2u;
    d->data_size = (uint32_t)w * (uint32_t)h * 2u;
    d->data = (const uint8_t *)px;
}

// ---- harness ----

static void on_render(lv_event_t *e) {
    (void)e;
    g_rendered++;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static void print_cpu(void) {
    static os_task_info_t info[OS_STATS_MAX_TASKS];
    size_t n = os_task_stats(info, OS_STATS_MAX_TASKS);
    printf("bench: cpu%%:");
    for (size_t i = 0; i < n; i++) {
        if (info[i].cpu_permille >= 5u) {
            printf(" %s=%u.%u", info[i].name, (unsigned)(info[i].cpu_permille / 10u), (unsigned)(info[i].cpu_permille % 10u));
        }
    }
    printf("\n");
}

typedef void (*step_fn)(uint32_t i, uint32_t ms);

static void reset_screen(void) {
    lv_obj_clean(g_scr);
    lv_obj_set_style_bg_color(g_scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(g_scr, false);
}

// Runs step() before every pass for WARM_MS + UI_BENCH_MS; counts the passes where LVGL rendered (LV_EVENT_RENDER_READY).
extern uint64_t g_bench_flush_us;
static void run(const char *name, step_fn step) {
    lv_refr_now(NULL);
    (void)os_task_stats((os_task_info_t[OS_STATS_MAX_TASKS]){0}, OS_STATS_MAX_TASKS); /* start a cpu window */
    uint64_t t_begin = hal_micros(), t_win = 0;
    uint32_t i = 0, frames = 0, stored = 0, max = 0;
    uint64_t sum = 0, busy = 0, flush = 0;
    for (;;) {
        uint32_t ms = (uint32_t)((hal_micros() - t_begin) / 1000u);
        if (ms >= WARM_MS + UI_BENCH_MS) {
            break;
        }
        step(i++, ms);
        uint32_t r0 = g_rendered;
        uint64_t f0 = g_bench_flush_us;
        uint64_t a = hal_micros();
        (void)lv_timer_handler();
        uint32_t dt = (uint32_t)(hal_micros() - a);
        hal_display_service();
        if (ms >= WARM_MS) {
            if (t_win == 0) {
                t_win = a;
            }
            busy += dt;
            flush += g_bench_flush_us - f0;
            if (g_rendered != r0) {
                frames++;
                sum += dt;
                max = dt > max ? dt : max;
                if (stored < MAX_FRAMES) {
                    g_dur[stored++] = dt;
                }
            }
        }
        os_delay_ms(1);
    }
    uint64_t wall = hal_micros() - (t_win != 0 ? t_win : t_begin);
    if (wall == 0) {
        wall = 1;
    }
    uint32_t p95 = 0;
    if (stored != 0) {
        qsort(g_dur, stored, sizeof g_dur[0], cmp_u32);
        p95 = g_dur[(stored * 95u) / 100u >= stored ? stored - 1u : (stored * 95u) / 100u];
    }
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    uint32_t fps10 = (uint32_t)((uint64_t)frames * 10000000u / wall);
    printf("bench: %s frames=%u fps=%u.%u frame_us avg=%u max=%u p95=%u busy=%u%% flush_us/frame=%u lvgl_mem_max=%u\n", name, (unsigned)frames,
           (unsigned)(fps10 / 10u), (unsigned)(fps10 % 10u), (unsigned)(frames != 0 ? sum / frames : 0), (unsigned)max, (unsigned)p95,
           (unsigned)(busy * 100u / wall), (unsigned)(frames != 0 ? flush / frames : 0), (unsigned)m.max_used);
    print_cpu();
}

// ---- shared pieces ----

static lv_obj_t *plain(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
}

static void rows(lv_obj_t *parent, int n, int32_t y0) {
    const int32_t rh = UIM->row_h;
    const int32_t fh = lv_font_get_line_height(UIM->font_body);
    for (int r = 0; r < n; r++) {
        lv_obj_t *l = lv_label_create(parent);
        lv_obj_set_style_text_font(l, UIM->font_body, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_label_set_text_fmt(l, "%02d  Track title number %d", (r % 14) + 1, (r % 14) + 1);
        lv_obj_set_pos(l, UIM->row_inset, y0 + r * rh + (rh - fh) / 2);
    }
}

static lv_obj_t *image_obj(lv_obj_t *parent, const lv_image_dsc_t *d, int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_obj_t *im = lv_image_create(parent);
    lv_image_set_src(im, d);
    lv_image_set_inner_align(im, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_pos(im, x, y);
    lv_obj_set_size(im, w, h);
    return im;
}

// ---- 1. grad_bar ----

static lv_grad_dsc_t g_grad;
static lv_obj_t *g_bar;

static void grad_bar_step(uint32_t i, uint32_t ms) {
    (void)ms;
    lv_obj_set_y(g_bar, UIM->status_h + (int32_t)(i % (uint32_t)UIM->rows) * UIM->row_h);
}

static void test_grad_bar(void) {
    reset_screen();
    const lv_color_t cols[4] = {lv_color_hex(0x6db3f5), lv_color_hex(0x2f80e0), lv_color_hex(0x1b62cf), lv_color_hex(0x1858c8)};
    const uint8_t fr[4] = {0, 128, 130, 255}; /* hard step between 50 % and 51 % */
    memset(&g_grad, 0, sizeof g_grad);
    lv_grad_init_stops(&g_grad, cols, NULL, fr, 4);
    lv_grad_vertical_init(&g_grad);
    g_bar = plain(g_scr, 0, UIM->status_h, W(), UIM->row_h);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad(g_bar, &g_grad, 0);
    rows(g_scr, UIM->rows, UIM->status_h);
    run("grad_bar", grad_bar_step);
}

// ---- 2. alpha_panel ----

static lv_obj_t *g_panel, *g_strip;

static void alpha_step(uint32_t i, uint32_t ms) {
    (void)ms;
    lv_obj_set_pos(g_panel, (W() - 452) / 2 + (int32_t)(i & 1u), (H() - 220) / 2 + (int32_t)(i & 1u));
    lv_obj_invalidate(g_strip);
}

static void test_alpha_panel(void) {
    reset_screen();
    image_obj(g_scr, &g_bg_dsc, 0, 0, W(), H());
    g_panel = plain(g_scr, (W() - 452) / 2, (H() - 220) / 2, 452, 220);
    lv_obj_set_style_bg_color(g_panel, lv_color_hex(0x0a0a0c), 0);
    lv_obj_set_style_bg_opa(g_panel, LV_OPA_50, 0);
    lv_obj_set_style_radius(g_panel, 14, 0);
    g_strip = plain(g_scr, 0, 0, W(), UIM->status_h);
    lv_obj_set_style_bg_color(g_strip, lv_color_hex(0x0a0a0c), 0);
    lv_obj_set_style_bg_opa(g_strip, LV_OPA_50, 0);
    run("alpha_panel", alpha_step);
}

// ---- 3. backdrop_list (+ smooth) ----

static lv_obj_t *g_cont;
static uint32_t g_scroll_last;
static uint32_t g_scroll_row;

static void backdrop_list_step(uint32_t i, uint32_t ms) {
    (void)ms;
    lv_obj_scroll_to_y(g_cont, (int32_t)(i % 14u) * UIM->row_h, LV_ANIM_OFF);
}

static void backdrop_smooth_step(uint32_t i, uint32_t ms) {
    (void)i;
    if (ms - g_scroll_last >= 300u) { /* a new row every 300 ms, LVGL animates the scroll between */
        g_scroll_last = ms;
        g_scroll_row++;
        if (g_scroll_row == 14u) {
            g_scroll_row = 0;
            lv_obj_scroll_to_y(g_cont, 0, LV_ANIM_OFF);
        } else {
            lv_obj_scroll_to_y(g_cont, (int32_t)g_scroll_row * UIM->row_h, LV_ANIM_ON);
        }
    }
}

static void backdrop_list_build(void) {
    reset_screen();
    image_obj(g_scr, &g_bg_dsc, 0, 0, W(), H());
    const int32_t y0 = UIM->status_h;
    lv_obj_t *ground = plain(g_scr, 0, y0, W(), H() - y0);
    lv_obj_set_style_bg_color(ground, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ground, LV_OPA_40, 0);
    g_cont = lv_obj_create(ground); /* transparent and scrollable: the rows move inside the static ground */
    lv_obj_remove_style_all(g_cont);
    lv_obj_set_pos(g_cont, 0, 0);
    lv_obj_set_size(g_cont, W(), H() - y0);
    lv_obj_set_scrollbar_mode(g_cont, LV_SCROLLBAR_MODE_OFF);
    rows(g_cont, 21, 0);
    g_scroll_last = 0;
    g_scroll_row = 0;
}

static void test_backdrop_list(void) {
    backdrop_list_build();
    run("backdrop_list", backdrop_list_step);
}

static void test_backdrop_list_smooth(void) {
    backdrop_list_build();
    run("backdrop_list_smooth", backdrop_smooth_step);
}

// ---- 4. flow_flat ----

#define RING 7
static lv_obj_t *g_tile_obj[RING];
static int32_t g_cycle_seen;

static const int32_t k_size[4] = {220, 150, 100, 100};
static const int32_t k_rad[4] = {20, 16, 12, 12};
static const int32_t k_opa[4] = {255, 217, 153, 153}; /* 1 / .85 / .6 */
static const int32_t k_dx[4] = {0, 170, 245, 280};

static int32_t ring_off(int i, int32_t centre) { return ((i - centre + 3) % RING + RING) % RING - 3; }

static void flat_place(lv_obj_t *o, int32_t pos_fx, int32_t cy) {
    int32_t d = pos_fx < 0 ? -pos_fx : pos_fx;
    if (d >= 3 * FX) {
        lv_obj_set_hidden(o, true);
        return;
    }
    lv_obj_set_hidden(o, false);
    int32_t k = d / FX, f = d % FX;
    int32_t size = lerp(k_size[k], k_size[k + 1], f), rad = lerp(k_rad[k], k_rad[k + 1], f), opa = lerp(k_opa[k], k_opa[k + 1], f);
    int32_t dx = lerp(k_dx[k], k_dx[k + 1], f);
    lv_obj_set_size(o, size, size);
    lv_obj_set_pos(o, W() / 2 + (pos_fx < 0 ? -dx : dx) - size / 2, cy - size / 2);
    lv_obj_set_style_radius(o, rad, 0);
    lv_obj_set_style_image_opa(o, (lv_opa_t)opa, 0);
}

static void flat_stack(int32_t centre, int32_t shift_fx) {
    for (int d = 3; d >= 0; d--) {
        for (int i = 0; i < RING; i++) {
            int32_t p = ring_off(i, centre) * FX - shift_fx;
            if ((p < 0 ? -p : p) / FX == d) {
                lv_obj_move_foreground(g_tile_obj[i]);
            }
        }
    }
}

static void flat_step(uint32_t i, uint32_t ms) {
    (void)i;
    int32_t cycle = (int32_t)(ms / 250u), frac = (int32_t)(ms % 250u) * FX / 250;
    int32_t centre = cycle % RING, cy = UIM->status_h + (H() - UIM->status_h) / 2;
    for (int t = 0; t < RING; t++) {
        flat_place(g_tile_obj[t], ring_off(t, centre) * FX - frac, cy);
    }
    if (cycle != g_cycle_seen) { /* stacking order twice per step, not every frame (move_foreground invalidates) */
        g_cycle_seen = cycle;
        flat_stack(centre, 0);
    }
}

static void test_flow_flat(void) {
    reset_screen();
    lv_obj_set_style_bg_color(g_scr, lv_color_black(), 0);
    for (int t = 0; t < RING; t++) {
        g_tile_obj[t] = image_obj(g_scr, &g_tile_dsc, 0, 0, 100, 100);
    }
    g_cycle_seen = -1;
    run("flow_flat", flat_step);
}

// ---- 5. flow_angled: skew is not drawn by the software renderer (it declines the task), so the turned tiles are drawn in 6 px
// vertical slices, each with its own scale_y (a trapezoid) and the shared scale_x (the foreshortened width). Reflection = the
// pre-flipped source at 30 % under each slice. ----

static const int32_t a_size[4] = {200, 156, 120, 120};
static const int32_t a_dx[4] = {0, 150, 215, 250};
static int32_t g_apos[RING];

static void aflow_tile(lv_layer_t *layer, int32_t pos_fx, int32_t cy) {
    int32_t d = pos_fx < 0 ? -pos_fx : pos_fx;
    if (d >= 3 * FX) {
        return;
    }
    int32_t k = d / FX, f = d % FX;
    int32_t size = lerp(a_size[k], a_size[k + 1], f), dx = lerp(a_dx[k], a_dx[k + 1], f);
    int32_t turn = imin(d, FX);                          /* 0 = facing, FX = fully turned (58 degrees) */
    int32_t w = size * (FX - 120 * turn / FX) / FX;      /* x cos(58) = 0.53 when fully turned */
    int32_t cx = W() / 2 + (pos_fx < 0 ? -dx : dx);
    int32_t far_pct = 100 - 22 * turn / FX;              /* the far edge is 78 % as tall as the near one */
    const lv_area_t clip0 = layer->_clip_area;
    int32_t gap = 4;
    lv_draw_image_dsc_t dsc, rfl;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = &g_tile_dsc;
    dsc.pivot.x = dsc.pivot.y = 80;
    dsc.scale_x = w * 256 / 160;
    lv_draw_image_dsc_init(&rfl);
    rfl = dsc;
    rfl.src = &g_flip_dsc;
    rfl.opa = 77; /* 30 % */
    int32_t n = turn == 0 ? 1 : imax(1, w / 6);
    for (int32_t s = 0; s < n; s++) {
        int32_t x0 = cx - w / 2 + s * w / n, x1 = cx - w / 2 + (s + 1) * w / n - 1;
        int32_t u = n == 1 ? 0 : s * FX / (n - 1);      /* 0 at the left, FX at the right */
        int32_t edge = pos_fx < 0 ? u : FX - u;         /* 0 = far edge .. FX = near edge */
        int32_t pct = lerp(far_pct, 100, edge);
        int32_t h = size * pct / 100;
        dsc.scale_y = rfl.scale_y = h * 256 / 160;
        lv_area_t slice = {x0, cy - h / 2 - 1, x1, cy + h / 2 + 1}, cl;
        lv_area_t at = {cx - 80, cy - 80, cx + 79, cy + 79};
        if (area_isect(&cl, &slice, &clip0)) {
            layer->_clip_area = cl;
            lv_draw_image(layer, &dsc, &at);
        }
        int32_t ry = cy + h + gap;
        lv_area_t rslice = {x0, cy + h / 2 + gap, x1, cy + h / 2 + gap + h / 2}, rat = {cx - 80, ry - 80, cx + 79, ry + 79};
        if (area_isect(&cl, &rslice, &clip0)) {
            layer->_clip_area = cl;
            lv_draw_image(layer, &rfl, &rat);
        }
    }
    layer->_clip_area = clip0;
}

static void aflow_draw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    int32_t cy = UIM->status_h + 20 + a_size[0] / 2;
    for (int d = 3; d >= 0; d--) { /* far tiles first, the centre last */
        for (int t = 0; t < RING; t++) {
            int32_t p = g_apos[t] < 0 ? -g_apos[t] : g_apos[t];
            if (p / FX == d) {
                aflow_tile(layer, g_apos[t], cy);
            }
        }
    }
}

static lv_obj_t *g_ground;

static void aflow_step(uint32_t i, uint32_t ms) {
    (void)i;
    int32_t cycle = (int32_t)(ms / 250u), frac = (int32_t)(ms % 250u) * FX / 250;
    for (int t = 0; t < RING; t++) {
        g_apos[t] = ring_off(t, cycle % RING) * FX - frac;
    }
    lv_obj_invalidate(g_ground);
}

static lv_grad_dsc_t g_ground_grad;

static void test_flow_angled(void) {
    reset_screen();
    const lv_color_t c[2] = {lv_color_black(), lv_color_hex(0x1a1c20)};
    memset(&g_ground_grad, 0, sizeof g_ground_grad);
    lv_grad_init_stops(&g_ground_grad, c, NULL, NULL, 2);
    lv_grad_vertical_init(&g_ground_grad);
    g_ground = plain(g_scr, 0, UIM->status_h, W(), H() - UIM->status_h);
    lv_obj_set_style_bg_opa(g_ground, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad(g_ground, &g_ground_grad, 0);
    lv_obj_add_event_cb(g_ground, aflow_draw, LV_EVENT_DRAW_MAIN, NULL);
    run("flow_angled", aflow_step);
}

// ---- 6. ring ----

static lv_obj_t *g_ringbar;

static void ring_step(uint32_t i, uint32_t ms) {
    (void)ms;
    lv_obj_set_style_outline_opa(g_ringbar, (i & 1u) ? 0 : 153, 0); /* .6 */
}

static void test_ring(void) {
    reset_screen();
    const int32_t r = 3;
    g_ringbar = plain(g_scr, 6, UIM->status_h + r * UIM->row_h, W() - 12, UIM->row_h);
    lv_obj_set_style_bg_color(g_ringbar, lv_color_hex(0x3a86ff), 0);
    lv_obj_set_style_bg_opa(g_ringbar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_ringbar, 14, 0);
    lv_obj_set_style_outline_width(g_ringbar, 3, 0);
    lv_obj_set_style_outline_color(g_ringbar, lv_color_white(), 0);
    lv_obj_set_style_outline_pad(g_ringbar, 0, 0);
    lv_obj_set_style_outline_opa(g_ringbar, 0, 0);
    rows(g_scr, UIM->rows, UIM->status_h);
    run("ring", ring_step);
}

// ---- 7. backdrop_gen and the two ways to draw it ----

static uint32_t us_now(void) { return (uint32_t)hal_micros(); }

// Centre crop of the square cover to 4:3 (160x120), box-averaged down to tw x th.
static void bd_scale(const uint16_t *src, uint16_t *dst, int tw, int th) {
    const int sw = 160, sh = 120, y_off = 20;
    for (int y = 0; y < th; y++) {
        int sy0 = y * sh / th, sy1 = imax(sy0 + 1, (y + 1) * sh / th);
        for (int x = 0; x < tw; x++) {
            int sx0 = x * sw / tw, sx1 = imax(sx0 + 1, (x + 1) * sw / tw);
            int r = 0, g = 0, b = 0, n = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                const uint16_t *row = src + (yy + y_off) * 160;
                for (int xx = sx0; xx < sx1; xx++) {
                    uint16_t p = row[xx];
                    r += (p >> 11) & 31;
                    g += (p >> 5) & 63;
                    b += p & 31;
                    n++;
                }
            }
            dst[y * tw + x] = (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (b / n));
        }
    }
}

// Box blur of radius rad along one axis (stride 1 = horizontal, stride w = vertical), edges clamped, sliding window.
static void bd_blur_axis(const uint16_t *src, uint16_t *dst, int len, int lines, int step, int line_step, int rad) {
    const int win = 2 * rad + 1;
    for (int l = 0; l < lines; l++) {
        const uint16_t *s = src + l * line_step;
        uint16_t *d = dst + l * line_step;
        int r = 0, g = 0, b = 0;
        for (int i = -rad; i <= rad; i++) {
            uint16_t p = s[imin(imax(i, 0), len - 1) * step];
            r += (p >> 11) & 31;
            g += (p >> 5) & 63;
            b += p & 31;
        }
        for (int i = 0; i < len; i++) {
            d[i * step] = (uint16_t)(((r / win) << 11) | ((g / win) << 5) | (b / win));
            uint16_t pa = s[imin(i + rad + 1, len - 1) * step], pr = s[imax(i - rad, 0) * step];
            r += ((pa >> 11) & 31) - ((pr >> 11) & 31);
            g += ((pa >> 5) & 63) - ((pr >> 5) & 63);
            b += (pa & 31) - (pr & 31);
        }
    }
}

// 3 passes of (horizontal, vertical); result in a. b is scratch.
static void bd_blur(uint16_t *a, uint16_t *b, int tw, int th, int rad) {
    for (int pass = 0; pass < 3; pass++) {
        bd_blur_axis(a, b, tw, th, 1, tw, rad);
        bd_blur_axis(b, a, th, tw, tw, 1, rad);
    }
}

// Scrim: black at 30 % at the top to 55 % at the bottom.
static void bd_scrim(uint16_t *a, int tw, int th) {
    for (int y = 0; y < th; y++) {
        int al = 77 + (140 - 77) * y / imax(th - 1, 1), m = 255 - al;
        for (int x = 0; x < tw; x++) {
            uint16_t p = a[y * tw + x];
            int r = (((p >> 11) & 31) * m) >> 8, g = (((p >> 5) & 63) * m) >> 8, b = ((p & 31) * m) >> 8;
            a[y * tw + x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

typedef struct {
    uint16_t *a, *b; /* a holds the finished backdrop */
    bool psram;
    int tw, th;
} bd_t;

static void backdrop_gen_case(int tw, int th, bool want_psram, bd_t *keep) {
    const size_t bytes = (size_t)tw * (size_t)th * 2u;
    bool psram = want_psram;
    uint16_t *a = ui_bench_alloc(bytes, psram), *b = ui_bench_alloc(bytes, psram);
    if (a == NULL || b == NULL) {
        printf("bench: backdrop_gen size=%dx%d where=%s alloc failed (2 x %u bytes)\n", tw, th, want_psram ? "psram" : "internal", (unsigned)bytes);
        ui_bench_free(b, psram);
        ui_bench_free(a, psram);
        return;
    }
    const int runs = 8, rad = imax(1, tw / 30);
    uint32_t t_scale = 0, t_blur = 0, t_scrim = 0;
    for (int n = 0; n < runs; n++) {
        uint32_t t0 = us_now();
        bd_scale(g_tile, a, tw, th);
        uint32_t t1 = us_now();
        bd_blur(a, b, tw, th, rad);
        uint32_t t2 = us_now();
        bd_scrim(a, tw, th);
        uint32_t t3 = us_now();
        t_scale += t1 - t0;
        t_blur += t2 - t1;
        t_scrim += t3 - t2;
    }
    t_scale /= runs;
    t_blur /= runs;
    t_scrim /= runs;
    printf("bench: backdrop_gen src=synth160 size=%dx%d where=%s bytes=%u blur_radius=%d passes=3 runs=%d us_scale=%u us_blur=%u us_scrim=%u us_total=%u\n",
           tw, th, psram ? "psram" : "internal", (unsigned)(2u * bytes), rad, runs, (unsigned)t_scale, (unsigned)t_blur, (unsigned)t_scrim,
           (unsigned)(t_scale + t_blur + t_scrim));
    if (keep != NULL) {
        *keep = (bd_t){a, b, psram, tw, th};
    } else {
        ui_bench_free(b, psram);
        ui_bench_free(a, psram);
    }
}

static lv_image_dsc_t g_small_dsc;
static lv_obj_t *g_bd_img;

static void bd_draw_step(uint32_t i, uint32_t ms) {
    (void)i;
    (void)ms;
    lv_obj_invalidate(g_bd_img);
}

// Bilinear-ish (2x2 average of the nearest small pixels, fixed point) upscale of the finished small backdrop to the full screen.
static void bd_upscale(const uint16_t *s, int tw, int th, uint16_t *d) {
    const int dw = W(), dh = H();
    for (int y = 0; y < dh; y++) {
        int fy = y * 256 * (th - 1) / (dh - 1), y0 = fy >> 8, y1 = imin(y0 + 1, th - 1), wy = fy & 255;
        for (int x = 0; x < dw; x++) {
            int fx = x * 256 * (tw - 1) / (dw - 1), x0 = fx >> 8, x1 = imin(x0 + 1, tw - 1), wx = fx & 255;
            uint16_t p00 = s[y0 * tw + x0], p01 = s[y0 * tw + x1], p10 = s[y1 * tw + x0], p11 = s[y1 * tw + x1];
            int w00 = (256 - wx) * (256 - wy), w01 = wx * (256 - wy), w10 = (256 - wx) * wy, w11 = wx * wy;
            int r = (((p00 >> 11) & 31) * w00 + ((p01 >> 11) & 31) * w01 + ((p10 >> 11) & 31) * w10 + ((p11 >> 11) & 31) * w11) >> 16;
            int g = (((p00 >> 5) & 63) * w00 + ((p01 >> 5) & 63) * w01 + ((p10 >> 5) & 63) * w10 + ((p11 >> 5) & 63) * w11) >> 16;
            int b = ((p00 & 31) * w00 + (p01 & 31) * w01 + (p10 & 31) * w10 + (p11 & 31) * w11) >> 16;
            d[y * dw + x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

static void test_backdrop_draw(int tw, int th, bool psram, bd_t *bd) {
    char name[64];
    reset_screen();
    dsc_init(&g_small_dsc, bd->a, tw, th);
    g_bd_img = image_obj(g_scr, &g_small_dsc, 0, 0, W(), H()); /* STRETCH: LVGL scales it to the screen every time it draws */
    snprintf(name, sizeof name, "backdrop_draw_scaled_%dx%d@%s", tw, th, psram ? "psram" : "int");
    run(name, bd_draw_step);

    size_t full_bytes = (size_t)W() * (size_t)H() * 2u;
    uint16_t *full = ui_bench_alloc(full_bytes, true);
    if (full == NULL) {
        printf("bench: backdrop_draw_full alloc failed (%u bytes)\n", (unsigned)full_bytes);
        return;
    }
    uint32_t t0 = us_now();
    bd_upscale(bd->a, tw, th, full);
    printf("bench: backdrop_upscale from=%dx%d to=%dx%d us=%u bytes=%u where=psram\n", tw, th, (int)W(), (int)H(), (unsigned)(us_now() - t0),
           (unsigned)full_bytes);
    reset_screen();
    lv_image_dsc_t fd;
    dsc_init(&fd, full, W(), H());
    g_bd_img = image_obj(g_scr, &fd, 0, 0, W(), H());
    snprintf(name, sizeof name, "backdrop_draw_full_from_%dx%d", tw, th);
    run(name, bd_draw_step);
    reset_screen(); /* drop the image object before the buffer goes */
    ui_bench_free(full, true);
}

// ---- main ----

static void round_run(void) {
    printf("bench: start profile=%dx%d row_h=%d rows=%d bench_ms=%u\n", (int)W(), (int)H(), (int)UIM->row_h, (int)UIM->rows, (unsigned)UI_BENCH_MS);
    size_t bg_bytes = (size_t)W() * (size_t)H() * 2u, tile_bytes = 160u * 160u * 2u;
    g_bg = ui_bench_alloc(bg_bytes, true);
    g_tile = ui_bench_alloc(tile_bytes, true);
    g_tile_flip = ui_bench_alloc(tile_bytes, true);
    if (g_bg == NULL || g_tile == NULL || g_tile_flip == NULL) {
        printf("bench: ERROR no memory for the picture buffers (psram): bg=%p tile=%p flip=%p\n", (void *)g_bg, (void *)g_tile, (void *)g_tile_flip);
        goto out;
    }
    printf("bench: mem psram bg=%u tile=%u tile_flip=%u (all synthetic pictures, no JPEG decode)\n", (unsigned)bg_bytes, (unsigned)tile_bytes,
           (unsigned)tile_bytes);
    synth(g_bg, (int)W(), (int)H());
    synth(g_tile, 160, 160);
    for (int y = 0; y < 160; y++) {
        memcpy(g_tile_flip + y * 160, g_tile + (159 - y) * 160, 160u * 2u);
    }
    dsc_init(&g_bg_dsc, g_bg, (int)W(), (int)H());
    dsc_init(&g_tile_dsc, g_tile, 160, 160);
    dsc_init(&g_flip_dsc, g_tile_flip, 160, 160);

    test_grad_bar();
    test_alpha_panel();
    test_backdrop_list();
    test_backdrop_list_smooth();
    test_flow_flat();
    test_flow_angled();
    test_ring();

    static const int sizes[2][2] = {{75, 56}, {150, 112}};
    for (int s = 0; s < 2; s++) {
        int tw = sizes[s][0], th = sizes[s][1];
        backdrop_gen_case(tw, th, true, NULL);
        bd_t keep;
        memset(&keep, 0, sizeof keep);
        /* the frame tests use the internal-RAM copy when it fits, else the PSRAM one */
        backdrop_gen_case(tw, th, false, &keep);
        if (keep.a == NULL) {
            backdrop_gen_case(tw, th, true, &keep);
        }
        if (keep.a != NULL) {
            test_backdrop_draw(tw, th, keep.psram, &keep);
            ui_bench_free(keep.b, keep.psram);
            ui_bench_free(keep.a, keep.psram);
        }
    }
    reset_screen();
out:
    ui_bench_free(g_tile_flip, true);
    ui_bench_free(g_tile, true);
    ui_bench_free(g_bg, true);
    g_bg = g_tile = g_tile_flip = NULL;
    printf("bench: done\n");
}

void ui_bench_main(void) {
    g_scr = lv_obj_create(NULL);
    reset_screen();
    lv_screen_load(g_scr);
    lv_display_t *d = lv_display_get_default();
    lv_display_add_event_cb(d, on_render, LV_EVENT_RENDER_READY, NULL);
    lv_timer_set_period(lv_display_get_refr_timer(d), 1); /* render on every pass that has something dirty, not every 33 ms */
    for (;;) {
        round_run();
        os_delay_ms(2000);
    }
}

#endif /* NEUPOD_UI_BENCH */
