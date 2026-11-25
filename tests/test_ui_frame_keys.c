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
uint32_t player_volume_blocked(void) { return 0; }
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
static uint32_t g_set_gen;
static bool g_set_have; // is a "Sleep after" item stored, and what it holds
static uint8_t g_set_val, g_set_saved;
static bool g_lh_have; // is a "Lock hold" item stored, and what it holds
static uint8_t g_lh_val, g_lh_saved;
uint32_t settings_generation(void) { return g_set_gen; }
bool settings_get_u8(uint32_t tag, uint8_t *out) {
    if (tag == SETTING_SLEEP_IDX && g_set_have) {
        *out = g_set_val;
        return true;
    }
    if (tag == SETTING_LOCK_HOLD && g_lh_have) {
        *out = g_lh_val;
        return true;
    }
    return false;
}
void settings_set_u8(uint32_t tag, uint8_t v) {
    if (tag == SETTING_SLEEP_IDX) {
        g_set_saved = v;
    }
    if (tag == SETTING_LOCK_HOLD) {
        g_lh_saved = v;
    }
}
void hal_display_backlight(uint8_t level) { g_backlight = level; }
void ui_log_screen(const char *t, const char *w, const char *n) { (void)t; (void)w; (void)n; }


// ---- fakes: power (docs/adr/0021-power-sleep.md). g_pw_log records the order of the sleep sequence: P prepare, S display sleep, B block, D display wake, W wake.
static power_action_t g_pw_action;
static bool g_pw_prepare_ok = true, g_pw_can_sleep = true, g_pw_poweroff_ok = true;
static hal_status_t g_pw_status = HAL_OK;
static hal_wake_reason_t g_pw_why = HAL_WAKE_KEY;
static int g_pw_poweroff, g_pw_restart;
static char g_pw_log[16];
static void pw_log(char c) {
    size_t n = strlen(g_pw_log);
    if (n < sizeof g_pw_log - 1) {
        g_pw_log[n] = c;
    }
}
power_action_t power_service_poll(uint32_t now_ms, uint32_t last_ms, uint32_t sleep_after_s) { (void)now_ms; (void)last_ms; (void)sleep_after_s; return g_pw_action; }
bool power_service_sleep_prepare(void) { pw_log('P'); return g_pw_prepare_ok; }
hal_status_t power_service_sleep_block(hal_wake_reason_t *why) { pw_log('B'); *why = g_pw_why; return g_pw_status; }
void power_service_wake(uint32_t now_ms) { (void)now_ms; pw_log('W'); }
bool power_service_poweroff(void) { g_pw_poweroff++; return g_pw_poweroff_ok; }
void power_service_restart(uint32_t now_ms) { (void)now_ms; g_pw_restart++; }
static int g_pw_results, g_pw_queued;
static bool g_pw_external;
void power_service_sleep_result(hal_status_t st, uint32_t now_ms) { (void)st; (void)now_ms; g_pw_results++; }
os_queue_t *rt_input_queue(void) { return NULL; }
bool os_queue_pending(os_queue_t *q) { (void)q; return g_pw_queued != 0; }
bool hal_power_external(void) { return g_pw_external; }
bool power_service_can_act(void) { return true; }
bool hal_power_can_sleep(void) { return g_pw_can_sleep; }
void hal_display_sleep(bool sleep) { pw_log(sleep ? 'S' : 'D'); }

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
    g_ui_state.lock_hold_s = 2; /* the shipped default is Off; most tests are about the 2 s gesture */
    g_hold.toggle_s = 2;
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

static void power_reset(void);

TEST(with_lock_hold_off_the_screen_going_off_never_locks_even_with_auto_lock_on) {
    reset(false, false);
    g_ui_state.lock_hold_s = 0;
    ui_frame_lock_hold_changed();
    ev_at(INPUT_POWER_DOWN, 1000);
    ev_at(INPUT_POWER_UP, 1200);
    ASSERT_TRUE(g_idle.off);
    ASSERT_FALSE(ui_frame_locked());
    power_reset();
    g_ui_state.lock_hold_s = 0;
    ui_frame_lock_hold_changed();
    g_pw_action = POWER_ACT_SLEEP;
    power_tick(1000);
    ASSERT_TRUE(g_idle.off);
    ASSERT_FALSE(ui_frame_locked());
    g_ui_state.lock_hold_s = 2; // back on: the same short press locks
    ui_frame_lock_hold_changed();
    reset(false, false);
    ev_at(INPUT_POWER_DOWN, 1000);
    ev_at(INPUT_POWER_UP, 1200);
    ASSERT_TRUE(ui_frame_locked());
}

TEST(with_lock_hold_off_a_long_power_hold_does_nothing_but_the_switch_and_console_key_still_lock) {
    reset(false, false);
    g_ui_state.lock_hold_s = 0;
    ui_frame_lock_hold_changed();
    ev_at(INPUT_POWER_DOWN, 1000);
    ev_at(INPUT_POWER_UP, 4100); // late poll: 3.1 s
    ASSERT_FALSE(ui_frame_locked());
    ASSERT_FALSE(g_idle.off); // and it was not a short press
    ev_at(INPUT_HOLD_TOGGLE, 5000); // the console key is a dev override
    ASSERT_TRUE(ui_frame_locked());
    input_event_t off = {.type = INPUT_HOLD_SWITCH, .steps = 0}, on = {.type = INPUT_HOLD_SWITCH, .steps = 1};
    ui_frame_input(&off, 5100);
    ASSERT_FALSE(ui_frame_locked());
    ui_frame_input(&on, 5200);
    ASSERT_TRUE(ui_frame_locked());
    ev_at(INPUT_POWER_DOWN, 6000); // Power cannot unlock now
    ev_at(INPUT_POWER_UP, 9100);
    ASSERT_TRUE(ui_frame_locked());
}

TEST(setting_lock_hold_off_while_locked_unlocks_and_changing_the_time_moves_the_gesture) {
    reset(false, false);
    ev_at(INPUT_HOLD_TOGGLE, 1000);
    ASSERT_TRUE(ui_frame_locked());
    g_ui_state.lock_hold_s = 3;
    ui_frame_lock_hold_changed();
    ASSERT_TRUE(ui_frame_locked());
    ev_at(INPUT_POWER_DOWN, 2000);
    ev_at(INPUT_POWER_UP, 4500); // 2.5 s: not yet for 3 s
    ASSERT_TRUE(ui_frame_locked());
    ev_at(INPUT_POWER_DOWN, 6000);
    ev_at(INPUT_POWER_UP, 9100);
    ASSERT_FALSE(ui_frame_locked());
    ev_at(INPUT_HOLD_TOGGLE, 10000);
    g_ui_state.lock_hold_s = 0;
    ui_frame_lock_hold_changed();
    ASSERT_FALSE(ui_frame_locked());
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


// ---- Sleep and the empty battery (ADR-0021): the screen's part of the sequence ----
static void power_reset(void) {
    reset(true, false);
    g_pi.state = PLAYER_PAUSED;
    g_pw_action = POWER_ACT_NONE;
    g_pw_prepare_ok = g_pw_can_sleep = g_pw_poweroff_ok = true;
    g_pw_status = HAL_OK;
    g_pw_why = HAL_WAKE_KEY;
    g_pw_poweroff = g_pw_restart = g_pw_results = g_pw_queued = 0;
    g_pw_external = false;
    memset(g_pw_log, 0, sizeof g_pw_log);
    g_wake_pending = g_crit_shown = g_crit_done = false;
    g_backlight = 100;
}

TEST(sleep_runs_in_order_and_the_key_that_wakes_it_only_lights_the_screen) {
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    power_tick(1000);
    ASSERT_TRUE(strcmp(g_pw_log, "PSBDW") == 0); // prepare, panel to sleep, block, panel awake, wake: the screen went off before the block
    ASSERT_TRUE(g_idle.off);                      // and stays off after it: nothing pressed yet
    ASSERT_EQ(0, g_backlight);
    ASSERT_TRUE(ui_frame_locked());               // auto-lock, as when the screen goes off by itself
    ASSERT_TRUE(g_wake_pending);
    g_pw_action = POWER_ACT_NONE;
    press(HAL_BTN_PLAY); // the input that woke the system arrives
    ASSERT_FALSE(g_idle.off);
    ASSERT_TRUE(g_backlight > 0);
    ASSERT_FALSE(g_wake_pending);
    ASSERT_EQ(0, g_pause + g_btn_calls); // not delivered
    ASSERT_TRUE(ui_frame_locked());      // Hold as the screen-off left it: only a hold of Power unlocks
    ASSERT_EQ(0, g_pw_poweroff);
}

TEST(a_key_wake_whose_input_never_arrives_lights_the_screen_anyway) {
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    power_tick(1000);
    g_pw_action = POWER_ACT_NONE;
    power_tick(1000 + WAKE_INPUT_WAIT_MS - 1);
    ASSERT_TRUE(g_idle.off);
    power_tick(1000 + WAKE_INPUT_WAIT_MS);
    ASSERT_FALSE(g_idle.off);
    ASSERT_TRUE(g_backlight > 0);
}

TEST(a_charger_wake_lights_the_screen_at_once_and_a_failed_sleep_does_too) {
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_why = HAL_WAKE_EXTERNAL_POWER;
    power_tick(1000);
    ASSERT_FALSE(g_idle.off);
    ASSERT_FALSE(g_wake_pending);
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_status = HAL_EBUSY; // something held the system awake: it never slept
    power_tick(1000);
    ASSERT_FALSE(g_idle.off);
    ASSERT_TRUE(g_backlight > 0);
}

TEST(waking_by_the_timer_takes_the_normal_power_off_path) {
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_why = HAL_WAKE_TIMER;
    power_tick(1000);
    ASSERT_EQ(1, g_pw_poweroff);
    ASSERT_FALSE(g_wake_pending);
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_why = HAL_WAKE_TIMER;
    g_pw_poweroff_ok = false; // a platform that cannot switch off carries on awake
    power_tick(1000);
    ASSERT_FALSE(g_idle.off);
}

TEST(a_refused_preparation_leaves_the_screen_alone_and_restarts_the_count) {
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_prepare_ok = false; // playing again, or the card stayed busy
    power_tick(1000);
    ASSERT_TRUE(strcmp(g_pw_log, "P") == 0);
    ASSERT_FALSE(g_idle.off);
    ASSERT_EQ(100, g_backlight);
    ASSERT_EQ(1, g_pw_restart);
}

TEST(no_suspend_on_this_platform_switches_off_after_the_idle_time_instead) {
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_can_sleep = false;
    power_tick(1000);
    ASSERT_EQ(1, g_pw_poweroff);
    ASSERT_FALSE(g_idle.off); // the screen is left as it was: the player is going away
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_can_sleep = false;
    g_pw_poweroff_ok = false; // cannot switch off either: nothing happens, and the count starts again
    power_tick(1000);
    ASSERT_EQ(1, g_pw_restart);
}

TEST(an_empty_battery_shows_the_notice_lights_the_screen_and_powers_off_once) {
    power_reset();
    g_idle.off = g_idle.forced_off = true;
    g_backlight = 0;
    g_pw_action = POWER_ACT_POWEROFF_CRITICAL;
    power_tick(1000);
    ASSERT_FALSE(g_idle.off); // the notice has to be seen
    ASSERT_TRUE(g_backlight > 0);
    ASSERT_TRUE(g_crit_shown);
    ASSERT_EQ(0, g_pw_poweroff);
    power_tick(1000 + CRIT_NOTICE_MS - 1);
    ASSERT_EQ(0, g_pw_poweroff);
    power_tick(1000 + CRIT_NOTICE_MS);
    ASSERT_EQ(1, g_pw_poweroff);
    power_tick(1000 + CRIT_NOTICE_MS + 500);
    ASSERT_EQ(1, g_pw_poweroff); // asked once
    g_pw_action = POWER_ACT_NONE; // a charger came: the rule resets and a later empty battery shows its notice again
    power_tick(1000 + CRIT_NOTICE_MS + 1000);
    ASSERT_FALSE(g_crit_shown);
    ASSERT_FALSE(g_crit_done);
}


TEST(sleep_after_defaults_to_30_minutes_loads_from_the_card_and_is_saved) {
    ASSERT_EQ(POWER_SLEEP_DEFAULT_IDX, g_ui_state.sleep_idx);
    ASSERT_EQ(1800, power_sleep_seconds(g_ui_state.sleep_idx));
    g_set_have = false; // a settings file from an older firmware has no such item: the default stays
    g_set_gen = 1;
    sync_from_settings();
    ASSERT_EQ(POWER_SLEEP_DEFAULT_IDX, g_ui_state.sleep_idx);
    g_set_have = true; // the card says 60 min
    g_set_val = 3;
    g_set_gen = 2;
    sync_from_settings();
    ASSERT_EQ(3, g_ui_state.sleep_idx);
    ui_state_persist();
    ASSERT_EQ(3, g_set_saved);
    g_set_val = 9; // a value only a newer firmware knows: ignored, the current choice stays
    g_set_gen = 3;
    sync_from_settings();
    ASSERT_EQ(3, g_ui_state.sleep_idx);
    g_ui_state.sleep_idx = POWER_SLEEP_DEFAULT_IDX;
    g_set_have = false;
}

TEST(lock_hold_loads_from_the_card_is_saved_and_anything_unknown_is_off) {
    reset(false, false);
    g_ui_state.lock_hold_s = 0; // the shipped default
    g_hold.toggle_s = 0;
    g_lh_have = false; // an older firmware's file has only Auto-lock: Lock hold stays Off, Auto-lock keeps its value
    g_ui_state.autolock = false;
    g_set_gen += 10;
    sync_from_settings();
    ASSERT_EQ(0, g_ui_state.lock_hold_s);
    ASSERT_FALSE(g_ui_state.autolock);
    const uint8_t good[] = {0, 1, 2, 3, 5};
    for (unsigned i = 0; i < sizeof good; i++) {
        g_lh_have = true;
        g_lh_val = good[i];
        g_set_gen++;
        sync_from_settings();
        ASSERT_EQ(good[i], g_ui_state.lock_hold_s);
        ASSERT_EQ(good[i], g_hold.toggle_s);
        ui_state_persist();
        ASSERT_EQ(good[i], g_lh_saved);
    }
    const uint8_t bad[] = {4, 6, 9, 10, 255};
    for (unsigned i = 0; i < sizeof bad; i++) {
        g_lh_val = 3;
        g_set_gen++;
        sync_from_settings();
        g_lh_val = bad[i];
        g_set_gen++;
        sync_from_settings();
        ASSERT_EQ(0, g_ui_state.lock_hold_s);
        ASSERT_EQ(0, g_hold.toggle_s);
    }
    // a lock that is already on (the switch at boot) survives the settings load
    ui_hold_set(&g_hold, true);
    g_lh_val = 0;
    g_set_gen++;
    sync_from_settings();
    ASSERT_TRUE(ui_frame_locked());
    g_lh_have = false;
    g_ui_state.autolock = true;
}

TEST(a_failed_sleep_puts_the_screen_back_as_it_was) {
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_status = HAL_EBUSY; // the screen was on: it is lit again and Hold is as it was (unlocked)
    power_tick(1000);
    ASSERT_EQ(1, g_pw_results);
    ASSERT_FALSE(g_idle.off);
    ASSERT_FALSE(ui_frame_locked());
    power_reset();
    g_idle.off = g_idle.forced_off = true; // the screen was already off and locked: a failed sleep must not light it
    g_backlight = 0;
    ui_hold_set(&g_hold, true);
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_status = HAL_ERR;
    power_tick(1000);
    ASSERT_TRUE(g_idle.off);
    ASSERT_EQ(0, g_backlight);
    ASSERT_TRUE(ui_frame_locked());
    ASSERT_FALSE(g_wake_pending);
}

TEST(input_or_a_charger_during_the_preparation_aborts_the_sleep_before_the_screen_goes) {
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_queued = 1;
    power_tick(1000);
    ASSERT_TRUE(strcmp(g_pw_log, "PW") == 0); // prepared, then woken again at once: no display sleep, no block
    ASSERT_FALSE(g_idle.off);
    ASSERT_EQ(100, g_backlight);
    ASSERT_EQ(1, g_pw_restart);
    power_reset();
    g_pw_action = POWER_ACT_SLEEP;
    g_pw_external = true;
    power_tick(1000);
    ASSERT_TRUE(strcmp(g_pw_log, "PW") == 0);
    ASSERT_FALSE(g_idle.off);
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
    RUN(with_lock_hold_off_the_screen_going_off_never_locks_even_with_auto_lock_on);
    RUN(with_lock_hold_off_a_long_power_hold_does_nothing_but_the_switch_and_console_key_still_lock);
    RUN(setting_lock_hold_off_while_locked_unlocks_and_changing_the_time_moves_the_gesture);
    RUN(lock_hold_loads_from_the_card_is_saved_and_anything_unknown_is_off);
    RUN(the_waking_input_is_swallowed_and_the_screen_stays_locked);
    RUN(without_auto_lock_the_screen_wakes_unlocked_and_only_the_first_input_is_lost);
    RUN(waking_with_a_scroll_swallows_it_too);
    RUN(a_power_press_that_wakes_the_screen_does_not_sleep_it_again);
    RUN(holding_power_two_seconds_toggles_hold_and_the_power_key_still_works_when_locked);
    RUN(the_hardware_switch_sets_the_state_both_ways);
    RUN(a_dimmed_screen_that_is_not_off_still_delivers_the_input);
    RUN(a_press_the_screen_saw_still_gets_its_release_when_hold_comes_on_in_between);
    RUN(sleep_after_defaults_to_30_minutes_loads_from_the_card_and_is_saved);
    RUN(sleep_runs_in_order_and_the_key_that_wakes_it_only_lights_the_screen);
    RUN(a_failed_sleep_puts_the_screen_back_as_it_was);
    RUN(input_or_a_charger_during_the_preparation_aborts_the_sleep_before_the_screen_goes);
    RUN(a_key_wake_whose_input_never_arrives_lights_the_screen_anyway);
    RUN(a_charger_wake_lights_the_screen_at_once_and_a_failed_sleep_does_too);
    RUN(waking_by_the_timer_takes_the_normal_power_off_path);
    RUN(a_refused_preparation_leaves_the_screen_alone_and_restarts_the_count);
    RUN(no_suspend_on_this_platform_switches_off_after_the_idle_time_instead);
    RUN(an_empty_battery_shows_the_notice_lights_the_screen_and_powers_off_once);
    return RESULT();
}
