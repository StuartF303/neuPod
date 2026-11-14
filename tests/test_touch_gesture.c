// SPDX-License-Identifier: MPL-2.0
// Touch gestures to wheel events (app/input/touch_gesture.c), and the "no touch panel" HAL (hal/common/hal_touch_none.c).
#include "touch_gesture.h"
#include "unit.h"

static touch_gesture_t g;
static input_event_t ev[4];

static size_t at(uint32_t t, int x, int y, bool down) {
    hal_touch_point_t p = {.x = (uint16_t)x, .y = (uint16_t)y, .down = down};
    return touch_gesture_update(&g, &p, t, ev, 4);
}

static int32_t sum_steps(size_t n) {
    int32_t s = 0;
    for (size_t i = 0; i < n; i++) {
        ASSERT_EQ(INPUT_SCROLL, ev[i].type);
        s += ev[i].steps;
    }
    return s;
}

#define PANEL_600 600, 450

TEST(constants_scale_with_the_panel) {
    touch_gesture_init(&g, 600, 450);
    ASSERT_EQ(22, g.slop_px);
    ASSERT_EQ(120, g.swipe_px);
    ASSERT_EQ(14, g.px_per_position); // 4 positions = 56 px = one 600x450 list row (row_h 56)
    touch_gesture_init(&g, 320, 240);
    ASSERT_EQ(12, g.slop_px);
    ASSERT_EQ(64, g.swipe_px);
    ASSERT_EQ(7, g.px_per_position); // 4 positions = 28 px (row_h 30)
    touch_gesture_init(&g, 40, 30); // absurdly small: floors, never zero
    ASSERT_EQ(8, g.slop_px);
    ASSERT_EQ(1, g.px_per_position);
}

TEST(a_quick_tap_is_select) {
    touch_gesture_init(&g, PANEL_600);
    ASSERT_EQ(0, at(1000, 300, 200, true));
    ASSERT_EQ(0, at(1050, 303, 202, true)); // jitter inside the slop
    ASSERT_EQ(2, at(1100, 303, 202, false));
    ASSERT_EQ(INPUT_BUTTON_DOWN, ev[0].type);
    ASSERT_EQ(HAL_BTN_SELECT, ev[0].button);
    ASSERT_EQ(INPUT_BUTTON_UP, ev[1].type);
    ASSERT_EQ(HAL_BTN_SELECT, ev[1].button);
    ASSERT_EQ(0, at(1110, 303, 202, false)); // nothing more without a finger
}

TEST(a_tap_just_under_the_long_press_time_is_still_a_tap) {
    touch_gesture_init(&g, PANEL_600);
    at(0, 100, 100, true);
    ASSERT_EQ(0, at(TOUCH_LONG_PRESS_MS - 1, 100, 100, true));
    ASSERT_EQ(2, at(TOUCH_LONG_PRESS_MS - 1, 100, 100, false));
    ASSERT_EQ(HAL_BTN_SELECT, ev[0].button);
}

TEST(a_still_finger_lifted_after_the_time_with_no_poll_between_is_still_play) {
    touch_gesture_init(&g, PANEL_600);
    at(100, 300, 200, true);
    ASSERT_EQ(2, at(100 + TOUCH_LONG_PRESS_MS, 300, 200, false)); // the samples stopped for a while (busy bus): the lift itself says 600 ms
    ASSERT_EQ(HAL_BTN_PLAY, ev[0].button);
    ASSERT_EQ(INPUT_BUTTON_UP, ev[1].type);
}

TEST(a_still_finger_for_600_ms_is_play_once_and_its_lift_is_silent) {
    touch_gesture_init(&g, PANEL_600);
    at(5000, 300, 200, true);
    ASSERT_EQ(0, at(5000 + TOUCH_LONG_PRESS_MS - 1, 300, 200, true));
    ASSERT_EQ(2, at(5000 + TOUCH_LONG_PRESS_MS, 300, 200, true));
    ASSERT_EQ(INPUT_BUTTON_DOWN, ev[0].type);
    ASSERT_EQ(HAL_BTN_PLAY, ev[0].button);
    ASSERT_EQ(INPUT_BUTTON_UP, ev[1].type);
    ASSERT_EQ(HAL_BTN_PLAY, ev[1].button);
    ASSERT_EQ(0, at(5000 + 2000, 300, 200, true)); // keeps holding: nothing more
    ASSERT_EQ(0, at(5000 + 2100, 300, 200, false)); // the lift is not a tap
    ASSERT_EQ(0, at(5000 + 2200, 300, 300, true)); // a new touch starts clean
    ASSERT_EQ(2, at(5000 + 2250, 300, 300, false));
    ASSERT_EQ(HAL_BTN_SELECT, ev[0].button);
}

TEST(dragging_up_scrolls_down_the_list_a_row_per_56_px) {
    touch_gesture_init(&g, PANEL_600);
    at(0, 300, 400, true);
    ASSERT_EQ(0, at(10, 300, 390, true)); // inside the slop
    ASSERT_EQ(1, at(20, 300, 340, true)); // 60 px up: 4 positions (14 px each) = 56 px, the 4th position at 56 px
    ASSERT_EQ(INPUT_SCROLL, ev[0].type);
    ASSERT_EQ(4, ev[0].steps);
    ASSERT_EQ(0, at(30, 300, 335, true)); // 65 px: still 4
    ASSERT_EQ(1, at(40, 300, 316, true)); // 84 px: 6 positions in all, 2 more
    ASSERT_EQ(2, ev[0].steps);
    ASSERT_EQ(0, at(50, 300, 316, false)); // lift after a drag: no tap
}

TEST(dragging_down_scrolls_up_and_going_back_undoes_it) {
    touch_gesture_init(&g, PANEL_600);
    at(0, 300, 100, true);
    ASSERT_EQ(-8, sum_steps(at(10, 300, 212, true))); // 112 px down: 8 positions up the list
    ASSERT_EQ(8, sum_steps(at(20, 300, 100, true)));   // back to where it came down: the 8 are given back
    ASSERT_EQ(0, at(30, 300, 100, false));
}

TEST(a_drag_is_total_from_the_touch_down_point_so_jitter_cannot_drift) {
    touch_gesture_init(&g, PANEL_600);
    at(0, 300, 300, true);
    int32_t total = 0;
    for (int i = 0; i < 40; i++) { // wobbling between 30 and 28 px up
        size_t n = at(10u + 10u * (uint32_t)i, 300, (i & 1) ? 272 : 270, true);
        total += n ? sum_steps(n) : 0;
    }
    ASSERT_EQ(2, total); // the last sample is 28 px up = 2 positions, however many samples got there
}

TEST(a_big_jump_is_split_into_int8_events) {
    touch_gesture_init(&g, PANEL_600);
    g.px_per_position = 1;
    at(0, 300, 449, true);
    int32_t total = 0;
    for (int i = 1; i <= 4; i++) { // the finger leaps 449 px in one sample: 449 positions, 127 at a time
        size_t n = at((uint32_t)i * 10u, 300, 0, true);
        ASSERT_EQ(1, n);
        ASSERT_TRUE(ev[0].steps > 0 && ev[0].steps <= 127);
        total += ev[0].steps;
    }
    ASSERT_EQ(449, total);
    ASSERT_EQ(0, at(60, 300, 0, true));
}

TEST(movement_before_the_long_press_time_cancels_the_long_press) {
    touch_gesture_init(&g, PANEL_600);
    at(0, 300, 200, true);
    at(500, 300, 240, true); // 40 px: past the slop, a drag now
    ASSERT_EQ(0, at(TOUCH_LONG_PRESS_MS + 100, 300, 240, true));
    ASSERT_EQ(0, at(TOUCH_LONG_PRESS_MS + 200, 300, 240, false));
}

TEST(a_swipe_right_is_menu_once) {
    touch_gesture_init(&g, PANEL_600);
    at(0, 100, 200, true);
    ASSERT_EQ(0, at(10, 160, 205, true));
    ASSERT_EQ(0, at(20, 210, 210, true)); // 110 px: not far enough
    ASSERT_EQ(2, at(30, 225, 212, true)); // 125 px
    ASSERT_EQ(INPUT_BUTTON_DOWN, ev[0].type);
    ASSERT_EQ(HAL_BTN_MENU, ev[0].button);
    ASSERT_EQ(INPUT_BUTTON_UP, ev[1].type);
    ASSERT_EQ(HAL_BTN_MENU, ev[1].button);
    ASSERT_EQ(0, at(40, 400, 212, true)); // still going: once per touch
    ASSERT_EQ(0, at(50, 400, 212, false));
}

TEST(a_swipe_left_does_nothing) {
    touch_gesture_init(&g, PANEL_600);
    at(0, 500, 200, true);
    ASSERT_EQ(0, at(10, 300, 202, true));
    ASSERT_EQ(0, at(20, 100, 205, true));
    ASSERT_EQ(0, at(30, 100, 205, false));
}

TEST(the_first_axis_out_of_the_slop_decides_the_touch) {
    // vertical first: a later sideways run is not a swipe
    touch_gesture_init(&g, PANEL_600);
    at(0, 100, 300, true);
    ASSERT_EQ(1, at(10, 105, 260, true)); // 40 px up, 5 across: a scroll (2 positions), not yet a full row
    ASSERT_EQ(0, at(20, 400, 260, true)); // 300 px right: ignored, still a scroll
    ASSERT_EQ(0, at(30, 400, 260, false));
    // horizontal first: a later vertical run is not a scroll
    touch_gesture_init(&g, PANEL_600);
    at(0, 100, 300, true);
    ASSERT_EQ(0, at(10, 140, 305, true)); // 40 px right, 5 down: a swipe in progress, short of the Menu distance
    ASSERT_EQ(0, at(20, 140, 100, true)); // 200 px up: ignored
    ASSERT_EQ(0, at(30, 140, 100, false));
}

TEST(cancel_forgets_the_touch_without_a_tap) {
    touch_gesture_init(&g, PANEL_600);
    at(0, 100, 100, true);
    touch_gesture_cancel(&g);
    ASSERT_EQ(0, at(50, 100, 100, false)); // the lift after a cancel is not a tap
    ASSERT_EQ(0, at(60, 100, 100, true));  // and the next contact starts a fresh touch
    ASSERT_EQ(2, at(110, 100, 100, false));
}

TEST(a_finger_held_through_a_cancel_or_at_start_up_must_lift_first) {
    touch_gesture_init(&g, PANEL_600);
    at(0, 100, 100, true);
    touch_gesture_cancel(&g);
    ASSERT_EQ(0, at(10, 100, 100, true));
    ASSERT_EQ(0, at(10 + TOUCH_LONG_PRESS_MS + 50, 100, 100, true)); // held on: no Play
    ASSERT_EQ(0, at(900, 100, 100, false));                          // lifted: no Select
    ASSERT_EQ(0, at(910, 100, 100, true));                           // a new touch
    ASSERT_EQ(2, at(950, 100, 100, false));
    ASSERT_EQ(HAL_BTN_SELECT, ev[0].button);
    touch_gesture_init(&g, PANEL_600); // start-up with the finger already down
    touch_gesture_cancel(&g);
    ASSERT_EQ(0, at(0, 50, 50, true));
    ASSERT_EQ(0, at(1000, 50, 400, true)); // even a drag is ignored
    ASSERT_EQ(0, at(1010, 50, 400, false));
}

TEST(the_millisecond_clock_wrapping_is_harmless) {
    touch_gesture_init(&g, PANEL_600);
    at(0xFFFFFF00u, 100, 100, true);
    ASSERT_EQ(0, at(0x00000050u, 100, 100, true)); // 336 ms later across the wrap: still waiting
    ASSERT_EQ(2, at(0x00000158u, 100, 100, true)); // 600 ms: Play
    ASSERT_EQ(HAL_BTN_PLAY, ev[0].button);
}

TEST(not_enough_room_sends_nothing_and_does_not_crash) {
    touch_gesture_init(&g, PANEL_600);
    hal_touch_point_t d = {.x = 10, .y = 10, .down = true}, u = {.x = 10, .y = 10, .down = false};
    input_event_t one[1];
    ASSERT_EQ(0, touch_gesture_update(&g, &d, 0, one, 1));
    ASSERT_EQ(0, touch_gesture_update(&g, &u, 50, one, 1)); // a tap needs two slots
}

TEST(a_board_without_touch_reports_none) {
    ASSERT_TRUE(!hal_touch_present());
    ASSERT_EQ(HAL_ENOTSUP, hal_touch_init());
    hal_touch_point_t p = {.x = 5, .y = 5, .down = true};
    ASSERT_TRUE(!hal_touch_read(&p));
    ASSERT_TRUE(!p.down);
    ASSERT_TRUE(!hal_touch_read(NULL));
}

int main(void) {
    RUN(constants_scale_with_the_panel);
    RUN(a_quick_tap_is_select);
    RUN(a_tap_just_under_the_long_press_time_is_still_a_tap);
    RUN(a_still_finger_lifted_after_the_time_with_no_poll_between_is_still_play);
    RUN(a_still_finger_for_600_ms_is_play_once_and_its_lift_is_silent);
    RUN(dragging_up_scrolls_down_the_list_a_row_per_56_px);
    RUN(dragging_down_scrolls_up_and_going_back_undoes_it);
    RUN(a_drag_is_total_from_the_touch_down_point_so_jitter_cannot_drift);
    RUN(a_big_jump_is_split_into_int8_events);
    RUN(movement_before_the_long_press_time_cancels_the_long_press);
    RUN(a_swipe_right_is_menu_once);
    RUN(a_swipe_left_does_nothing);
    RUN(the_first_axis_out_of_the_slop_decides_the_touch);
    RUN(cancel_forgets_the_touch_without_a_tap);
    RUN(a_finger_held_through_a_cancel_or_at_start_up_must_lift_first);
    RUN(the_millisecond_clock_wrapping_is_harmless);
    RUN(not_enough_room_sends_nothing_and_does_not_crash);
    RUN(a_board_without_touch_reports_none);
    return RESULT();
}
