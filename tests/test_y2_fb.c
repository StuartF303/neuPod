// SPDX-License-Identifier: MPL-2.0
// Y2 framebuffer helpers: RGB565 -> XRGB8888, the mode check, the page count and the round-robin pager with its
// per-page stale-row tracking.
#include "unit.h"
#include "y2_fb.h"

static y2_fb_field_t f(uint32_t o, uint32_t l) { return (y2_fb_field_t){o, l}; }

TEST(expand_565_known_colours) {
    ASSERT_EQ(0xFF000000u, y2_fb_expand565(0x0000));
    ASSERT_EQ(0xFFFFFFFFu, y2_fb_expand565(0xFFFF)); // low bits replicate: white stays white
    ASSERT_EQ(0xFFFF0000u, y2_fb_expand565(0xF800)); // red
    ASSERT_EQ(0xFF00FF00u, y2_fb_expand565(0x07E0)); // green
    ASSERT_EQ(0xFF0000FFu, y2_fb_expand565(0x001F)); // blue
    ASSERT_EQ(0xFF080408u, y2_fb_expand565(0x0821)); // r=1 g=1 b=1 -> 8, 4, 8
}

TEST(expand_keeps_the_top_bits) {
    // Packing 8-bit channels the way HAL_RGB565 does and expanding keeps the top bits exactly.
    uint16_t p = (uint16_t)((((0x80) & 0xF8u) << 8) | (((0x40) & 0xFCu) << 3) | (0x20 >> 3));
    uint32_t x = y2_fb_expand565(p);
    ASSERT_EQ(0x84, (x >> 16) & 0xFF); // 0x80 -> 0x84 (5-bit replicated)
    ASSERT_EQ(0x41, (x >> 8) & 0xFF);
    ASSERT_EQ(0x21, x & 0xFF);
    ASSERT_EQ(0xFF, x >> 24);
}

TEST(convert_buffer) {
    const uint16_t src[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    uint32_t dst[5] = {0, 0, 0, 0, 0xDEADBEEF};
    y2_fb_convert_rgb565(src, dst, 4);
    ASSERT_EQ(0xFFFF0000u, dst[0]);
    ASSERT_EQ(0xFF00FF00u, dst[1]);
    ASSERT_EQ(0xFF0000FFu, dst[2]);
    ASSERT_EQ(0xFFFFFFFFu, dst[3]);
    ASSERT_EQ(0xDEADBEEFu, dst[4]); // no write past the count
}

TEST(mode_check_is_exactly_the_mtkfb_layout) {
    ASSERT_TRUE(y2_fb_mode_is_xrgb8888(32, f(16, 8), f(8, 8), f(0, 8), f(24, 8)));
    ASSERT_FALSE(y2_fb_mode_is_xrgb8888(16, f(11, 5), f(5, 6), f(0, 5), f(0, 0)));
    ASSERT_FALSE(y2_fb_mode_is_xrgb8888(32, f(0, 8), f(8, 8), f(16, 8), f(24, 8))); // BGRA
    ASSERT_FALSE(y2_fb_mode_is_xrgb8888(32, f(16, 8), f(8, 8), f(0, 8), f(0, 0))); // no alpha field
}

TEST(page_count_from_the_measured_geometry) {
    // 480x360, virtual 480x1104, line 1920, mapping of 3 pages (or a bit more)
    ASSERT_EQ(3, y2_fb_page_count(1920u * 1104u, 1104, 1920, 360));
    ASSERT_EQ(3, y2_fb_page_count(1920u * 1080u, 1104, 1920, 360));
    ASSERT_EQ(2, y2_fb_page_count(1920u * 720u, 1104, 1920, 360)); // mapping too small for 3
    ASSERT_EQ(1, y2_fb_page_count(1920u * 360u, 360, 1920, 360));
    ASSERT_EQ(1, y2_fb_page_count(100, 1104, 1920, 360)); // never 0
    ASSERT_EQ(1, y2_fb_page_count(1000, 0, 0, 0));
    ASSERT_EQ(Y2_FB_MAX_PAGES, y2_fb_page_count(1920u * 360u * 20u, 7200, 1920, 360));
}

TEST(round_robin_order) {
    ASSERT_EQ(1, y2_fb_next_page(0, 3));
    ASSERT_EQ(2, y2_fb_next_page(1, 3));
    ASSERT_EQ(0, y2_fb_next_page(2, 3));
    ASSERT_EQ(0, y2_fb_next_page(0, 1));
    ASSERT_EQ(0, y2_fb_next_page(5, 0));
}

TEST(pager_first_present_draws_everything_to_the_next_page) {
    y2_pager_t p;
    y2_pager_init(&p, 3, 0, 360); // pan-to-page-0 on open: page 0 is what we believe is visible
    unsigned page = 99;
    y2_rows_t r;
    ASSERT_TRUE(y2_pager_next(&p, &page, &r));
    ASSERT_EQ(1, page);
    ASSERT_EQ(0, r.lo);
    ASSERT_EQ(360, r.hi);
    ASSERT_EQ(1, p.visible);
}

TEST(pager_cycles_and_converts_only_what_changed) {
    y2_pager_t p;
    y2_pager_init(&p, 3, 0, 360);
    unsigned page;
    y2_rows_t r;
    for (int i = 0; i < 3; i++) { // every page starts stale: bring each up to date once
        ASSERT_TRUE(y2_pager_next(&p, &page, &r));
    }
    ASSERT_EQ(0, page); // 1, 2, 0
    ASSERT_FALSE(y2_pager_next(&p, &page, &r)); // nothing stale: no flip needed
    y2_pager_mark(&p, 100, 120);
    ASSERT_TRUE(y2_pager_next(&p, &page, &r));
    ASSERT_EQ(1, page);
    ASSERT_EQ(100, r.lo);
    ASSERT_EQ(120, r.hi);
    y2_pager_mark(&p, 10, 20); // second change: page 2 must catch up on BOTH, page 1 only on the new one
    ASSERT_TRUE(y2_pager_next(&p, &page, &r));
    ASSERT_EQ(2, page);
    ASSERT_EQ(10, r.lo);
    ASSERT_EQ(120, r.hi); // the union covers the earlier change too
    ASSERT_TRUE(y2_pager_next(&p, &page, &r));
    ASSERT_EQ(0, page); // page 0 missed both changes
    ASSERT_EQ(10, r.lo);
    ASSERT_EQ(120, r.hi);
    ASSERT_TRUE(y2_pager_next(&p, &page, &r));
    ASSERT_EQ(1, page); // page 1 was current for the first change: only the second is left
    ASSERT_EQ(10, r.lo);
    ASSERT_EQ(20, r.hi);
}

TEST(pager_clamps_and_ignores_empty_ranges) {
    y2_pager_t p;
    y2_pager_init(&p, 3, 0, 360);
    unsigned page;
    y2_rows_t r;
    for (int i = 0; i < 3; i++) {
        y2_pager_next(&p, &page, &r);
    }
    y2_pager_mark(&p, 50, 50); // empty
    y2_pager_mark(&p, 70, 60); // inverted
    ASSERT_FALSE(y2_pager_next(&p, &page, &r));
    y2_pager_mark(&p, 350, 9999); // clamped to the height
    ASSERT_TRUE(y2_pager_next(&p, &page, &r));
    ASSERT_EQ(350, r.lo);
    ASSERT_EQ(360, r.hi);
}

TEST(pager_single_page_redraws_in_place) {
    y2_pager_t p;
    y2_pager_init(&p, 1, 0, 360);
    unsigned page;
    y2_rows_t r;
    ASSERT_TRUE(y2_pager_next(&p, &page, &r));
    ASSERT_EQ(0, page);
    ASSERT_FALSE(y2_pager_next(&p, &page, &r));
    y2_pager_mark(&p, 5, 6);
    ASSERT_TRUE(y2_pager_next(&p, &page, &r));
    ASSERT_EQ(0, page);
    ASSERT_EQ(5, r.lo);
}

TEST(pager_visible_page_out_of_range_falls_back_to_zero) {
    y2_pager_t p;
    y2_pager_init(&p, 3, 7, 360);
    ASSERT_EQ(0, p.visible);
    y2_pager_init(&p, 99, 0, 360);
    ASSERT_EQ(Y2_FB_MAX_PAGES, p.pages);
}

int main(void) {
    RUN(expand_565_known_colours);
    RUN(expand_keeps_the_top_bits);
    RUN(convert_buffer);
    RUN(mode_check_is_exactly_the_mtkfb_layout);
    RUN(page_count_from_the_measured_geometry);
    RUN(round_robin_order);
    RUN(pager_first_present_draws_everything_to_the_next_page);
    RUN(pager_cycles_and_converts_only_what_changed);
    RUN(pager_clamps_and_ignores_empty_ranges);
    RUN(pager_single_page_redraws_in_place);
    RUN(pager_visible_page_out_of_range_falls_back_to_zero);
    return RESULT();
}
