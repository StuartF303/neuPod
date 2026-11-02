// SPDX-License-Identifier: MPL-2.0
/*
 * Display bring-up test runner (see docs/display-tests.md).
 *
 * Each test takes a context; in "auto" mode (menu key 'a') it shortens itself to about 3 s.
 * Tests poll the console between steps and stop on 'q'. Scenes are drawn with the portable gfx
 * layer through the strip renderer (no framebuffer); solid fills use hal_display_fill_rect.
 */
#include "display_tests.h"

#include <stdio.h>

#include "assets/test_image.h"
#include "gfx.h"
#include "gfx_strip.h"
#include "hal_console.h"
#include "hal_display.h"
#include "hal_system.h"
#include "now_playing.h"

typedef enum { TEST_DONE, TEST_QUIT } test_result_t;
typedef enum { WAIT_DONE, WAIT_QUIT, WAIT_REDRAW } wait_result_t;

typedef struct {
    bool autorun; // true when run via 'a': cap the test at about 3 s
} test_ctx_t;

#define POLL_MS 20u
#define MADCTL_ORIENT_MASK 0xE0u // MY | MX | MV
#define MADCTL_BGR 0x08u

#define COL_BLACK HAL_RGB565(0, 0, 0)
#define COL_WHITE HAL_RGB565(255, 255, 255)
#define COL_RED HAL_RGB565(255, 0, 0)
#define COL_GREEN HAL_RGB565(0, 255, 0)
#define COL_BLUE HAL_RGB565(0, 0, 255)
#define COL_YELLOW HAL_RGB565(255, 255, 0)
#define COL_CYAN HAL_RGB565(0, 255, 255)
#define COL_MAGENTA HAL_RGB565(255, 0, 255)

#define PANEL_W ((int)hal_display_width())
#define PANEL_H ((int)hal_display_height())

static unsigned g_hal_errors; // display calls that failed during the current test

// --- bring-up helpers ----------------------------------------------------------------------

static void print_state(void) {
    uint8_t m = hal_display_get_madctl();
    printf("state: MADCTL=0x%02X (MY=%d MX=%d MV=%d, %s order) inversion=%s\n", m,
           (m & 0x80) != 0, (m & 0x40) != 0, (m & 0x20) != 0, (m & MADCTL_BGR) ? "BGR" : "RGB",
           hal_display_get_inversion() ? "on" : "off");
}

// Black screen with red top-left, green top-right, blue bottom-left, white bottom-right squares
// and a white outline: shows orientation (mirror/flip) and colour order at a glance.
static void draw_corner_pattern(void) {
    uint16_t w = hal_display_width();
    uint16_t h = hal_display_height();
    uint16_t s = 48;
    hal_display_fill_rect(0, 0, w, h, COL_BLACK);
    hal_display_fill_rect(0, 0, s, s, COL_RED);
    hal_display_fill_rect((uint16_t)(w - s), 0, s, s, COL_GREEN);
    hal_display_fill_rect(0, (uint16_t)(h - s), s, s, COL_BLUE);
    hal_display_fill_rect((uint16_t)(w - s), (uint16_t)(h - s), s, s, COL_WHITE);
    hal_display_fill_rect(s, 0, (uint16_t)(w - 2 * s), 2, COL_WHITE);
    hal_display_fill_rect(s, (uint16_t)(h - 2), (uint16_t)(w - 2 * s), 2, COL_WHITE);
    hal_display_present();
}

// Handle a bring-up key. Returns true if the key was one (and the display state changed).
static bool bringup_key(int c) {
    static const uint8_t orient[] = {0x60, 0xA0, 0xE0, 0x20}; // landscape variants (MV set)
    uint8_t m = hal_display_get_madctl();
    switch (c) {
    case 'b':
        hal_display_set_madctl((uint8_t)(m ^ MADCTL_BGR));
        break;
    case 'i':
        hal_display_set_inversion(!hal_display_get_inversion());
        break;
    case 'r': {
        size_t cur = 0;
        for (size_t k = 0; k < sizeof orient; k++) {
            if (orient[k] == (m & MADCTL_ORIENT_MASK)) {
                cur = k;
            }
        }
        uint8_t next = orient[(cur + 1) % sizeof orient];
        hal_display_set_madctl((uint8_t)((m & ~MADCTL_ORIENT_MASK) | next));
        break;
    }
    case 'm':
        print_state();
        return false; // nothing changed on screen
    case 'p':
        break;
    default:
        return false;
    }
    print_state();
    draw_corner_pattern();
    printf("  (corner pattern: red top-left, green top-right, blue bottom-left, white "
           "bottom-right)\n");
    return true;
}

// Wait up to @p ms, polling the console. 'q' quits; a bring-up key applies and asks the caller
// to redraw its current step.
static wait_result_t wait_step(uint32_t ms) {
    uint32_t start = hal_millis();
    for (;;) {
        uint32_t elapsed = hal_millis() - start;
        if (elapsed >= ms) {
            return WAIT_DONE;
        }
        uint32_t left = ms - elapsed;
        int c = hal_console_getc_timeout(left < POLL_MS ? left : POLL_MS);
        if (c == 'q' || c == 'Q') {
            return WAIT_QUIT;
        }
        if (c >= 0 && bringup_key(c)) {
            return WAIT_REDRAW;
        }
    }
}

static uint32_t step_ms(const test_ctx_t *ctx, uint32_t normal, uint32_t quick) {
    return ctx->autorun ? quick : normal;
}

// Non-blocking 'q' check for loops that do not wait_step(). Bring-up keys are applied too.
static bool quit_requested(void) {
    int c = hal_console_getc_timeout(0);
    if (c == 'q' || c == 'Q') {
        return true;
    }
    if (c >= 0) {
        (void)bringup_key(c);
    }
    return false;
}

static void begin_test(const char *title) {
    g_hal_errors = 0;
    printf("\n== %s ==\n", title);
}

// End of a test: FAIL if any display call errored, otherwise the operator's visual check.
static void end_test(const char *visual_check) {
    if (g_hal_errors != 0) {
        printf("FAIL: %u display call(s) returned an error\n", g_hal_errors);
    }
    printf("VISUAL CHECK: %s\n", visual_check);
}

static void note(hal_status_t st) {
    if (st != HAL_OK) {
        g_hal_errors++;
    }
}

// Render a scene through the strip renderer and hold it for the step time (redrawing if a
// bring-up key changed the panel settings meanwhile).
static wait_result_t show_scene(const test_ctx_t *ctx, gfx_draw_fn fn, void *sctx,
                                uint32_t normal_ms, uint32_t quick_ms) {
    wait_result_t r;
    do {
        note(gfx_present_frame(fn, sctx));
        r = wait_step(step_ms(ctx, normal_ms, quick_ms));
    } while (r == WAIT_REDRAW);
    return r;
}

// --- test 1: backlight ---------------------------------------------------------------------

static test_result_t test_backlight(const test_ctx_t *ctx) {
    begin_test("Test 1: backlight");
    hal_display_fill_rect(0, 0, hal_display_width(), hal_display_height(), COL_WHITE);
    hal_display_present();

    // Ramp 0 -> 255 -> 0 (4 s total, 1.5 s in auto mode), then hold 10 / 50 / 100 %.
    uint32_t total = step_ms(ctx, 4000, 1500);
    printf("ramp 0 -> 255 -> 0 over %u ms\n", (unsigned)total);
    uint32_t start = hal_millis();
    for (;;) {
        uint32_t t = hal_millis() - start;
        if (t >= total) {
            break;
        }
        uint32_t half = total / 2;
        uint32_t phase = t < half ? t : total - t; // triangle wave
        uint32_t level = phase * 255u / half;
        hal_display_backlight((uint8_t)(level > 255u ? 255u : level));
        int c = hal_console_getc_timeout(POLL_MS);
        if (c == 'q' || c == 'Q') {
            hal_display_backlight(255);
            return TEST_QUIT;
        }
    }
    static const struct {
        unsigned pct;
        uint8_t level;
    } holds[] = {{10, 26}, {50, 128}, {100, 255}};
    for (size_t k = 0; k < sizeof holds / sizeof holds[0]; k++) {
        hal_display_backlight(holds[k].level);
        printf("hold %u %% (level %u)\n", holds[k].pct, (unsigned)holds[k].level);
        if (wait_step(step_ms(ctx, 2000, 500)) == WAIT_QUIT) {
            hal_display_backlight(255);
            return TEST_QUIT;
        }
    }
    hal_display_backlight(255);
    end_test("white screen fades smoothly dark -> full -> dark with no flicker or visible steps, "
             "then three clearly brighter holds (10%, 50%, 100%)");
    return TEST_DONE;
}

// --- test 2: colours -----------------------------------------------------------------------

static void scene_bars(gfx_canvas_t *c, void *sctx) {
    (void)sctx;
    static const hal_pixel_t bars[8] = {COL_WHITE, COL_YELLOW, COL_CYAN,  COL_GREEN,
                                        COL_MAGENTA, COL_RED,  COL_BLUE, COL_BLACK};
    for (int i = 0; i < 8; i++) {
        int x0 = i * PANEL_W / 8;
        int x1 = (i + 1) * PANEL_W / 8;
        gfx_fill_rect(c, x0, 0, x1 - x0, PANEL_H, bars[i]);
    }
}

typedef enum { GRAD_R, GRAD_G, GRAD_B, GRAD_GREY } grad_t;

// 256 steps across the width (x * 256 / width).
static void scene_gradient(gfx_canvas_t *c, void *sctx) {
    grad_t g = *(const grad_t *)sctx;
    for (int x = 0; x < PANEL_W; x++) {
        uint8_t v = (uint8_t)(x * 256 / PANEL_W);
        hal_pixel_t col = g == GRAD_R   ? HAL_RGB565(v, 0, 0)
                          : g == GRAD_G ? HAL_RGB565(0, v, 0)
                          : g == GRAD_B ? HAL_RGB565(0, 0, v)
                                        : HAL_RGB565(v, v, v);
        gfx_vline(c, x, c->y0, c->h, col);
    }
}

static test_result_t test_colours(const test_ctx_t *ctx) {
    static const struct {
        const char *name;
        hal_pixel_t colour;
        const char *wrong;
    } cols[] = {
        {"RED", COL_RED, "blue = BGR order wrong (key b); cyan = inverted (key i)"},
        {"GREEN", COL_GREEN, "magenta = inverted (key i); green stays green under BGR"},
        {"BLUE", COL_BLUE, "red = BGR order wrong (key b); yellow = inverted (key i)"},
        {"WHITE", COL_WHITE, "black = inverted (key i)"},
        {"BLACK", COL_BLACK, "white = inverted (key i)"},
    };
    begin_test("Test 2: colours (part 1: solid fills)");
    for (size_t k = 0; k < sizeof cols / sizeof cols[0]; k++) {
        printf("%s - if it looks like: %s\n", cols[k].name, cols[k].wrong);
        wait_result_t r;
        do {
            note(hal_display_fill_rect(0, 0, hal_display_width(), hal_display_height(),
                                       cols[k].colour));
            hal_display_present();
            r = wait_step(step_ms(ctx, 1000, 300));
        } while (r == WAIT_REDRAW);
        if (r == WAIT_QUIT) {
            return TEST_QUIT;
        }
    }
    end_test("red, green, blue, white, black in turn, each uniform with no stuck/dead pixels, "
             "banding or tint");

    printf("\n-- Test 2b: colour bars --\n");
    if (show_scene(ctx, scene_bars, NULL, 2000, 300) == WAIT_QUIT) {
        return TEST_QUIT;
    }
    end_test("8 vertical bars left to right: white, yellow, cyan, green, magenta, red, blue, "
             "black; sharp edges, saturated colours");

    printf("\n-- Test 2b: 256-step gradients (R, G, B, grey) --\n");
    static const char *const names[] = {"red", "green", "blue", "grey"};
    for (grad_t g = GRAD_R; g <= GRAD_GREY; g++) {
        printf("gradient: %s\n", names[g]);
        if (show_scene(ctx, scene_gradient, &g, 2000, 250) == WAIT_QUIT) {
            return TEST_QUIT;
        }
    }
    end_test("smooth black-to-colour ramps left to right. RGB565 has only 32 (R,B) / 64 (G) "
             "levels, so steps of 8 / 4 input levels are EXPECTED (grey bands are tinted green/"
             "magenta slightly); look for uneven or missing bands, not for perfect smoothness");
    return TEST_DONE;
}

// --- test 3: sharpness ---------------------------------------------------------------------

typedef enum { SH_CHECKER, SH_VGRATE, SH_HGRATE, SH_BORDER, SH_CROSS } sharp_t;

static void scene_sharp(gfx_canvas_t *c, void *sctx) {
    sharp_t k = *(const sharp_t *)sctx;
    int w = PANEL_W;
    int h = PANEL_H;
    if (k == SH_CHECKER || k == SH_VGRATE || k == SH_HGRATE) {
        for (int y = c->y0; y < c->y0 + (int)c->h; y++) {
            for (int x = 0; x < w; x++) {
                int on = k == SH_CHECKER ? ((x + y) & 1) : k == SH_VGRATE ? (x & 1) : (y & 1);
                gfx_pixel(c, x, y, on ? COL_WHITE : COL_BLACK);
            }
        }
        return;
    }
    gfx_clear(c, COL_BLACK);
    if (k == SH_BORDER) {
        gfx_rect(c, 0, 0, w, h, COL_WHITE);
        gfx_pixel(c, 0, 0, COL_RED); // corner marks: which corner is which
        gfx_pixel(c, w - 1, 0, COL_GREEN);
        gfx_pixel(c, 0, h - 1, COL_BLUE);
    } else {
        gfx_hline(c, 0, h / 2, w, COL_WHITE);
        gfx_vline(c, w / 2, 0, h, COL_WHITE);
        gfx_circle(c, w / 2, h / 2, 40, COL_WHITE);
        gfx_circle(c, w / 2, h / 2, 80, COL_WHITE);
        for (int i = 0; i < w; i += 20) {
            gfx_vline(c, i, h / 2 - 3, 7, COL_WHITE);
        }
        for (int i = 0; i < h; i += 20) {
            gfx_hline(c, w / 2 - 3, i, 7, COL_WHITE);
        }
    }
}

static test_result_t test_sharpness(const test_ctx_t *ctx) {
    static const char *const names[] = {"1-px checkerboard", "1-px vertical grating",
                                        "1-px horizontal grating", "1-px border at panel edges",
                                        "crosshair + circles at centre"};
    begin_test("Test 3: sharpness");
    for (sharp_t k = SH_CHECKER; k <= SH_CROSS; k++) {
        printf("%s\n", names[k]);
        if (show_scene(ctx, scene_sharp, &k, 1500, 600) == WAIT_QUIT) {
            return TEST_QUIT;
        }
    }
    end_test("crisp single-pixel black/white patterns with no smearing, noise, random pixels or "
             "grey haze (those mean SPI too fast); border: all four edges visible, none missing "
             "or wrapped to the opposite side (else column/row offset); crosshair centred");
    return TEST_DONE;
}

// --- test 4: geometry ----------------------------------------------------------------------

#define MAX_SHAPES 64u
typedef struct {
    uint8_t kind; // 0 line, 1 rect, 2 filled rect, 3 circle, 4 filled circle
    int16_t a, b, c, d;
    hal_pixel_t colour;
} shape_t;
typedef struct {
    shape_t s[MAX_SHAPES];
    unsigned n;
} shapes_t;

static uint32_t rng_next(uint32_t *st) { // xorshift32
    uint32_t x = *st;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *st = x;
}

static void scene_shapes(gfx_canvas_t *c, void *sctx) {
    const shapes_t *sh = sctx;
    gfx_clear(c, COL_BLACK);
    for (unsigned i = 0; i < sh->n; i++) {
        const shape_t *s = &sh->s[i];
        switch (s->kind) {
        case 0:
            gfx_line(c, s->a, s->b, s->c, s->d, s->colour);
            break;
        case 1:
            gfx_rect(c, s->a, s->b, s->c, s->d, s->colour);
            break;
        case 2:
            gfx_fill_rect(c, s->a, s->b, s->c, s->d, s->colour);
            break;
        case 3:
            gfx_circle(c, s->a, s->b, s->c, s->colour);
            break;
        default:
            gfx_fill_circle(c, s->a, s->b, s->c, s->colour);
            break;
        }
    }
}

static void scene_grid(gfx_canvas_t *c, void *sctx) {
    (void)sctx;
    gfx_clear(c, COL_BLACK);
    for (int x = 0; x < PANEL_W; x += 10) {
        gfx_vline(c, x, 0, PANEL_H, x % 50 == 0 ? COL_WHITE : HAL_RGB565(110, 110, 110));
    }
    for (int y = 0; y < PANEL_H; y += 10) {
        gfx_hline(c, 0, y, PANEL_W, y % 50 == 0 ? COL_WHITE : HAL_RGB565(110, 110, 110));
    }
    gfx_vline(c, PANEL_W - 1, 0, PANEL_H, COL_WHITE); // close the last column / row
    gfx_hline(c, 0, PANEL_H - 1, PANEL_W, COL_WHITE);
}

static test_result_t test_geometry(const test_ctx_t *ctx) {
    static shapes_t sh; // 64 shapes, ~1 KB
    uint32_t rng = 0x2545F491u; // fixed seed: same picture every run and on the host
    begin_test("Test 4: geometry");
    for (unsigned i = 0; i < MAX_SHAPES; i++) {
        shape_t *s = &sh.s[i];
        s->kind = (uint8_t)(rng_next(&rng) % 5u);
        uint16_t col16 = (uint16_t)rng_next(&rng);
        s->colour = col16 | 0x0861u; // avoid near-black
        s->a = (int16_t)(rng_next(&rng) % 320u);
        s->b = (int16_t)(rng_next(&rng) % 240u);
        if (s->kind >= 3) {
            s->c = (int16_t)(4u + rng_next(&rng) % 50u);
        } else {
            s->c = (int16_t)(rng_next(&rng) % 320u);
            s->d = (int16_t)(rng_next(&rng) % 240u);
            if (s->kind != 0) { // rects: c,d are width/height
                s->c = (int16_t)(10u + (uint16_t)s->c % 120u);
                s->d = (int16_t)(10u + (uint16_t)s->d % 90u);
            }
        }
    }
    uint32_t t0 = hal_millis();
    uint32_t limit = step_ms(ctx, 2000, 1500);
    unsigned frames = 0;
    sh.n = 0;
    while (hal_millis() - t0 < limit) {
        if (sh.n < MAX_SHAPES) {
            sh.n++;
        }
        note(gfx_present_frame(scene_shapes, &sh));
        frames++;
        if (quit_requested()) {
            return TEST_QUIT;
        }
    }
    printf("random shapes: %u shapes, %u strip-rendered frames in %u ms\n", sh.n, frames,
           (unsigned)(hal_millis() - t0));
    end_test("random lines, rectangles, outlined and filled circles accumulate with clean edges "
             "(circles round, not oval; diagonal lines unbroken)");
    if (wait_step(step_ms(ctx, 1000, 300)) == WAIT_QUIT) {
        return TEST_QUIT;
    }

    printf("grid of 10 px squares\n");
    if (show_scene(ctx, scene_grid, NULL, 2500, 900) == WAIT_QUIT) {
        return TEST_QUIT;
    }
    end_test("32 x 24 grid of exactly SQUARE cells (not wider than tall), lines straight and "
             "evenly spaced, brighter line every 50 px, last column and row visible");
    return TEST_DONE;
}

// --- test 5: image -------------------------------------------------------------------------

static void scene_image(gfx_canvas_t *c, void *sctx) {
    (void)sctx;
    gfx_image(c, 0, 0, test_img, TEST_IMG_WIDTH, TEST_IMG_HEIGHT);
}

static test_result_t test_image(const test_ctx_t *ctx) {
    begin_test("Test 5: image");
    if (TEST_IMG_WIDTH != PANEL_W || TEST_IMG_HEIGHT != PANEL_H) {
        printf("FAIL: image is %dx%d, panel is %dx%d\n", TEST_IMG_WIDTH, TEST_IMG_HEIGHT,
               PANEL_W, PANEL_H);
        end_test("skipped: test image size does not match the panel");
        return TEST_DONE;
    }
    wait_result_t r;
    do {
        uint32_t t0 = hal_millis();
        note(hal_display_set_window(0, 0, hal_display_width(), hal_display_height()));
        note(hal_display_blit(test_img, (size_t)TEST_IMG_WIDTH * TEST_IMG_HEIGHT));
        hal_display_wait();
        printf("full-screen blit straight from flash: %u ms\n", (unsigned)(hal_millis() - t0));
        hal_display_present();
        r = wait_step(step_ms(ctx, 3000, 1000));
    } while (r == WAIT_REDRAW);
    if (r == WAIT_QUIT) {
        return TEST_QUIT;
    }
    do {
        uint32_t t0 = hal_millis();
        note(gfx_present_frame(scene_image, NULL));
        printf("same image via strip renderer (gfx_image): %u ms\n",
               (unsigned)(hal_millis() - t0));
        r = wait_step(step_ms(ctx, 3000, 1000));
    } while (r == WAIT_REDRAW);
    if (r == WAIT_QUIT) {
        return TEST_QUIT;
    }
    end_test("both passes look identical: sky/ground gradient with vignette, colour wheel on the "
             "left, white arrow pointing RIGHT on the right, red/green/blue patches bottom "
             "right, yellow mark top-left, magenta mark top-right (mirrored or rotated = wrong "
             "MADCTL, red/blue swapped = BGR, negative look = inversion)");
    return TEST_DONE;
}

// --- test 6: text --------------------------------------------------------------------------

static void draw_wrapped(gfx_canvas_t *c, const gfx_font_t *f, int x0, int y0, int right) {
    char ch[2] = {0, 0};
    int x = x0;
    int y = y0;
    for (int u = f->first; u <= f->last; u++) {
        ch[0] = (char)u;
        int adv = gfx_text_width(f, ch);
        if (x + adv > right) {
            x = x0;
            y += f->height + 2;
        }
        gfx_text(c, f, x, y, ch, COL_WHITE);
        x += adv;
    }
}

static void scene_font_sheet(gfx_canvas_t *c, void *sctx) {
    (void)sctx;
    gfx_clear(c, COL_BLACK);
    gfx_text(c, &gfx_font_8x8, 4, 4, "8x8 fixed font:", COL_YELLOW);
    draw_wrapped(c, &gfx_font_8x8, 4, 16, 316);
    gfx_text(c, &gfx_font_16, 4, 70, "Proportional 15 px font:", COL_YELLOW);
    draw_wrapped(c, &gfx_font_16, 4, 90, 316);
    gfx_text(c, &gfx_font_16, 4, 160, "The quick brown fox jumps over", COL_CYAN);
    gfx_text(c, &gfx_font_16, 4, 178, "the lazy dog. 0123456789", COL_CYAN);
    gfx_text(c, &gfx_font_8x8, 4, 206, "Quick brown fox jumps over lazy dog", COL_GREEN);
    gfx_text_bg(c, &gfx_font_8x8, 4, 222, " opaque bg ", COL_BLACK, COL_WHITE);
    gfx_text_bg(c, &gfx_font_16, 120, 220, " opaque bg ", COL_BLACK, COL_WHITE);
}

static test_result_t test_text(const test_ctx_t *ctx) {
    begin_test("Test 6: text");
    printf("font sheet\n");
    if (show_scene(ctx, scene_font_sheet, NULL, 3000, 800) == WAIT_QUIT) {
        return TEST_QUIT;
    }
    static const struct {
        const char *label;
        const gfx_font_t *font;
        hal_pixel_t fg, bg;
    } looks[] = {
        {"8x8, white on black", &gfx_font_8x8, COL_WHITE, COL_BLACK},
        {"15 px, white on black", &gfx_font_16, COL_WHITE, COL_BLACK},
        {"8x8, black on white", &gfx_font_8x8, COL_BLACK, COL_WHITE},
        {"15 px, black on white", &gfx_font_16, COL_BLACK, COL_WHITE},
    };
    for (size_t k = 0; k < sizeof looks / sizeof looks[0]; k++) {
        now_playing_t np = {.title = "Bohemian Rhapsody",
                            .artist = "Queen",
                            .album = "A Night at the Opera",
                            .elapsed_s = 83,
                            .total_s = 355,
                            .font = looks[k].font,
                            .fg = looks[k].fg,
                            .bg = looks[k].bg};
        printf("Now Playing: %s\n", looks[k].label);
        if (show_scene(ctx, now_playing_draw, &np, 2500, 500) == WAIT_QUIT) {
            return TEST_QUIT;
        }
    }
    end_test("all characters legible with consistent spacing, no clipped or missing pixels in "
             "glyphs; Now Playing screen: header with battery icon, square album-art box with "
             "rings on the left, title/artist/album at right, progress bar about 23% full with "
             "1:23 elapsed and -4:32 remaining; black-on-white reads as well as white-on-black");
    return TEST_DONE;
}

// --- test 7: animation ---------------------------------------------------------------------

#define TRAIL 10
typedef struct {
    int x[TRAIL], y[TRAIL]; // sampled history, [0] = oldest
    int hx, hy;             // current head position
} trail_t;

static void scene_ball(gfx_canvas_t *c, void *sctx) {
    const trail_t *t = sctx;
    gfx_clear(c, COL_BLACK);
    for (int i = 0; i < TRAIL; i++) {
        uint8_t v = (uint8_t)(40 + (215 * (i + 1)) / (TRAIL + 1));
        gfx_fill_circle(c, t->x[i], t->y[i], 14, HAL_RGB565(0, v, v));
    }
    gfx_fill_circle(c, t->hx, t->hy, 14, COL_WHITE);
}

static test_result_t test_animation(const test_ctx_t *ctx) {
    begin_test("Test 7: animation (fps + SPI MB/s)");
    const int r = 14;
    int x = 60, y = 50, dx = 5, dy = 3;
    trail_t t;
    for (int i = 0; i < TRAIL; i++) {
        t.x[i] = x;
        t.y[i] = y;
    }
    t.hx = x;
    t.hy = y;
    uint32_t duration = step_ms(ctx, 10000, 3000);
    uint32_t t0 = hal_millis();
    uint32_t elapsed = 0;
    unsigned frames = 0;
    while ((elapsed = hal_millis() - t0) < duration) {
        x += dx;
        y += dy;
        if (x < r || x > PANEL_W - 1 - r) {
            dx = -dx;
            x += 2 * dx;
        }
        if (y < r || y > PANEL_H - 1 - r) {
            dy = -dy;
            y += 2 * dy;
        }
        if (frames % 3u == 0) { // sample the trail every 3rd frame so the ghosts are spaced out
            for (int i = 0; i < TRAIL - 1; i++) {
                t.x[i] = t.x[i + 1];
                t.y[i] = t.y[i + 1];
            }
            t.x[TRAIL - 1] = x;
            t.y[TRAIL - 1] = y;
        }
        t.hx = x;
        t.hy = y;
        note(gfx_present_frame(scene_ball, &t));
        frames++;
        if (quit_requested()) {
            return TEST_QUIT;
        }
    }
    elapsed = hal_millis() - t0;
    if (elapsed == 0) {
        elapsed = 1;
    }
    uint64_t bytes = (uint64_t)frames * (uint64_t)PANEL_W * (uint64_t)PANEL_H * 2u;
    uint32_t fps100 = (uint32_t)((uint64_t)frames * 100000u / elapsed);
    uint32_t mbs100 = (uint32_t)(bytes * 100u / ((uint64_t)elapsed * 1000u)); // MB = 1e6 B
    printf("frames: %u in %u ms -> %u.%02u fps\n", frames, (unsigned)elapsed,
           (unsigned)(fps100 / 100), (unsigned)(fps100 % 100));
    printf("SPI throughput: %llu bytes -> %u.%02u MB/s (ceiling at 62.5 MHz: 7.81 MB/s, "
           "= 50.8 fps full frames; %u%% of ceiling)\n",
           (unsigned long long)bytes, (unsigned)(mbs100 / 100), (unsigned)(mbs100 % 100),
           (unsigned)(mbs100 * 100u / 781u));
    end_test("a white ball with a fading cyan trail bounces smoothly off all four edges with no "
             "tearing, flicker or stray lines; fps/MB/s above are the real numbers (host "
             "figures are meaningless)");
    return TEST_DONE;
}

// --- test 8: sleep -------------------------------------------------------------------------

static test_result_t test_sleep(const test_ctx_t *ctx) {
    begin_test("Test 8: sleep");
    note(hal_display_set_window(0, 0, hal_display_width(), hal_display_height()));
    note(hal_display_blit(test_img, (size_t)TEST_IMG_WIDTH * TEST_IMG_HEIGHT));
    hal_display_wait();
    hal_display_present();
    if (wait_step(step_ms(ctx, 1500, 500)) == WAIT_QUIT) {
        return TEST_QUIT;
    }
    printf("entering sleep (DISPOFF + SLPIN); backlight left on\n");
    hal_display_sleep(true);
    wait_result_t r = wait_step(step_ms(ctx, 2000, 1000));
    printf("waking (SLPOUT + DISPON); image NOT redrawn\n");
    hal_display_sleep(false);
    if (r == WAIT_QUIT) {
        return TEST_QUIT;
    }
    if (wait_step(step_ms(ctx, 3000, 1000)) == WAIT_QUIT) {
        return TEST_QUIT;
    }
    end_test("the test image goes blank during sleep (backlight still on), then returns intact "
             "without being redrawn (the ST7789 keeps its frame memory in sleep); garbage or "
             "white/black screen after waking = bad sleep/wake timing");
    return TEST_DONE;
}

// --- menu ----------------------------------------------------------------------------------

static const struct {
    const char *name;
    test_result_t (*run)(const test_ctx_t *);
} k_tests[] = {
    {"backlight", test_backlight},   {"colours (solid, bars, gradients)", test_colours},
    {"sharpness", test_sharpness},   {"geometry", test_geometry},
    {"image", test_image},           {"text + Now Playing", test_text},
    {"animation (fps, MB/s)", test_animation}, {"sleep / wake", test_sleep},
};
#define NUM_TESTS (sizeof k_tests / sizeof k_tests[0])

static void print_menu(void) {
    printf("\n=== neuPod display tests ===\n");
    for (size_t k = 0; k < NUM_TESTS; k++) {
        printf("  %u  %s\n", (unsigned)(k + 1), k_tests[k].name);
    }
    printf("  a  run all (~3 s each)       q  stop test / back to menu\n"
           "  b  toggle BGR order          i  toggle inversion\n"
           "  r  cycle rotation            m  show MADCTL / inversion\n"
           "  p  corner pattern            h  or Enter: this menu\n"
           "key: ");
    fflush(stdout);
}

void display_tests_run_menu(void) {
    print_state();
    print_menu();
    for (;;) {
        int c = hal_console_getc_timeout(100);
        if (c < 0) {
            continue;
        }
        if (c >= '1' && c < '1' + (int)NUM_TESTS) {
            printf("%c\n", c);
            test_ctx_t ctx = {.autorun = false};
            if (k_tests[c - '1'].run(&ctx) == TEST_QUIT) {
                printf("(stopped)\n");
            }
            print_menu();
        } else if (c == 'a' || c == 'A') {
            printf("a\n");
            test_ctx_t ctx = {.autorun = true};
            for (size_t k = 0; k < NUM_TESTS; k++) {
                if (k_tests[k].run(&ctx) == TEST_QUIT) {
                    printf("(stopped)\n");
                    break;
                }
            }
            print_menu();
        } else if (c == 'h' || c == '?' || c == '\r' || c == '\n') {
            print_menu();
        } else if (bringup_key(c)) {
            printf("key: ");
            fflush(stdout);
        } else if (c == 'm') {
            printf("key: ");
            fflush(stdout);
        }
    }
}
