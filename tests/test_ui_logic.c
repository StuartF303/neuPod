// SPDX-License-Identifier: MPL-2.0
#include "runtime_config.h"
#include "ui_logic.h"
#include "unit.h"

TEST(nav_clamps_and_window_follows) {
    ui_nav_t n;
    ui_nav_init(&n, 20, 9);
    ASSERT_FALSE(ui_nav_move(&n, -1)); // clamp at the top
    ASSERT_EQ(0, n.sel);
    ASSERT_TRUE(ui_nav_move(&n, 8));
    ASSERT_EQ(0, n.top); // still inside the first window
    ASSERT_TRUE(ui_nav_move(&n, 1));
    ASSERT_EQ(9, n.sel);
    ASSERT_EQ(1, n.top); // scrolled by exactly one row
    ASSERT_TRUE(ui_nav_move(&n, 100));
    ASSERT_EQ(19, n.sel);
    ASSERT_EQ(11, n.top); // last window: 11..19
    ASSERT_FALSE(ui_nav_move(&n, 1)); // clamp at the bottom
    ASSERT_TRUE(ui_nav_move(&n, -3));
    ASSERT_EQ(16, n.sel);
    ASSERT_EQ(11, n.top);
    ASSERT_TRUE(ui_nav_move(&n, -6));
    ASSERT_EQ(10, n.sel);
    ASSERT_EQ(10, n.top); // moved above the window: window follows
}

TEST(nav_short_and_empty_lists) {
    ui_nav_t n;
    ui_nav_init(&n, 0, 9);
    ASSERT_FALSE(ui_nav_move(&n, 1));
    ui_nav_init(&n, 3, 9);
    ASSERT_TRUE(ui_nav_move(&n, 5));
    ASSERT_EQ(2, n.sel);
    ASSERT_EQ(0, n.top);
    ui_nav_set_count(&n, 2); // list shrank
    ASSERT_EQ(1, n.sel);
    ui_nav_set(&n, 99);
    ASSERT_EQ(1, n.sel);
}

TEST(nav_set_centres) {
    ui_nav_t n;
    ui_nav_init(&n, 50, 9);
    ui_nav_set(&n, 25);
    ASSERT_EQ(25, n.sel);
    ASSERT_EQ(21, n.top); // 25 - 9/2
    ui_nav_set(&n, 49);
    ASSERT_EQ(41, n.top); // clamped to the last window
}

TEST(accel_slow_is_one_step_per_four_counts) {
    ui_accel_t a;
    ui_accel_init(&a);
    uint32_t t = 1000;
    int32_t total = 0;
    for (int i = 0; i < 8; i++) { // one count every 400 ms: far below the acceleration window
        total += ui_accel_feed(&a, 1, t);
        t += 400;
    }
    ASSERT_EQ(2, total);
    ASSERT_EQ(1, ui_accel_feed(&a, 4, t + 1000)); // a key press (+4) = one step
    ASSERT_EQ(-1, ui_accel_feed(&a, -4, t + 2000));
}

TEST(accel_fast_multiplies) {
    ui_accel_t a;
    ui_accel_init(&a);
    int32_t total = 0;
    uint32_t t = 5000;
    for (int i = 0; i < 20; i++) { // 4 counts every 10 ms
        total += ui_accel_feed(&a, 4, t);
        t += 10;
    }
    ASSERT_TRUE(total > 20); // plain speed would give exactly 20 steps
    ASSERT_TRUE(total <= 20 * 8);
    // after a pause the speed is back to normal
    ASSERT_EQ(1, ui_accel_feed(&a, 4, t + 1000));
}

TEST(accel_reversal_drops_residue) {
    ui_accel_t a;
    ui_accel_init(&a);
    ASSERT_EQ(0, ui_accel_feed(&a, 3, 1000)); // residue +3
    ASSERT_EQ(0, ui_accel_feed(&a, -1, 3000)); // reversed: residue restarts at -1, not +2
    ASSERT_EQ(-1, ui_accel_feed(&a, -3, 5000));
}

TEST(stack_push_pop) {
    ui_stack_t s;
    ui_stack_init(&s);
    int a = 1, b = 2, c = 3;
    ASSERT_TRUE(ui_stack_pop(&s) == NULL);
    ASSERT_TRUE(ui_stack_push(&s, &a));
    ASSERT_TRUE(ui_stack_push(&s, &b));
    ASSERT_TRUE(ui_stack_push(&s, &c));
    ASSERT_TRUE(ui_stack_top(&s) == &c);
    ASSERT_TRUE(ui_stack_below_top(&s) == &b);
    ASSERT_TRUE(ui_stack_pop(&s) == &c);
    ASSERT_TRUE(ui_stack_top(&s) == &b);
    ASSERT_TRUE(ui_stack_pop(&s) == &b);
    ASSERT_TRUE(ui_stack_below_top(&s) == NULL);
    ASSERT_TRUE(ui_stack_pop(&s) == &a);
    ASSERT_EQ(0, s.depth);
    for (int i = 0; i < UI_STACK_MAX; i++) {
        ASSERT_TRUE(ui_stack_push(&s, &a));
    }
    ASSERT_FALSE(ui_stack_push(&s, &a)); // full
    ASSERT_FALSE(ui_stack_push(&s, NULL));
}

TEST(signal_bars_thresholds) {
    const ui_signal_thr_t *w = &k_ui_signal_wifi;
    ASSERT_EQ(4, ui_signal_bars(-30, w, 0, 0));
    ASSERT_EQ(4, ui_signal_bars(-60, w, 0, 0));
    ASSERT_EQ(3, ui_signal_bars(-61, w, 0, 0));
    ASSERT_EQ(3, ui_signal_bars(-70, w, 0, 0));
    ASSERT_EQ(2, ui_signal_bars(-71, w, 0, 0));
    ASSERT_EQ(2, ui_signal_bars(-80, w, 0, 0));
    ASSERT_EQ(1, ui_signal_bars(-81, w, 0, 0));
    ASSERT_EQ(1, ui_signal_bars(-127, w, 0, 0));
    const ui_signal_thr_t *b = &k_ui_signal_bt;
    ASSERT_EQ(4, ui_signal_bars(-55, b, 0, 0));
    ASSERT_EQ(3, ui_signal_bars(-56, b, 0, 0));
    ASSERT_EQ(3, ui_signal_bars(-65, b, 0, 0));
    ASSERT_EQ(2, ui_signal_bars(-75, b, 0, 0));
    ASSERT_EQ(1, ui_signal_bars(-76, b, 0, 0));
    /* the same RSSI reads one step lower on Bluetooth between the two scales */
    ASSERT_EQ(4, ui_signal_bars(-58, w, 0, 0));
    ASSERT_EQ(3, ui_signal_bars(-58, b, 0, 0));
    ASSERT_EQ(1, ui_signal_class(1));
    ASSERT_EQ(2, ui_signal_class(2));
    ASSERT_EQ(3, ui_signal_class(3));
    ASSERT_EQ(3, ui_signal_class(4));
}

TEST(signal_bars_hysteresis) {
    const ui_signal_thr_t *w = &k_ui_signal_wifi;
    /* showing 3 bars: -60 is not enough to rise (needs -57), -73 is not enough to fall (needs below -73) */
    ASSERT_EQ(3, ui_signal_bars(-60, w, 3, 3));
    ASSERT_EQ(3, ui_signal_bars(-58, w, 3, 3));
    ASSERT_EQ(4, ui_signal_bars(-57, w, 3, 3));
    ASSERT_EQ(3, ui_signal_bars(-73, w, 3, 3));
    ASSERT_EQ(2, ui_signal_bars(-74, w, 3, 3));
    /* showing 4: stays until 4 dB below... i.e. below -63 */
    ASSERT_EQ(4, ui_signal_bars(-63, w, 4, 3));
    ASSERT_EQ(3, ui_signal_bars(-64, w, 4, 3));
    /* a wobble of +/-2 dB around the boundary never changes the bars */
    int bars = 3;
    for (int i = 0; i < 20; i++) {
        bars = ui_signal_bars(i % 2 ? -58 : -62, w, bars, 3);
        ASSERT_EQ(3, bars);
    }
    /* a big jump moves several steps at once */
    ASSERT_EQ(1, ui_signal_bars(-95, w, 4, 3));
    ASSERT_EQ(4, ui_signal_bars(-40, w, 1, 3));
    /* out-of-range previous = no hysteresis */
    ASSERT_EQ(4, ui_signal_bars(-60, w, 9, 3));
}

int main(void) {
    RUN(signal_bars_thresholds);
    RUN(signal_bars_hysteresis);
    RUN(nav_clamps_and_window_follows);
    RUN(nav_short_and_empty_lists);
    RUN(nav_set_centres);
    RUN(accel_slow_is_one_step_per_four_counts);
    RUN(accel_fast_multiplies);
    RUN(accel_reversal_drops_residue);
    RUN(stack_push_pop);
    return RESULT();
}
