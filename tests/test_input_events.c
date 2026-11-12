// SPDX-License-Identifier: MPL-2.0
#include "input_events.h"
#include "runtime_config.h"
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

/* ---- the hand-over to the UI queue (UI-6): a fake bounded queue stands in for the RTOS queue ---- */
typedef struct {
    input_event_t ev[256];
    size_t n, cap;
} fake_q_t;

static bool fq_send(void *ctx, const input_event_t *e) {
    fake_q_t *q = ctx;
    if (q->n >= q->cap) {
        return false;
    }
    q->ev[q->n++] = *e;
    return true;
}

static void burst(input_pump_t *p, fake_q_t *q, int presses, int key) {
    for (int i = 0; i < presses; i++) {
        input_event_t ev[2];
        input_pump_send(p, fq_send, q, ev, input_events_from_key(key, ev, 2));
    }
}

TEST(a_burst_of_presses_fits_the_queue_depth_of_the_runtime) {
    /* 32 Menu presses (64 events) arriving between two UI passes: nothing may be lost at the depth the runtime uses */
    input_pump_t p;
    input_pump_init(&p);
    fake_q_t q = {.cap = RT_INPUT_QUEUE_LEN};
    burst(&p, &q, 32, 'm');
    ASSERT_EQ(0, p.dropped);
    ASSERT_EQ(64, q.n);
    /* the old depth of 16 lost presses 9 and later (the bench symptom): the counter now says so */
    input_pump_t old;
    input_pump_init(&old);
    fake_q_t small = {.cap = 16};
    burst(&old, &small, 12, 'm');
    ASSERT_EQ(16, small.n);
    ASSERT_EQ(8, old.dropped);
}

TEST(the_queue_depth_covers_a_uart_burst_with_headroom) {
    ASSERT_TRUE(RT_INPUT_QUEUE_LEN >= 64u);
}

TEST(power_and_switch_edges_are_retried_until_the_queue_takes_them) {
    input_pump_t p;
    input_pump_init(&p);
    fake_q_t q = {.cap = 0}; /* completely full */
    input_pump_levels(&p, fq_send, &q, true, HAL_HOLD_SWITCH_ON);
    ASSERT_EQ(0, q.n);
    ASSERT_EQ(0, p.dropped); /* a level is not "dropped": it is still owed */
    q.cap = 1;
    input_pump_levels(&p, fq_send, &q, true, HAL_HOLD_SWITCH_ON);
    ASSERT_EQ(1, q.n);
    ASSERT_EQ(INPUT_POWER_DOWN, q.ev[0].type);
    q.cap = 8;
    input_pump_levels(&p, fq_send, &q, true, HAL_HOLD_SWITCH_ON);
    ASSERT_EQ(2, q.n);
    ASSERT_EQ(INPUT_HOLD_SWITCH, q.ev[1].type);
    ASSERT_EQ(1, q.ev[1].steps);
    input_pump_levels(&p, fq_send, &q, true, HAL_HOLD_SWITCH_ON); /* nothing changed: nothing sent */
    ASSERT_EQ(2, q.n);
    input_pump_levels(&p, fq_send, &q, false, HAL_HOLD_SWITCH_OFF);
    ASSERT_EQ(4, q.n);
    ASSERT_EQ(INPUT_POWER_UP, q.ev[2].type);
    ASSERT_EQ(0, q.ev[3].steps);
}

TEST(a_platform_without_a_switch_or_power_key_sends_nothing) {
    input_pump_t p;
    input_pump_init(&p);
    fake_q_t q = {.cap = 8};
    for (int i = 0; i < 100; i++) {
        input_pump_levels(&p, fq_send, &q, false, HAL_HOLD_SWITCH_NONE);
    }
    ASSERT_EQ(0, q.n);
}

TEST(the_console_key_l_toggles_hold) {
    input_event_t ev[2];
    ASSERT_EQ(1, input_events_from_key('l', ev, 2));
    ASSERT_EQ(INPUT_HOLD_TOGGLE, ev[0].type);
    ASSERT_EQ(0, input_events_from_key('l', ev, 0));
}

int main(void) {
    RUN(button_edges);
    RUN(scroll_shortest_way_round);
    RUN(no_scroll_when_finger_lands);
    RUN(invalid_frame_and_limit);
    RUN(console_keys);
    RUN(a_burst_of_presses_fits_the_queue_depth_of_the_runtime);
    RUN(the_queue_depth_covers_a_uart_burst_with_headroom);
    RUN(power_and_switch_edges_are_retried_until_the_queue_takes_them);
    RUN(a_platform_without_a_switch_or_power_key_sends_nothing);
    RUN(the_console_key_l_toggles_hold);
    return RESULT();
}
