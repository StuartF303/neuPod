// SPDX-License-Identifier: MPL-2.0
// Y2 input: evdev decoding, the ring-button brush filter, the mapping onto the app's wheel model (checked through the
// real input_events_diff), side-key volume, the console key filter and node discovery by name.
#include "input_events.h"
#include "unit.h"
#include "y2_fake_fs.h"
#include "y2_input.h"

static void put_event(uint8_t *r, uint16_t type, uint16_t code, int32_t value) {
    memset(r, 0, Y2_EVENT_SIZE);
    r[0] = 0x78; // tv_sec / tv_usec: garbage, must be ignored
    r[8] = (uint8_t)type;
    r[9] = (uint8_t)(type >> 8);
    r[10] = (uint8_t)code;
    r[11] = (uint8_t)(code >> 8);
    for (int i = 0; i < 4; i++) {
        r[12 + i] = (uint8_t)((uint32_t)value >> (8 * i));
    }
}

static y2_event_t key(uint16_t code, int32_t value) { return (y2_event_t){.type = Y2_EV_KEY, .code = code, .value = value}; }

TEST(decode_whole_records_only) {
    uint8_t raw[Y2_EVENT_SIZE * 3 + 5];
    memset(raw, 0, sizeof raw);
    put_event(raw, Y2_EV_KEY, Y2_KEY_DOWN, 1);
    put_event(raw + 16, Y2_EV_KEY, Y2_KEY_DOWN, 0);
    put_event(raw + 32, 0 /* EV_SYN */, 0, -1);
    y2_event_t ev[8];
    size_t used = 0;
    ASSERT_EQ(3, y2_evdev_decode(raw, sizeof raw, ev, 8, &used));
    ASSERT_EQ(48, used); // the 5 trailing bytes are left for the next read
    ASSERT_EQ(Y2_EV_KEY, ev[0].type);
    ASSERT_EQ(Y2_KEY_DOWN, ev[0].code);
    ASSERT_EQ(1, ev[0].value);
    ASSERT_EQ(0, ev[1].value);
    ASSERT_EQ(-1, ev[2].value); // sign preserved
    ASSERT_EQ(2, y2_evdev_decode(raw, sizeof raw, ev, 2, &used)); // capped by max
    ASSERT_EQ(32, used);
    ASSERT_EQ(0, y2_evdev_decode(raw, 15, ev, 8, &used));
    ASSERT_EQ(0, used);
}

// What the app's input task does with consecutive wheel states.
static size_t step(y2_input_t *s, hal_wheel_state *prev, input_event_t *out) {
    hal_wheel_state cur;
    y2_input_take(s, &cur);
    size_t n = input_events_diff(prev, &cur, out, 8);
    *prev = cur;
    return n;
}

TEST(first_frame_then_detents_scroll_by_four) {
    y2_input_t s;
    y2_input_init(&s);
    hal_wheel_state prev = {0};
    input_event_t ev[8];
    ASSERT_EQ(0, step(&s, &prev, ev)); // first frame: the wheel boot check needs a valid one, no events
    ASSERT_TRUE(prev.valid);
    y2_event_t down = key(Y2_KEY_DOWN, 1), up = key(Y2_KEY_UP, 1), rel = key(Y2_KEY_DOWN, 0);
    y2_input_feed(&s, &down, 1000);
    ASSERT_EQ(1, step(&s, &prev, ev)); // the very first detent is not swallowed
    ASSERT_EQ(INPUT_SCROLL, ev[0].type);
    ASSERT_EQ(4, ev[0].steps); // clockwise, one list step, as a console 's'
    y2_input_feed(&s, &rel, 1010);
    ASSERT_EQ(0, step(&s, &prev, ev)); // the release carries nothing
    y2_input_feed(&s, &up, 1100);
    ASSERT_EQ(1, step(&s, &prev, ev));
    ASSERT_EQ(-4, ev[0].steps); // anticlockwise
    y2_input_feed(&s, &up, 1200);
    y2_input_feed(&s, &up, 1210); // two detents between polls: one event, two list steps
    ASSERT_EQ(1, step(&s, &prev, ev));
    ASSERT_EQ(-8, ev[0].steps);
}

TEST(position_wraps_the_ring) {
    y2_input_t s;
    y2_input_init(&s);
    hal_wheel_state prev = {0};
    input_event_t ev[8];
    step(&s, &prev, ev);
    y2_event_t up = key(Y2_KEY_UP, 1);
    y2_input_feed(&s, &up, 0); // 0 -> 92
    ASSERT_EQ(1, step(&s, &prev, ev));
    ASSERT_EQ(-4, ev[0].steps); // the shortest way round across the seam
    ASSERT_EQ(HAL_WHEEL_POSITIONS - 4, s.position);
    y2_event_t down = key(Y2_KEY_DOWN, 1);
    y2_input_feed(&s, &down, 500); // 92 -> 0
    ASSERT_EQ(1, step(&s, &prev, ev));
    ASSERT_EQ(4, ev[0].steps);
}

TEST(ring_buttons_map_and_hold_duration_is_real) {
    static const struct {
        uint16_t code;
        uint8_t btn;
    } map[] = {{Y2_KEY_ENTER, HAL_BTN_SELECT},
               {Y2_KEY_BACK, HAL_BTN_MENU},
               {Y2_KEY_LEFT, HAL_BTN_LEFT},
               {Y2_KEY_RIGHT, HAL_BTN_RIGHT},
               {Y2_KEY_PLAYPAUSE, HAL_BTN_PLAY}};
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        y2_input_t s;
        y2_input_init(&s);
        hal_wheel_state prev = {0};
        input_event_t ev[8];
        step(&s, &prev, ev);
        y2_event_t p = key(map[i].code, 1), r = key(map[i].code, 0), rep = key(map[i].code, 2);
        y2_input_feed(&s, &p, 100);
        ASSERT_EQ(1, step(&s, &prev, ev));
        ASSERT_EQ(INPUT_BUTTON_DOWN, ev[0].type);
        ASSERT_EQ(map[i].btn, ev[0].button);
        y2_input_feed(&s, &rep, 300); // autorepeat is not a new press
        ASSERT_EQ(0, step(&s, &prev, ev));
        ASSERT_EQ(map[i].btn, prev.buttons); // still held: a long press has a real duration
        y2_input_feed(&s, &r, 530);
        ASSERT_EQ(1, step(&s, &prev, ev));
        ASSERT_EQ(INPUT_BUTTON_UP, ev[0].type);
        ASSERT_EQ(map[i].btn, ev[0].button);
        ASSERT_EQ(0, prev.buttons);
    }
}

TEST(release_without_press_is_ignored) {
    y2_input_t s;
    y2_input_init(&s);
    y2_input_take(&s, NULL);
    y2_event_t r = key(Y2_KEY_ENTER, 0);
    y2_input_feed(&s, &r, 10);
    ASSERT_FALSE(s.dirty);
}

TEST(wheel_tick_right_after_a_button_press_is_dropped) {
    y2_input_t s;
    y2_input_init(&s);
    y2_input_take(&s, NULL);
    y2_event_t p = key(Y2_KEY_BACK, 1), tick = key(Y2_KEY_DOWN, 1);
    y2_input_feed(&s, &p, 10000);
    y2_input_take(&s, NULL);
    y2_input_feed(&s, &tick, 10000 + Y2_TICK_AFTER_PRESS_IGNORE_MS - 1);
    ASSERT_FALSE(s.dirty);
    ASSERT_EQ(0, s.position);
    ASSERT_EQ(1, s.ticks_dropped);
    y2_input_feed(&s, &tick, 10000 + Y2_TICK_AFTER_PRESS_IGNORE_MS); // 150 ms later it is a real turn
    ASSERT_TRUE(s.dirty);
    ASSERT_EQ(4, s.position);
}

TEST(debounce_survives_millisecond_clock_wrap) {
    y2_input_t s;
    y2_input_init(&s);
    y2_event_t p = key(Y2_KEY_ENTER, 1), tick = key(Y2_KEY_UP, 1);
    y2_input_feed(&s, &p, 0xFFFFFFF0u);
    y2_input_take(&s, NULL);
    y2_input_feed(&s, &tick, 20); // 36 ms later across the wrap
    ASSERT_FALSE(s.dirty);
}

TEST(no_debounce_before_any_press) {
    y2_input_t s;
    y2_input_init(&s);
    y2_input_take(&s, NULL);
    y2_event_t tick = key(Y2_KEY_DOWN, 1);
    y2_input_feed(&s, &tick, 5); // boot: a tick right away is real
    ASSERT_TRUE(s.dirty);
}

TEST(side_keys_become_volume_steps) {
    y2_input_t s;
    y2_input_init(&s);
    y2_event_t vu = key(Y2_KEY_VOLUMEUP, 1), vd = key(Y2_KEY_VOLUMEDOWN, 1), vur = key(Y2_KEY_VOLUMEUP, 0), rep = key(Y2_KEY_VOLUMEUP, 2);
    y2_input_feed(&s, &vu, 0);
    y2_input_feed(&s, &vur, 0);
    ASSERT_EQ(1, y2_input_take_volume(&s));
    ASSERT_EQ(0, y2_input_take_volume(&s));
    y2_input_feed(&s, &vd, 0);
    y2_input_feed(&s, &vd, 0);
    y2_input_feed(&s, &rep, 0); // holding repeats
    ASSERT_EQ(-1, y2_input_take_volume(&s));
    // volume keys never reach the wheel state
    ASSERT_EQ(0, s.buttons);
    ASSERT_EQ(0, s.position);
}

TEST(power_key_is_counted_not_mapped) {
    y2_input_t s;
    y2_input_init(&s);
    y2_input_take(&s, NULL);
    y2_event_t pw = key(Y2_KEY_POWER, 1), pwr = key(Y2_KEY_POWER, 0);
    y2_input_feed(&s, &pw, 0);
    y2_input_feed(&s, &pwr, 0);
    ASSERT_EQ(1, s.power_presses);
    ASSERT_FALSE(s.dirty);
}

TEST(non_key_events_and_unknown_codes_are_ignored) {
    y2_input_t s;
    y2_input_init(&s);
    y2_input_take(&s, NULL);
    y2_event_t syn = {.type = 0, .code = Y2_KEY_DOWN, .value = 1}, unk = key(30 /* KEY_A */, 1);
    y2_input_feed(&s, &syn, 0);
    y2_input_feed(&s, &unk, 0);
    ASSERT_FALSE(s.dirty);
}

TEST(console_filter_drops_newline_only_off_a_terminal) {
    ASSERT_EQ('s', y2_console_key('s', false));
    ASSERT_EQ(-1, y2_console_key('\n', false)); // echo s > fifo must not also press Select
    ASSERT_EQ(-1, y2_console_key('\r', false));
    ASSERT_EQ('e', y2_console_key('e', false));
    ASSERT_EQ('\n', y2_console_key('\n', true)); // a real Enter on a terminal
    input_event_t ev[2];
    ASSERT_EQ(2, input_events_from_key(y2_console_key('e', false), ev, 2)); // e = Select
}

TEST(find_node_by_exact_device_name) {
    fs_new();
    fs_write("sys/class/input/event0/device/name", "mtk-kpd\n");
    fs_write("sys/class/input/event1/device/name", "ACCDET\n");
    fs_write("sys/class/input/event2/device/name", "mtk-tpd\n");
    fs_write("sys/class/input/event3/device/name", "mtk-tpd-kpd\n");
    fs_write("sys/class/input/input7/name", "ignored\n"); // not an event node
    char cls[300], out[64];
    fs_path("sys/class/input", cls, sizeof cls);
    ASSERT_TRUE(y2_input_find_node(cls, "/dev/input", "mtk-tpd", out, sizeof out));
    ASSERT_EQ(0, strcmp(out, "/dev/input/event2")); // not event3, whose name only starts with mtk-tpd
    ASSERT_TRUE(y2_input_find_node(cls, "/dev/input", "mtk-kpd", out, sizeof out));
    ASSERT_EQ(0, strcmp(out, "/dev/input/event0"));
    ASSERT_FALSE(y2_input_find_node(cls, "/dev/input", "nothing", out, sizeof out));
    ASSERT_FALSE(y2_input_find_node("/nonexistent/dir", "/dev/input", "mtk-tpd", out, sizeof out));
    fs_destroy();
}

int main(void) {
    RUN(decode_whole_records_only);
    RUN(first_frame_then_detents_scroll_by_four);
    RUN(position_wraps_the_ring);
    RUN(ring_buttons_map_and_hold_duration_is_real);
    RUN(release_without_press_is_ignored);
    RUN(wheel_tick_right_after_a_button_press_is_dropped);
    RUN(debounce_survives_millisecond_clock_wrap);
    RUN(no_debounce_before_any_press);
    RUN(side_keys_become_volume_steps);
    RUN(power_key_is_counted_not_mapped);
    RUN(non_key_events_and_unknown_codes_are_ignored);
    RUN(console_filter_drops_newline_only_off_a_terminal);
    RUN(find_node_by_exact_device_name);
    return RESULT();
}
