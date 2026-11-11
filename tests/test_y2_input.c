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

TEST(a_tap_inside_one_poll_window_is_not_lost) {
    y2_input_t s;
    y2_input_init(&s);
    hal_wheel_state prev = {0};
    input_event_t ev[8];
    step(&s, &prev, ev);
    y2_event_t p = key(Y2_KEY_PLAYPAUSE, 1), r = key(Y2_KEY_PLAYPAUSE, 0);
    y2_input_feed(&s, &p, 100); // press and release both arrive before the next take (an 8 ms poll period)
    y2_input_feed(&s, &r, 105);
    ASSERT_EQ(1, step(&s, &prev, ev));
    ASSERT_EQ(INPUT_BUTTON_DOWN, ev[0].type);
    ASSERT_EQ(HAL_BTN_PLAY, ev[0].button);
    ASSERT_EQ(1, step(&s, &prev, ev)); // the release follows on the next poll
    ASSERT_EQ(INPUT_BUTTON_UP, ev[0].type);
    ASSERT_EQ(HAL_BTN_PLAY, ev[0].button);
    ASSERT_EQ(0, step(&s, &prev, ev)); // and then it is quiet
    // two taps of the same button in one window are still one down/up pair (they cannot be told apart here)
    y2_input_feed(&s, &p, 200);
    y2_input_feed(&s, &r, 201);
    y2_input_feed(&s, &p, 202);
    y2_input_feed(&s, &r, 203);
    ASSERT_EQ(1, step(&s, &prev, ev));
    ASSERT_EQ(INPUT_BUTTON_DOWN, ev[0].type);
    ASSERT_EQ(1, step(&s, &prev, ev));
    ASSERT_EQ(INPUT_BUTTON_UP, ev[0].type);
    // a button still held is reported held, then released normally
    y2_input_feed(&s, &p, 300);
    ASSERT_EQ(1, step(&s, &prev, ev));
    ASSERT_EQ(0, step(&s, &prev, ev));
    ASSERT_EQ(HAL_BTN_PLAY, prev.buttons);
    y2_input_feed(&s, &r, 400);
    ASSERT_EQ(1, step(&s, &prev, ev));
    ASSERT_EQ(INPUT_BUTTON_UP, ev[0].type);
}

TEST(release_without_press_is_ignored) {
    y2_input_t s;
    y2_input_init(&s);
    y2_input_take(&s, NULL);
    y2_event_t r = key(Y2_KEY_ENTER, 0);
    y2_input_feed(&s, &r, 10);
    ASSERT_FALSE(s.dirty);
}

TEST(menu_and_play_do_not_suppress_the_wheel) {
    static const uint16_t codes[] = {Y2_KEY_BACK, Y2_KEY_PLAYPAUSE};
    for (size_t i = 0; i < 2; i++) {
        y2_input_t s;
        y2_input_init(&s);
        y2_input_take(&s, NULL);
        y2_event_t p = key(codes[i], 1), tick = key(Y2_KEY_DOWN, 1);
        y2_input_feed(&s, &p, 1000);
        y2_input_feed(&s, &tick, 1010); // Menu, then straight into the list that appears
        ASSERT_EQ(4, s.position);
        ASSERT_EQ(0, s.ticks_dropped);
    }
}

TEST(wheel_tick_right_after_a_button_press_is_dropped) {
    y2_input_t s;
    y2_input_init(&s);
    y2_input_take(&s, NULL);
    y2_event_t p = key(Y2_KEY_ENTER, 1), tick = key(Y2_KEY_DOWN, 1);
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

// Press Power at t0 and release it after hold_ms (polling the hold timer on the way, as hal_input_poll does).
static void power_press(y2_input_t *s, uint32_t t0, uint32_t hold_ms) {
    y2_event_t pw = key(Y2_KEY_POWER, 1), pwr = key(Y2_KEY_POWER, 0);
    y2_input_feed(s, &pw, t0);
    y2_input_tick(s, t0 + hold_ms);
    y2_input_feed(s, &pwr, t0 + hold_ms);
}

TEST(short_power_press_toggles_screen_sleep) {
    y2_input_t s;
    y2_input_init(&s);
    y2_input_take(&s, NULL);
    bool asleep = true;
    ASSERT_FALSE(y2_input_take_sleep(&s, &asleep));
    power_press(&s, 1000, 300);
    ASSERT_EQ(1, s.power_presses);
    ASSERT_TRUE(y2_input_take_sleep(&s, &asleep));
    ASSERT_TRUE(asleep);
    ASSERT_FALSE(y2_input_take_sleep(&s, &asleep)); // reported once
    power_press(&s, 5000, Y2_POWER_HOLD_MS - 1); // just under the hold time is still short: wakes
    ASSERT_TRUE(y2_input_take_sleep(&s, &asleep));
    ASSERT_FALSE(asleep);
    ASSERT_FALSE(y2_input_take_shutdown(&s));
}

TEST(power_hold_requests_shutdown_once_while_still_held) {
    y2_input_t s;
    y2_input_init(&s);
    y2_event_t pw = key(Y2_KEY_POWER, 1), rep = key(Y2_KEY_POWER, 2), pwr = key(Y2_KEY_POWER, 0);
    y2_input_feed(&s, &pw, 1000);
    y2_input_tick(&s, 2999);
    ASSERT_FALSE(y2_input_take_shutdown(&s));
    y2_input_tick(&s, 3000); // exactly 2 s, no release yet
    ASSERT_TRUE(y2_input_take_shutdown(&s));
    y2_input_feed(&s, &rep, 3500); // autorepeat and further ticks do not fire again
    y2_input_tick(&s, 9000);
    ASSERT_FALSE(y2_input_take_shutdown(&s));
    y2_input_feed(&s, &pwr, 9100); // the release after a hold is not a short press
    bool asleep;
    ASSERT_FALSE(y2_input_take_sleep(&s, &asleep));
    ASSERT_FALSE(asleep);
    power_press(&s, 20000, 100); // a new press works normally again
    ASSERT_TRUE(y2_input_take_sleep(&s, &asleep));
    ASSERT_TRUE(asleep);
}

TEST(power_hold_seen_only_at_release_still_counts) {
    y2_input_t s;
    y2_input_init(&s);
    y2_event_t pw = key(Y2_KEY_POWER, 1), pwr = key(Y2_KEY_POWER, 0);
    y2_input_feed(&s, &pw, 0);
    y2_input_feed(&s, &pwr, 2500); // no tick in between (poll gap)
    ASSERT_TRUE(y2_input_take_shutdown(&s));
    bool asleep;
    ASSERT_FALSE(y2_input_take_sleep(&s, &asleep));
}

TEST(power_release_without_press_is_ignored) {
    y2_input_t s;
    y2_input_init(&s);
    y2_event_t pwr = key(Y2_KEY_POWER, 0);
    y2_input_feed(&s, &pwr, 100);
    bool asleep;
    ASSERT_FALSE(y2_input_take_sleep(&s, &asleep));
    ASSERT_FALSE(y2_input_take_shutdown(&s));
}

TEST(asleep_swallows_input_except_wake_keys) {
    y2_input_t s;
    y2_input_init(&s);
    hal_wheel_state prev = {0};
    input_event_t ev[8];
    ASSERT_EQ(0, step(&s, &prev, ev));
    power_press(&s, 100, 100);
    bool asleep;
    ASSERT_TRUE(y2_input_take_sleep(&s, &asleep));
    ASSERT_TRUE(asleep);
    y2_event_t down = key(Y2_KEY_DOWN, 1), left = key(Y2_KEY_LEFT, 1), left_up = key(Y2_KEY_LEFT, 0),
               play = key(Y2_KEY_PLAYPAUSE, 1), vol = key(Y2_KEY_VOLUMEUP, 1);
    y2_input_feed(&s, &down, 1000);
    y2_input_feed(&s, &left, 1010);
    y2_input_feed(&s, &left_up, 1020);
    y2_input_feed(&s, &play, 1030);
    y2_input_feed(&s, &vol, 1040);
    ASSERT_EQ(0, step(&s, &prev, ev)); // nothing reached the app
    ASSERT_EQ(0, y2_input_take_volume(&s));
    ASSERT_FALSE(y2_input_take_sleep(&s, &asleep)); // and none of it woke the screen
    // Centre wakes, and that press is not delivered as a Select
    y2_event_t sel = key(Y2_KEY_ENTER, 1), sel_up = key(Y2_KEY_ENTER, 0);
    y2_input_feed(&s, &sel, 2000);
    ASSERT_TRUE(y2_input_take_sleep(&s, &asleep));
    ASSERT_FALSE(asleep);
    y2_input_feed(&s, &sel_up, 2100);
    ASSERT_EQ(0, step(&s, &prev, ev));
    // awake again: the wheel works
    y2_input_feed(&s, &down, 3000);
    ASSERT_EQ(1, step(&s, &prev, ev));
    ASSERT_EQ(4, ev[0].steps);
    // Menu wakes too; Power wakes (tested above by toggle)
    power_press(&s, 4000, 100);
    ASSERT_TRUE(y2_input_take_sleep(&s, &asleep));
    ASSERT_TRUE(asleep);
    y2_event_t menu = key(Y2_KEY_BACK, 1);
    y2_input_feed(&s, &menu, 5000);
    ASSERT_TRUE(y2_input_take_sleep(&s, &asleep));
    ASSERT_FALSE(asleep);
    ASSERT_EQ(0, step(&s, &prev, ev));
    ASSERT_EQ(0, s.buttons);
}

TEST(sleep_releases_a_held_button) {
    y2_input_t s;
    y2_input_init(&s);
    hal_wheel_state prev = {0};
    input_event_t ev[8];
    step(&s, &prev, ev);
    y2_event_t sel = key(Y2_KEY_ENTER, 1), sel_up = key(Y2_KEY_ENTER, 0);
    y2_input_feed(&s, &sel, 100);
    step(&s, &prev, ev);
    ASSERT_EQ(HAL_BTN_SELECT, prev.buttons);
    power_press(&s, 200, 100);
    y2_input_feed(&s, &sel_up, 400); // swallowed while asleep
    step(&s, &prev, ev);
    ASSERT_EQ(0, prev.buttons); // not stuck held
}

TEST(hold_while_asleep_still_shuts_down) {
    y2_input_t s;
    y2_input_init(&s);
    power_press(&s, 0, 100);
    ASSERT_TRUE(s.asleep);
    power_press(&s, 1000, 2500);
    ASSERT_TRUE(y2_input_take_shutdown(&s));
}

TEST(other_buttons_during_a_power_hold_still_work_and_hold_still_fires) {
    y2_input_t s;
    y2_input_init(&s);
    hal_wheel_state w;
    y2_input_take(&s, &w);
    y2_event_t pw = key(Y2_KEY_POWER, 1), pwr = key(Y2_KEY_POWER, 0), rep = key(Y2_KEY_POWER, 2), sel = key(Y2_KEY_ENTER, 1),
               selu = key(Y2_KEY_ENTER, 0);
    bool asleep;
    y2_input_feed(&s, &pw, 1000);
    y2_input_feed(&s, &sel, 1500);
    y2_input_take(&s, &w);
    ASSERT_EQ(HAL_BTN_SELECT, w.buttons); // awake: Centre still reaches the app
    y2_input_feed(&s, &selu, 1700);
    y2_input_feed(&s, &rep, 2000);
    y2_input_tick(&s, 3000);
    ASSERT_TRUE(y2_input_take_shutdown(&s));
    y2_input_feed(&s, &pwr, 3100);
    ASSERT_FALSE(y2_input_take_sleep(&s, &asleep));
    ASSERT_FALSE(asleep);
}

TEST(wake_by_centre_while_power_held_stays_awake_on_release) {
    y2_input_t s;
    y2_input_init(&s);
    bool asleep;
    power_press(&s, 100, 100);
    ASSERT_TRUE(y2_input_take_sleep(&s, &asleep));
    ASSERT_TRUE(asleep);
    y2_event_t pw = key(Y2_KEY_POWER, 1), pwr = key(Y2_KEY_POWER, 0), sel = key(Y2_KEY_ENTER, 1), menu = key(Y2_KEY_BACK, 1);
    y2_input_feed(&s, &pw, 6000);
    y2_input_feed(&s, &sel, 6500); // Centre wakes during the hold
    ASSERT_TRUE(y2_input_take_sleep(&s, &asleep));
    ASSERT_FALSE(asleep);
    y2_input_feed(&s, &pwr, 6800); // short release: must not sleep again
    ASSERT_FALSE(y2_input_take_sleep(&s, &asleep));
    ASSERT_FALSE(s.asleep);
    power_press(&s, 8000, 100); // the next ordinary press toggles as usual
    ASSERT_TRUE(s.asleep);
    // Menu wake during a hold, then the hold still reaches 2 s
    y2_input_take_sleep(&s, &asleep);
    y2_input_feed(&s, &pw, 9000);
    y2_input_feed(&s, &menu, 9200);
    ASSERT_FALSE(s.asleep);
    y2_input_tick(&s, 11000);
    ASSERT_TRUE(y2_input_take_shutdown(&s));
}

TEST(power_lost_forgets_the_press_without_side_effects) {
    y2_input_t s;
    y2_input_init(&s);
    y2_event_t pw = key(Y2_KEY_POWER, 1), pwr = key(Y2_KEY_POWER, 0);
    bool asleep;
    y2_input_feed(&s, &pw, 1000);
    y2_input_power_lost(&s); // mtk-kpd closed mid-press
    y2_input_tick(&s, 9000);
    ASSERT_FALSE(y2_input_take_shutdown(&s));
    ASSERT_FALSE(y2_input_take_sleep(&s, &asleep));
    y2_input_feed(&s, &pwr, 9100); // a stray release is ignored
    ASSERT_FALSE(y2_input_take_sleep(&s, &asleep));
    power_press(&s, 10000, 100); // and the key works again afterwards
    ASSERT_TRUE(y2_input_take_sleep(&s, &asleep));
    ASSERT_TRUE(asleep);
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
    RUN(a_tap_inside_one_poll_window_is_not_lost);
    RUN(release_without_press_is_ignored);
    RUN(menu_and_play_do_not_suppress_the_wheel);
    RUN(wheel_tick_right_after_a_button_press_is_dropped);
    RUN(debounce_survives_millisecond_clock_wrap);
    RUN(no_debounce_before_any_press);
    RUN(side_keys_become_volume_steps);
    RUN(short_power_press_toggles_screen_sleep);
    RUN(power_hold_requests_shutdown_once_while_still_held);
    RUN(power_hold_seen_only_at_release_still_counts);
    RUN(power_release_without_press_is_ignored);
    RUN(asleep_swallows_input_except_wake_keys);
    RUN(sleep_releases_a_held_button);
    RUN(hold_while_asleep_still_shuts_down);
    RUN(other_buttons_during_a_power_hold_still_work_and_hold_still_fires);
    RUN(wake_by_centre_while_power_held_stays_awake_on_release);
    RUN(power_lost_forgets_the_press_without_side_effects);
    RUN(non_key_events_and_unknown_codes_are_ignored);
    RUN(console_filter_drops_newline_only_off_a_terminal);
    RUN(find_node_by_exact_device_name);
    return RESULT();
}
