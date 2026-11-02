// SPDX-License-Identifier: MPL-2.0
#include "input_events.h"
#include "unit.h"

static hal_wheel_state st(uint8_t buttons, uint8_t pos, bool touched) {
    return (hal_wheel_state){.buttons = buttons, .position = pos, .touched = touched, .valid = true};
}

TEST(button_edges) {
    input_event_t ev[8];
    hal_wheel_state a = st(0, 0, false), b = st(HAL_BTN_SELECT | HAL_BTN_MENU, 0, false);
    ASSERT_EQ(2, input_events_diff(&a, &b, ev, 8));
    ASSERT_EQ(INPUT_BUTTON_DOWN, ev[0].type);
    ASSERT_EQ(HAL_BTN_SELECT, ev[0].button);
    ASSERT_EQ(HAL_BTN_MENU, ev[1].button);
    ASSERT_EQ(1, input_events_diff(&b, &(hal_wheel_state){.buttons = HAL_BTN_MENU, .valid = true}, ev, 8));
    ASSERT_EQ(INPUT_BUTTON_UP, ev[0].type);
    ASSERT_EQ(HAL_BTN_SELECT, ev[0].button);
}

TEST(scroll_shortest_way_round) {
    input_event_t ev[8];
    hal_wheel_state a = st(0, 94, true), b = st(0, 2, true);
    ASSERT_EQ(1, input_events_diff(&a, &b, ev, 8));
    ASSERT_EQ(INPUT_SCROLL, ev[0].type);
    ASSERT_EQ(4, ev[0].steps); // 94 -> 2 clockwise across the seam
    ASSERT_EQ(1, input_events_diff(&b, &a, ev, 8));
    ASSERT_EQ(-4, ev[0].steps);
}

TEST(no_scroll_when_finger_lands) {
    input_event_t ev[8];
    hal_wheel_state a = st(0, 10, false), b = st(0, 50, true);
    ASSERT_EQ(0, input_events_diff(&a, &b, ev, 8));
    hal_wheel_state same = st(0, 50, true);
    ASSERT_EQ(0, input_events_diff(&b, &same, ev, 8));
}

TEST(invalid_frame_and_limit) {
    input_event_t ev[1];
    hal_wheel_state bad = {.valid = false}, a = st(0, 0, false);
    ASSERT_EQ(0, input_events_diff(&a, &bad, ev, 1));
    hal_wheel_state b = st(HAL_BTN_SELECT | HAL_BTN_PLAY, 0, false);
    ASSERT_EQ(1, input_events_diff(&a, &b, ev, 1)); // capped at max
}

TEST(console_keys) {
    input_event_t ev[2];
    ASSERT_EQ(2, input_events_from_key('\r', ev, 2));
    ASSERT_EQ(INPUT_BUTTON_DOWN, ev[0].type);
    ASSERT_EQ(HAL_BTN_SELECT, ev[0].button);
    ASSERT_EQ(INPUT_BUTTON_UP, ev[1].type);
    ASSERT_EQ(1, input_events_from_key('w', ev, 2));
    ASSERT_EQ(-4, ev[0].steps);
    ASSERT_EQ(1, input_events_from_key('s', ev, 2));
    ASSERT_EQ(4, ev[0].steps);
    ASSERT_EQ(HAL_BTN_MENU, (input_events_from_key('m', ev, 2), ev[0].button));
    ASSERT_EQ(0, input_events_from_key('z', ev, 2));
    ASSERT_EQ(0, input_events_from_key('\r', ev, 1)); // not enough room for press+release
}

int main(void) {
    RUN(button_edges);
    RUN(scroll_shortest_way_round);
    RUN(no_scroll_when_finger_lands);
    RUN(invalid_frame_and_limit);
    RUN(console_keys);
    return RESULT();
}
