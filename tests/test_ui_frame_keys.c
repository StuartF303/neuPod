// SPDX-License-Identifier: MPL-2.0
// ui_frame_input() routing of the transport keys, with the real ui_frame.c (included, so the test can seat a fake screen on its stack) and
// fakes for the player and the services around it: Play, Left and Right do their global job only when the top screen did not consume the
// key, Right / Left only while a queue plays; a button release reaches the top screen's on_button_up. The pure rules are in test_ui_logic.c;
// this is the wiring (the "consumed" result of the screen being honoured), which the tours only catch slowly.
#include <stdio.h>
#include <string.h>

#include "../app/ui/ui_frame.c" /* white box: g_stack */
#include "unit.h"

void neupod_panic_assert(const char *file, int line) { // LVGL's assert hook (never reached: no LVGL object is created here)
    printf("assert %s:%d\n", file, line);
    __builtin_trap();
}

// ---- fakes: the player ------------------------------------------------------------------------------------------------------
static player_info_t g_pi;
static int g_pause, g_next, g_prev;
void player_get(player_info_t *o) { *o = g_pi; }
uint32_t player_volume_att(void) { return 0; }
int bt_volume_get(void) { return -1; }
unsigned bt_vol_percent(uint8_t v) { return v; }
uint32_t bt_service_gave_up_seq(void) { return 0; }
uint32_t bt_service_lost_seq(void) { return 0; }
uint32_t player_skip_notice(char *reason, size_t n) {
    if (n > 0) {
        reason[0] = '\0';
    }
    return 0;
}
bool ui_library_background(void) { return false; }
bool bt_service_reconnecting(void) { return false; }
uint32_t bt_service_reconnected_seq(void) { return 0; }
void player_pause_toggle(void) { g_pause++; }
void player_next(void) { g_next++; }
void player_prev(void) { g_prev++; }

// ---- fakes: everything else ui_frame.c links against ---------------------------------------------------------------------------
void ui_flow_settle(void) {}
uint32_t os_tick_ms(void) { return 1000; }
bool battery_store_get(hal_battery_status_t *b) { (void)b; return false; }
void bt_service_tick(void) {}
hal_bt_state_t hal_bt_state(void) { return HAL_BT_OFF; }
hal_bt_stream_state_t hal_bt_stream_state(void) { return HAL_BT_STREAM_CLOSED; }
hal_bt_link_state_t hal_bt_link_state(void) { return HAL_BT_LINK_DISCONNECTED; }
bool bt_saved_get(hal_bt_device_t *o) { (void)o; return false; }
audio_route_notice_t audio_route_notice(uint32_t *seq) { *seq = 0; return ROUTE_NOTICE_NONE; }
static bool g_haptics_present;
bool caps_available(cap_id_t c) { return c == CAP_HAPTICS && g_haptics_present; }
bool caps_all_resolved(void) { return true; }
hal_wifi_status hal_wifi_state(void) { return HAL_WIFI_DOWN; }
void wifi_get_info(wifi_info_t *i) { memset(i, 0, sizeof *i); }
void rt_haptic_settings_loaded(void) {}
static int g_clicks;
void rt_haptic_click(void) { g_clicks++; }
bool hal_system_can_poweroff(void) { return true; }
bool hal_system_poweroff(void) { return true; }
bool hal_input_has_power_key(void) { return true; }
static int g_backlight = -1;
void rt_piezo_click(void) {}
uint32_t settings_generation(void) { return 0; }
bool settings_get_u8(uint32_t tag, uint8_t *out) { (void)tag; (void)out; return false; }
void settings_set_u8(uint32_t tag, uint8_t v) { (void)tag; (void)v; }
void hal_display_backlight(uint8_t level) { g_backlight = level; }
void ui_log_screen(const char *t, const char *w, const char *n) { (void)t; (void)w; (void)n; }

// ---- a fake screen -------------------------------------------------------------------------------------------------------------
static ui_screen_t g_scr;
static bool g_consume; // what on_button answers
static int g_btn_calls, g_up_calls;
static uint8_t g_last_up;
static bool scr_button(ui_screen_t *s, uint8_t b) { (void)s; (void)b; g_btn_calls++; return g_consume; }
static void scr_up(ui_screen_t *s, uint8_t b) { (void)s; g_up_calls++; g_last_up = b; }

static int g_scrolls;
static void scr_scroll(ui_screen_t *s, int32_t steps) { (void)s; (void)steps; g_scrolls++; }

static void press(uint8_t btn) {
    input_event_t e = {.type = INPUT_BUTTON_DOWN, .button = btn};
    ui_frame_input(&e, 1000);
}
static void reset(bool queue, bool consume) {
    memset(&g_pi, 0, sizeof g_pi);
    if (queue) {
        g_pi.src = PLAYER_SRC_FILE;
        g_pi.state = PLAYER_PLAYING;
        g_pi.queue_len = 3;
        g_pi.queue_pos = 2;
    }
    g_pause = g_next = g_prev = g_btn_calls = g_up_calls = g_scrolls = g_clicks = 0;
    g_consume = consume;
    g_swallowed = 0;
    g_delivered = 0;
    g_idle.last_ms = 1000; /* not idle, not dimmed, not off */
    g_idle.dimmed = g_idle.off = g_idle.forced_off = false;
    ui_hold_init(&g_hold);
    g_ui_state.autolock = true;
}

// ---- Hold (docs/adr/0020-hold.md): what the frame does with the input once the Hold state or the screen state says "swallow" ----
static void ev_at(input_event_type_t t, uint32_t now) {
    input_event_t e = {.type = t};
    ui_frame_input(&e, now);
}
static void scroll_at(uint32_t now) {
    input_event_t e = {.type = INPUT_SCROLL, .steps = 4};
    ui_frame_input(&e, now);
}
static void release(uint8_t btn) {
    input_event_t e = {.type = INPUT_BUTTON_UP, .button = btn};
    ui_frame_input(&e, 1000);
}

TEST(locked_input_never_reaches_the_screen_or_the_player) {
    reset(true, false);
    scroll_at(1000);
    ASSERT_EQ(1, g_scrolls); // control: unlocked, the wheel reaches the screen
    g_scrolls = 0;
    ASSERT_FALSE(ui_locked());
    ev_at(INPUT_HOLD_TOGGLE, 1000);
    ASSERT_TRUE(ui_frame_locked());
    ASSERT_TRUE(ui_locked()); // what the Y2's side volume keys check
    press(HAL_BTN_PLAY);
    press(HAL_BTN_RIGHT);
    press(HAL_BTN_MENU);
    press(HAL_BTN_SELECT);
    release(HAL_BTN_RIGHT);
    scroll_at(1000);
    ASSERT_EQ(0, g_pause + g_next + g_prev);
    ASSERT_EQ(0, g_btn_calls + g_up_calls + g_scrolls); // the screen was never asked
    ASSERT_EQ(0, g_swallowed & HAL_BTN_RIGHT);          // the swallowed press's release was swallowed too
}

TEST(unlocking_gives_the_input_back_and_a_stale_release_is_still_swallowed) {
    reset(true, false);
    ev_at(INPUT_HOLD_TOGGLE, 1000);
    press(HAL_BTN_RIGHT); // swallowed while locked
    ev_at(INPUT_HOLD_TOGGLE, 1000);
    ASSERT_FALSE(ui_frame_locked());
    release(HAL_BTN_RIGHT); // the release of the swallowed press: not a "key up" the screen never saw go down
    ASSERT_EQ(0, g_up_calls);
    press(HAL_BTN_RIGHT);
    ASSERT_EQ(1, g_next);
    scroll_at(1000);
    ASSERT_EQ(1, g_scrolls);
}

TEST(swallowed_keys_give_no_click_but_the_lock_and_unlock_do) {
    reset(true, false);
    g_haptics_present = true;
    ev_at(INPUT_HOLD_TOGGLE, 1000);
    ASSERT_EQ(1, g_clicks); // the slide-switch click
    press(HAL_BTN_SELECT);
    scroll_at(1000);
    ASSERT_EQ(1, g_clicks);
    ev_at(INPUT_HOLD_TOGGLE, 1000);
    ASSERT_EQ(2, g_clicks);
    g_haptics_present = false;
}

TEST(a_short_power_press_turns_the_screen_off_and_locks_with_auto_lock) {
    reset(false, false);
    ev_at(INPUT_POWER_DOWN, 1000);
    ev_at(INPUT_POWER_UP, 1200);
    ASSERT_TRUE(g_idle.off);
    ASSERT_EQ(0, g_backlight);
    ASSERT_TRUE(ui_frame_locked());
}

TEST(the_waking_input_is_swallowed_and_the_screen_stays_locked) {
    reset(true, false);
    ev_at(INPUT_POWER_DOWN, 1000);
    ev_at(INPUT_POWER_UP, 1200);
    press(HAL_BTN_PLAY); // wakes the screen and nothing else
    ASSERT_FALSE(g_idle.off);
    ASSERT_TRUE(g_backlight > 0);
    ASSERT_EQ(0, g_pause);
    ASSERT_TRUE(ui_frame_locked()); // still locked: only a hold of Power (or the switch) unlocks
    press(HAL_BTN_PLAY);
    ASSERT_EQ(0, g_pause);
}

TEST(without_auto_lock_the_screen_wakes_unlocked_and_only_the_first_input_is_lost) {
    reset(true, false);
    g_ui_state.autolock = false;
    ev_at(INPUT_POWER_DOWN, 1000);
    ev_at(INPUT_POWER_UP, 1200);
    ASSERT_TRUE(g_idle.off);
    ASSERT_FALSE(ui_frame_locked());
    press(HAL_BTN_PLAY); // the waking press
    release(HAL_BTN_PLAY);
    ASSERT_EQ(0, g_pause);
    ASSERT_EQ(0, g_up_calls);
    ASSERT_FALSE(g_idle.off);
    press(HAL_BTN_PLAY); // the next one works
    ASSERT_EQ(1, g_pause);
}

TEST(waking_with_a_scroll_swallows_it_too) {
    reset(false, false);
    g_ui_state.autolock = false;
    g_idle.off = true;
    scroll_at(1000);
    ASSERT_FALSE(g_idle.off);
    ASSERT_EQ(0, g_scrolls);
    scroll_at(1000);
    ASSERT_EQ(1, g_scrolls);
}

TEST(a_power_press_that_wakes_the_screen_does_not_sleep_it_again) {
    reset(false, false);
    g_ui_state.autolock = false;
    g_idle.off = true;
    ev_at(INPUT_POWER_DOWN, 1000); // wakes
    ASSERT_FALSE(g_idle.off);
    ev_at(INPUT_POWER_UP, 1150);
    ASSERT_FALSE(g_idle.off);
}

TEST(holding_power_two_seconds_toggles_hold_and_the_power_key_still_works_when_locked) {
    reset(false, false);
    ev_at(INPUT_POWER_DOWN, 1000);
    ev_at(INPUT_POWER_UP, 3100); // a late poll: the release still counts the hold
    ASSERT_TRUE(ui_frame_locked());
    ASSERT_FALSE(g_idle.off); // and it was not a short press
    ev_at(INPUT_POWER_DOWN, 5000); // locked: Power still reaches the Hold logic
    ev_at(INPUT_POWER_UP, 7100);
    ASSERT_FALSE(ui_frame_locked());
}

TEST(the_hardware_switch_sets_the_state_both_ways) {
    reset(false, false);
    input_event_t on = {.type = INPUT_HOLD_SWITCH, .steps = 1}, off = {.type = INPUT_HOLD_SWITCH, .steps = 0};
    ui_frame_input(&on, 1000);
    ASSERT_TRUE(ui_frame_locked());
    ui_frame_input(&on, 1000); // no change
    ASSERT_TRUE(ui_frame_locked());
    ui_frame_input(&off, 1000);
    ASSERT_FALSE(ui_frame_locked());
}

TEST(a_dimmed_screen_that_is_not_off_still_delivers_the_input) {
    reset(true, false);
    g_idle.dimmed = true;
    press(HAL_BTN_PLAY);
    ASSERT_EQ(1, g_pause); // the dim stage is unchanged (UI-5): only the off stage swallows the waking input
    ASSERT_FALSE(g_idle.dimmed);
}

TEST(right_and_left_skip_when_the_screen_leaves_them_alone_and_a_queue_plays) {
    reset(true, false);
    press(HAL_BTN_RIGHT);
    ASSERT_EQ(1, g_next);
    ASSERT_EQ(0, g_prev);
    press(HAL_BTN_LEFT);
    ASSERT_EQ(1, g_prev);
    ASSERT_EQ(2, g_btn_calls); // the screen was asked first, both times
}

TEST(a_screen_that_consumes_the_key_keeps_it) {
    reset(true, true);
    press(HAL_BTN_RIGHT);
    press(HAL_BTN_LEFT);
    press(HAL_BTN_PLAY);
    ASSERT_EQ(0, g_next);
    ASSERT_EQ(0, g_prev);
    ASSERT_EQ(0, g_pause);
    ASSERT_EQ(3, g_btn_calls);
}

TEST(no_queue_means_no_skip) {
    reset(false, false);
    press(HAL_BTN_RIGHT);
    press(HAL_BTN_LEFT);
    ASSERT_EQ(0, g_next + g_prev);
    reset(true, false);
    g_pi.state = PLAYER_IDLE; // a queue that is not playing
    press(HAL_BTN_RIGHT);
    g_pi.state = PLAYER_PLAYING;
    g_pi.src = PLAYER_SRC_TONE;
    press(HAL_BTN_LEFT);
    ASSERT_EQ(0, g_next + g_prev);
    reset(true, false);
    g_pi.state = PLAYER_PAUSED; // paused still counts: Next from a paused track is normal
    press(HAL_BTN_RIGHT);
    ASSERT_EQ(1, g_next);
}

TEST(play_pauses_on_every_screen_that_leaves_it_and_select_is_never_global) {
    reset(false, false);
    press(HAL_BTN_PLAY);
    ASSERT_EQ(1, g_pause);
    press(HAL_BTN_SELECT);
    ASSERT_EQ(0, g_next + g_prev);
    ASSERT_EQ(1, g_pause);
}

TEST(button_release_reaches_the_screen) {
    reset(true, false);
    input_event_t e = {.type = INPUT_BUTTON_UP, .button = HAL_BTN_RIGHT};
    ui_frame_input(&e, 1000);
    ASSERT_EQ(1, g_up_calls);
    ASSERT_EQ(HAL_BTN_RIGHT, g_last_up);
    ASSERT_EQ(0, g_next); // a release never skips by itself
}

TEST(a_press_the_screen_saw_still_gets_its_release_when_hold_comes_on_in_between) {
    reset(false, true);
    press(HAL_BTN_SELECT); // delivered
    input_event_t sw = {.type = INPUT_HOLD_SWITCH, .steps = 1};
    ui_frame_input(&sw, 1000);
    ASSERT_TRUE(ui_frame_locked());
    release(HAL_BTN_SELECT); // the screen must not be left with a key that never comes up
    ASSERT_EQ(1, g_up_calls);
    release(HAL_BTN_SELECT); // a second release (never pressed) is swallowed
    ASSERT_EQ(1, g_up_calls);
}

int main(void) {
    ui_stack_init(&g_stack);
    ui_idle_init(&g_idle, 1000);
    g_scr.on_button = scr_button;
    g_scr.on_button_up = scr_up;
    g_scr.on_scroll = scr_scroll;
    ASSERT_TRUE(ui_stack_push(&g_stack, &g_scr));
    RUN(right_and_left_skip_when_the_screen_leaves_them_alone_and_a_queue_plays);
    RUN(a_screen_that_consumes_the_key_keeps_it);
    RUN(no_queue_means_no_skip);
    RUN(play_pauses_on_every_screen_that_leaves_it_and_select_is_never_global);
    RUN(button_release_reaches_the_screen);
    RUN(locked_input_never_reaches_the_screen_or_the_player);
    RUN(unlocking_gives_the_input_back_and_a_stale_release_is_still_swallowed);
    RUN(swallowed_keys_give_no_click_but_the_lock_and_unlock_do);
    RUN(a_short_power_press_turns_the_screen_off_and_locks_with_auto_lock);
    RUN(the_waking_input_is_swallowed_and_the_screen_stays_locked);
    RUN(without_auto_lock_the_screen_wakes_unlocked_and_only_the_first_input_is_lost);
    RUN(waking_with_a_scroll_swallows_it_too);
    RUN(a_power_press_that_wakes_the_screen_does_not_sleep_it_again);
    RUN(holding_power_two_seconds_toggles_hold_and_the_power_key_still_works_when_locked);
    RUN(the_hardware_switch_sets_the_state_both_ways);
    RUN(a_dimmed_screen_that_is_not_off_still_delivers_the_input);
    RUN(a_press_the_screen_saw_still_gets_its_release_when_hold_comes_on_in_between);
    return RESULT();
}
