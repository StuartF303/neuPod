// SPDX-License-Identifier: MPL-2.0
// Y2 charging screen: animation steps, the 10 s screen-on timer and wake events, and the drawn pixels at both panel sizes.
// With a directory argument (ctest passes one) it also writes PPM snapshots of every state for a look.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"
#include "y2_chargeview.h"

#define BG 0xFF101010u
#define GREEN 0xFF30B870u
#define RED 0xFFE84840u

static const char *g_dir;

static uint32_t *px(unsigned w, unsigned h) { return malloc((size_t)w * h * 4); }

static unsigned count(const uint32_t *p, unsigned n, uint32_t c) {
    unsigned k = 0;
    for (unsigned i = 0; i < n; i++) {
        k += p[i] == c;
    }
    return k;
}

static void snap(const char *name, unsigned w, unsigned h, const uint32_t *p) {
    if (g_dir == NULL) {
        return;
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%s-%ux%u.ppm", g_dir, name, w, h);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (unsigned i = 0; i < w * h; i++) {
        unsigned char rgb[3] = {(unsigned char)(p[i] >> 16), (unsigned char)(p[i] >> 8), (unsigned char)p[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

TEST(cells_and_frames) {
    ASSERT_EQ(0, y2_cview_target_cells(0));
    ASSERT_EQ(1, y2_cview_target_cells(1));
    ASSERT_EQ(1, y2_cview_target_cells(20));
    ASSERT_EQ(2, y2_cview_target_cells(21));
    ASSERT_EQ(4, y2_cview_target_cells(80));
    ASSERT_EQ(5, y2_cview_target_cells(81));
    ASSERT_EQ(5, y2_cview_target_cells(100));
    ASSERT_EQ(0, y2_cview_target_cells(Y2_CVIEW_PCT_UNKNOWN)); // no reading: empty
    // 60 % = 3 cells: empty, 1, 2, 3, then the level held, then around again
    ASSERT_EQ(5, y2_cview_frame_count(3));
    unsigned want[5] = {0, 1, 2, 3, 3};
    for (unsigned f = 0; f < 5; f++) {
        ASSERT_EQ(want[f], y2_cview_cells_shown(f, 3, false));
    }
    ASSERT_EQ(5, y2_cview_cells_shown(0, 3, true)); // full: static, all lit
    ASSERT_EQ(2, y2_cview_frame_count(0));          // 0 %: empty, empty
}

TEST(wake_events) {
    y2_event_t press = {Y2_EV_KEY, Y2_KEY_POWER, 1}, rel = {Y2_EV_KEY, Y2_KEY_POWER, 0}, rep = {Y2_EV_KEY, Y2_KEY_ENTER, 2};
    y2_event_t syn = {0, 0, 0}, msc = {4, 4, 7}, rel_axis = {2, 8, 1}, abs_pos = {3, 0, 90};
    ASSERT_TRUE(y2_cview_is_wake_event(&press));
    ASSERT_TRUE(!y2_cview_is_wake_event(&rel));
    ASSERT_TRUE(!y2_cview_is_wake_event(&rep));
    ASSERT_TRUE(!y2_cview_is_wake_event(&syn));
    ASSERT_TRUE(!y2_cview_is_wake_event(&msc));
    ASSERT_TRUE(y2_cview_is_wake_event(&rel_axis));
    ASSERT_TRUE(y2_cview_is_wake_event(&abs_pos));
}

TEST(awake_for_ten_seconds_then_off_then_key_wakes) {
    y2_cview_t v;
    y2_cview_scene_t s = {.pct = 60};
    y2_cview_init(&v, 1000);
    unsigned m = y2_cview_tick(&v, 1000, &s);
    ASSERT_EQ(Y2_CVIEW_BACKLIGHT_ON | Y2_CVIEW_DRAW, m); // first pass: draw, then light
    ASSERT_EQ(0, s.frame);
    ASSERT_EQ(Y2_CVIEW_DRAW, y2_cview_tick(&v, 1000 + Y2_CVIEW_FRAME_MS, &s)); // next step
    ASSERT_EQ(1, s.frame);
    ASSERT_EQ(0, y2_cview_tick(&v, 1000 + Y2_CVIEW_FRAME_MS + 250, &s)); // same step, nothing to draw
    // the cycle wraps: 5 steps for 60 %
    ASSERT_EQ(Y2_CVIEW_DRAW, y2_cview_tick(&v, 1000 + 5 * Y2_CVIEW_FRAME_MS, &s));
    ASSERT_EQ(0, s.frame);
    // a level change redraws within the same step
    s.pct = 61;
    ASSERT_EQ(Y2_CVIEW_DRAW, y2_cview_tick(&v, 1000 + 5 * Y2_CVIEW_FRAME_MS + 100, &s));
    // 9.999 s: still on; 10 s: off, once
    ASSERT_TRUE(y2_cview_is_awake(&v, 1000 + Y2_CVIEW_AWAKE_MS - 1));
    ASSERT_TRUE(!y2_cview_is_awake(&v, 1000 + Y2_CVIEW_AWAKE_MS));
    ASSERT_EQ(Y2_CVIEW_BACKLIGHT_OFF, y2_cview_tick(&v, 1000 + Y2_CVIEW_AWAKE_MS, &s));
    ASSERT_EQ(0, y2_cview_tick(&v, 1000 + Y2_CVIEW_AWAKE_MS + 250, &s));
    ASSERT_EQ(0, y2_cview_tick(&v, 1000 + 3600000u, &s)); // an hour later: still nothing to do
    // a key: draw first, light second, animation restarts from empty, another 10 s
    uint32_t t = 4000000u;
    y2_cview_wake(&v, t);
    ASSERT_EQ(Y2_CVIEW_BACKLIGHT_ON | Y2_CVIEW_DRAW, y2_cview_tick(&v, t + 50, &s));
    ASSERT_EQ(0, s.frame);
    ASSERT_TRUE(y2_cview_is_awake(&v, t + Y2_CVIEW_AWAKE_MS - 1));
}

TEST(key_while_on_extends_without_a_second_backlight_write) {
    y2_cview_t v;
    y2_cview_scene_t s = {.pct = 40};
    y2_cview_init(&v, 0);
    (void)y2_cview_tick(&v, 0, &s);
    y2_cview_wake(&v, 8000);
    unsigned m = y2_cview_tick(&v, 8100, &s);
    ASSERT_TRUE(!(m & Y2_CVIEW_BACKLIGHT_ON));
    ASSERT_TRUE(y2_cview_is_awake(&v, 17999));
    ASSERT_TRUE(!y2_cview_is_awake(&v, 18000));
}

TEST(a_noisy_wheel_cannot_hold_the_screen_on_forever) {
    y2_cview_t v;
    y2_cview_scene_t s = {.pct = 40};
    y2_cview_init(&v, 0);
    (void)y2_cview_tick(&v, 0, &s);
    uint32_t t = 0;
    for (; t < 3 * Y2_CVIEW_MAX_MOTION_MS; t += 250) { // a wheel that never stops
        y2_cview_wake_motion(&v, t);
        (void)y2_cview_tick(&v, t, &s);
    }
    // it went dark within 10 s of the cap and stays dark while the noise carries on
    ASSERT_TRUE(!y2_cview_is_awake(&v, Y2_CVIEW_MAX_MOTION_MS + Y2_CVIEW_AWAKE_MS + 250));
    // a key always wakes it
    y2_cview_wake(&v, t);
    ASSERT_TRUE(y2_cview_is_awake(&v, t + 1));
    // ordinary use: one wheel tick wakes a dark screen
    y2_cview_t w;
    y2_cview_init(&w, 0);
    (void)y2_cview_tick(&w, Y2_CVIEW_AWAKE_MS, &s);
    y2_cview_wake_motion(&w, 20000);
    ASSERT_TRUE(y2_cview_is_awake(&w, 20001));
}

TEST(motion_extension_keeps_the_animation_running) {
    y2_cview_t v;
    y2_cview_scene_t s = {.pct = 60};
    y2_cview_init(&v, 0);
    (void)y2_cview_tick(&v, 0, &s);
    y2_cview_wake_motion(&v, 1300); // does not restart the animation
    (void)y2_cview_tick(&v, 1300, &s);
    ASSERT_EQ(2, s.frame);
}

TEST(timer_survives_the_32_bit_clock_wrapping) {
    y2_cview_t v;
    y2_cview_scene_t s = {.pct = 40};
    y2_cview_init(&v, 0xFFFFFFFFu - 2000u);
    ASSERT_TRUE(y2_cview_is_awake(&v, 5000)); // 7 s later, across the wrap
    ASSERT_TRUE(!y2_cview_is_awake(&v, 9000));
    (void)s;
}

TEST(hold_bar_quantised_and_shown) {
    y2_cview_t v;
    y2_cview_scene_t s = {.pct = 50};
    y2_cview_init(&v, 0);
    (void)y2_cview_tick(&v, 0, &s);
    s.hold_ms = 130;
    ASSERT_TRUE(y2_cview_tick(&v, 130, &s) & Y2_CVIEW_DRAW);
    ASSERT_EQ(100, s.hold_ms);
    s.hold_ms = 150; // same 100 ms bucket: no redraw
    ASSERT_EQ(0, y2_cview_tick(&v, 150, &s));
}

static void check_size(unsigned w, unsigned h) {
    unsigned n = w * h;
    uint32_t *p = px(w, h);
    y2_cview_scene_t s = {.pct = 60};
    // 60 %: 0..3 segments green, the rest unlit
    unsigned prev = 0;
    for (unsigned f = 0; f < 5; f++) {
        s.frame = f;
        y2_cview_render(p, w, h, w, &s);
        unsigned g = count(p, n, GREEN);
        if (f == 0) {
            ASSERT_EQ(0, g);
        } else if (f <= 3) {
            ASSERT_TRUE(g > prev); // each step lights one more segment
        } else {
            ASSERT_EQ(prev, g);    // the hold repeats the level
        }
        prev = g;
        char nm[32];
        snprintf(nm, sizeof nm, "charging-60-f%u", f);
        if (f == 2) {
            snap(nm, w, h, p);
        }
    }
    ASSERT_EQ(BG, p[0]);
    ASSERT_EQ(BG, p[n - 1]); // nothing runs to the panel's corners
    unsigned seg3 = prev;

    // 5 segments of the same size: 100 % full shows five times one segment, and the percentage text adds some ink
    s = (y2_cview_scene_t){.pct = 100, .full = true};
    y2_cview_render(p, w, h, w, &s);
    ASSERT_TRUE(count(p, n, GREEN) > seg3 * 5 / 3);
    snap("charged", w, h, p);

    // low battery is red, not green
    s = (y2_cview_scene_t){.pct = 8, .frame = 1};
    y2_cview_render(p, w, h, w, &s);
    ASSERT_TRUE(count(p, n, RED) > 0);
    ASSERT_EQ(0, count(p, n, GREEN));
    snap("charging-8-f1", w, h, p);

    // 0 %: all empty, no fill
    s = (y2_cview_scene_t){.pct = 0};
    y2_cview_render(p, w, h, w, &s);
    ASSERT_EQ(0, count(p, n, GREEN) + count(p, n, RED));

    // Power held: the amber caption and bar appear
    s = (y2_cview_scene_t){.pct = 60, .frame = 3, .hold_ms = 1000};
    y2_cview_render(p, w, h, w, &s);
    ASSERT_TRUE(count(p, n, 0xFFE89828u) > 0);
    snap("hold", w, h, p);
    // no gauge reading: an empty battery and "--%", never a made-up number
    s = (y2_cview_scene_t){.pct = Y2_CVIEW_PCT_UNKNOWN, .frame = 1};
    y2_cview_render(p, w, h, w, &s);
    ASSERT_EQ(0, count(p, n, GREEN) + count(p, n, RED));
    snap("unknown", w, h, p);
    free(p);
}

TEST(draws_at_320x240) { check_size(320, 240); }
TEST(draws_at_480x360) { check_size(480, 360); }

TEST(stride_and_buffer_bounds) {
    // Rows wider than the picture (the framebuffer's line padding) are left alone; nothing is written past the last row.
    unsigned w = 320, h = 240, stride = 340;
    uint32_t *p = malloc((size_t)stride * (h + 1) * 4);
    for (size_t i = 0; i < (size_t)stride * (h + 1); i++) {
        p[i] = 0xDEADBEEFu;
    }
    y2_cview_scene_t s = {.pct = 100, .full = true, .hold_ms = 5000};
    y2_cview_render(p, w, h, stride, &s);
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = w; x < stride; x++) {
            ASSERT_EQ(0xDEADBEEFu, p[(size_t)y * stride + x]);
        }
    }
    for (unsigned x = 0; x < stride; x++) {
        ASSERT_EQ(0xDEADBEEFu, p[(size_t)h * stride + x]);
    }
    free(p);
}

int main(int argc, char **argv) {
    g_dir = argc > 1 ? argv[1] : NULL;
    RUN(cells_and_frames);
    RUN(wake_events);
    RUN(awake_for_ten_seconds_then_off_then_key_wakes);
    RUN(key_while_on_extends_without_a_second_backlight_write);
    RUN(a_noisy_wheel_cannot_hold_the_screen_on_forever);
    RUN(motion_extension_keeps_the_animation_running);
    RUN(timer_survives_the_32_bit_clock_wrapping);
    RUN(hold_bar_quantised_and_shown);
    RUN(draws_at_320x240);
    RUN(draws_at_480x360);
    RUN(stride_and_buffer_bounds);
    return RESULT();
}
