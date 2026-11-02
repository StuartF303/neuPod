// SPDX-License-Identifier: MPL-2.0
// Fonts, primitives and the strip renderer (strip output must equal direct full-frame output).
#include <string.h>

#include "gfx.h"
#include "gfx_strip.h"
#include "unit.h"

#define W 320
#define H 240

// --- fonts ---------------------------------------------------------------------------------

TEST(test_font8x8_metrics) {
    ASSERT_EQ(8, gfx_font_8x8.height);
    ASSERT_EQ(7, gfx_font_8x8.ascent);
    ASSERT_EQ(0, gfx_text_width(&gfx_font_8x8, ""));
    ASSERT_EQ(8, gfx_text_width(&gfx_font_8x8, "i"));
    ASSERT_EQ(40, gfx_text_width(&gfx_font_8x8, "hello"));
    ASSERT_EQ(8 * 95, gfx_text_width(&gfx_font_8x8,
                                     " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                     "[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"));
}

TEST(test_font16_metrics) {
    const gfx_font_t *f = &gfx_font_16;
    ASSERT_EQ(15, f->height);
    ASSERT_EQ(12, f->ascent);
    ASSERT_TRUE(f->widths != NULL);
    for (int ch = f->first; ch <= f->last; ch++) {
        char s[2] = {(char)ch, 0};
        int w = gfx_text_width(f, s);
        ASSERT_TRUE(w >= 1 && w <= 16);
    }
    ASSERT_TRUE(gfx_text_width(f, "i") < gfx_text_width(f, "W")); // proportional
    ASSERT_TRUE(gfx_text_width(f, " ") > 0);
    ASSERT_EQ(gfx_text_width(f, "a") + gfx_text_width(f, "b"), gfx_text_width(f, "ab"));
    ASSERT_EQ(gfx_text_width(f, "?"), gfx_text_width(f, "\x01")); // out of range -> '?'
    ASSERT_EQ(gfx_text_width(f, "?"), gfx_text_width(f, "\xC3"));
}

TEST(test_text_draws_glyph_bits) {
    static hal_pixel_t buf[16 * 16];
    gfx_canvas_t c;
    gfx_canvas_init(&c, buf, 0, 0, 16, 16);
    gfx_clear(&c, 0);
    gfx_text(&c, &gfx_font_8x8, 2, 3, "A", 0xFFFF);
    const uint16_t *rows = gfx_font_8x8.rows + (size_t)('A' - 32) * 8;
    int set = 0;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int want = (rows[y] >> (15 - x)) & 1;
            ASSERT_EQ(want ? 0xFFFF : 0, buf[(3 + y) * 16 + 2 + x]);
            set += want;
        }
    }
    ASSERT_TRUE(set > 10);
    // Opaque background fills the whole cell.
    gfx_clear(&c, 0x1234);
    gfx_text_bg(&c, &gfx_font_8x8, 0, 0, " ", 0xFFFF, 0x0001);
    ASSERT_EQ(0x0001, buf[0]);
    ASSERT_EQ(0x0001, buf[7 * 16 + 7]);
    ASSERT_EQ(0x1234, buf[8]);
}

// --- primitives ----------------------------------------------------------------------------

TEST(test_clipping) {
    static hal_pixel_t buf[8 * 8];
    gfx_canvas_t c;
    gfx_canvas_init(&c, buf, 100, 50, 8, 8);
    gfx_clear(&c, 0);
    gfx_fill_rect(&c, 96, 46, 8, 8, 7); // covers local (0..3, 0..3) only
    ASSERT_EQ(7, buf[0]);
    ASSERT_EQ(7, buf[3 * 8 + 3]);
    ASSERT_EQ(0, buf[4]);
    ASSERT_EQ(0, buf[4 * 8]);
    gfx_pixel(&c, 99, 50, 9); // outside: ignored
    gfx_pixel(&c, 108, 50, 9);
    gfx_line(&c, 0, 0, 500, 500, 5); // must not write outside the buffer
    gfx_circle(&c, 104, 54, 100, 6);
    gfx_fill_circle(&c, 0, 0, 3, 6);
    ASSERT_EQ(0, buf[7]);
}

TEST(test_line_and_rect) {
    static hal_pixel_t buf[16 * 16];
    gfx_canvas_t c;
    gfx_canvas_init(&c, buf, 0, 0, 16, 16);
    gfx_clear(&c, 0);
    gfx_line(&c, 1, 1, 10, 10, 1);
    for (int i = 1; i <= 10; i++) {
        ASSERT_EQ(1, buf[i * 16 + i]);
    }
    gfx_rect(&c, 2, 12, 5, 3, 2);
    ASSERT_EQ(2, buf[12 * 16 + 2]);
    ASSERT_EQ(2, buf[14 * 16 + 6]);
    ASSERT_EQ(0, buf[13 * 16 + 4]); // hollow
}

TEST(test_circle_symmetry) {
    static hal_pixel_t buf[41 * 41];
    gfx_canvas_t c;
    gfx_canvas_init(&c, buf, 0, 0, 41, 41);
    gfx_clear(&c, 0);
    gfx_fill_circle(&c, 20, 20, 15, 1);
    ASSERT_EQ(1, buf[20 * 41 + 20]);
    ASSERT_EQ(1, buf[20 * 41 + 35]); // right extreme
    ASSERT_EQ(1, buf[5 * 41 + 20]);  // top extreme
    ASSERT_EQ(0, buf[20 * 41 + 36]);
    for (int y = 0; y < 41; y++) {
        for (int x = 0; x < 41; x++) {
            ASSERT_EQ(buf[y * 41 + x], buf[y * 41 + (40 - x)]);
            ASSERT_EQ(buf[y * 41 + x], buf[(40 - y) * 41 + x]);
        }
    }
}

// --- strip renderer ------------------------------------------------------------------------

static hal_pixel_t g_img[20 * 10];

static void scene(gfx_canvas_t *c, void *ctx) {
    (void)ctx;
    gfx_clear(c, 0x0000);
    gfx_fill_rect(c, 10, 5, 100, 40, 0xF800);
    gfx_rect(c, 0, 0, W, H, 0xFFFF);
    gfx_fill_circle(c, 200, 120, 30, 0x07E0);
    gfx_circle(c, 200, 120, 50, 0x001F);
    gfx_line(c, 0, 239, 319, 0, 0xFFE0);
    gfx_text(c, &gfx_font_16, 12, 14, "Strip test", 0xFFFF);
    gfx_text_bg(c, &gfx_font_8x8, 300, 15, "clip me at the edge", 0x0000, 0xFFFF);
    gfx_text(c, &gfx_font_8x8, 20, 232, "bottom", 0xFFFF);
    gfx_image(c, 40, 100, g_img, 20, 10);
    gfx_image(c, -5, 200, g_img, 20, 10); // partly off the left edge
}

static hal_pixel_t g_sink_fb[W * H];
static unsigned g_sink_windows, g_sink_blits, g_sink_waits;
static uint16_t g_wx, g_wy, g_ww, g_wh;
static const hal_pixel_t *g_last_buf;
static bool g_alternated = true;
static bool g_window_before_blit = true;
static bool g_window_pending;

static hal_status_t fake_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    g_wx = x;
    g_wy = y;
    g_ww = w;
    g_wh = h;
    g_sink_windows++;
    g_window_pending = true;
    return HAL_OK;
}

static hal_status_t fake_blit(const hal_pixel_t *p, size_t n) {
    if (!g_window_pending) {
        g_window_before_blit = false;
    }
    g_window_pending = false;
    if (g_last_buf == p) {
        g_alternated = false; // consecutive strips must use different buffers
    }
    g_last_buf = p;
    g_sink_blits++;
    if (n != (size_t)g_ww * g_wh) {
        return HAL_EINVAL;
    }
    for (size_t i = 0; i < n; i++) {
        g_sink_fb[(size_t)(g_wy + i / g_ww) * W + g_wx + i % g_ww] = p[i];
    }
    return HAL_OK;
}

static void fake_wait(void) { g_sink_waits++; }

static const gfx_sink_t k_fake = {fake_window, fake_blit, fake_wait};

TEST(test_strip_equals_direct) {
    for (size_t i = 0; i < sizeof g_img / sizeof g_img[0]; i++) {
        g_img[i] = (hal_pixel_t)(i * 321u + 5u);
    }
    static hal_pixel_t direct[W * H];
    gfx_canvas_t full;
    gfx_canvas_init(&full, direct, 0, 0, W, H);
    scene(&full, NULL);

    memset(g_sink_fb, 0xAA, sizeof g_sink_fb);
    ASSERT_EQ(HAL_OK, gfx_render_frame(W, H, scene, NULL, &k_fake));
    ASSERT_EQ(15, g_sink_windows);
    ASSERT_EQ(15, g_sink_blits);
    ASSERT_EQ(1, g_sink_waits);
    ASSERT_TRUE(g_alternated);
    ASSERT_TRUE(g_window_before_blit);
    ASSERT_EQ(0, memcmp(direct, g_sink_fb, sizeof direct));

    // Known pixels (independent of the comparison above).
    ASSERT_EQ(0xF800, g_sink_fb[30 * W + 90]);   // inside the red rect (below the text)
    ASSERT_EQ(0xFFFF, g_sink_fb[0]);             // border
    ASSERT_EQ(0x07E0, g_sink_fb[120 * W + 200]); // circle centre
    ASSERT_EQ(0x0000, g_sink_fb[100 * W + 300]); // background
    ASSERT_EQ(0xFFE0, g_sink_fb[239 * W + 0]);   // line start (bottom-left)
}

TEST(test_strip_partial_height_and_errors) {
    g_sink_windows = g_sink_blits = g_sink_waits = 0;
    ASSERT_EQ(HAL_OK, gfx_render_frame(100, 40, scene, NULL, &k_fake)); // 16 + 16 + 8 rows
    ASSERT_EQ(3, g_sink_windows);
    ASSERT_EQ(8, g_wh);
    ASSERT_EQ(32, g_wy);
    ASSERT_EQ(HAL_EINVAL, gfx_render_frame(321, 16, scene, NULL, &k_fake));
    ASSERT_EQ(HAL_EINVAL, gfx_render_frame(0, 16, scene, NULL, &k_fake));
    ASSERT_EQ(HAL_EINVAL, gfx_render_frame(16, 16, NULL, NULL, &k_fake));
}

int main(void) {
    RUN(test_font8x8_metrics);
    RUN(test_font16_metrics);
    RUN(test_text_draws_glyph_bits);
    RUN(test_clipping);
    RUN(test_line_and_rect);
    RUN(test_circle_symmetry);
    RUN(test_strip_equals_direct);
    RUN(test_strip_partial_height_and_errors);
    return RESULT();
}
