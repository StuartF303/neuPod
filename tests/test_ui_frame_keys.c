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
void player_pause_toggle(void) { g_pause++; }
void player_next(void) { g_next++; }
void player_prev(void) { g_prev++; }

// ---- fakes: everything else ui_frame.c links against ---------------------------------------------------------------------------
uint32_t os_tick_ms(void) { return 1000; }
bool battery_store_get(hal_battery_status_t *b) { (void)b; return false; }
void bt_service_tick(void) {}
bool caps_available(cap_id_t c) { (void)c; return false; }
bool caps_all_resolved(void) { return true; }
hal_wifi_status hal_wifi_state(void) { return HAL_WIFI_DOWN; }
void wifi_get_info(wifi_info_t *i) { memset(i, 0, sizeof *i); }
void rt_haptic_settings_loaded(void) {}
void rt_haptic_click(void) {}
void rt_piezo_click(void) {}
uint32_t settings_generation(void) { return 0; }
bool settings_get_u8(uint32_t tag, uint8_t *out) { (void)tag; (void)out; return false; }
void settings_set_u8(uint32_t tag, uint8_t v) { (void)tag; (void)v; }
void hal_display_backlight(uint8_t level) { (void)level; }
void ui_log_screen(const char *t, const char *w, const char *n) { (void)t; (void)w; (void)n; }

// ---- a fake screen -------------------------------------------------------------------------------------------------------------
static ui_screen_t g_scr;
static bool g_consume; // what on_button answers
static int g_btn_calls, g_up_calls;
static uint8_t g_last_up;
static bool scr_button(ui_screen_t *s, uint8_t b) { (void)s; (void)b; g_btn_calls++; return g_consume; }
static void scr_up(ui_screen_t *s, uint8_t b) { (void)s; g_up_calls++; g_last_up = b; }

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
    g_pause = g_next = g_prev = g_btn_calls = g_up_calls = 0;
    g_consume = consume;
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

int main(void) {
    ui_stack_init(&g_stack);
    ui_idle_init(&g_idle, 1000);
    g_scr.on_button = scr_button;
    g_scr.on_button_up = scr_up;
    ASSERT_TRUE(ui_stack_push(&g_stack, &g_scr));
    RUN(right_and_left_skip_when_the_screen_leaves_them_alone_and_a_queue_plays);
    RUN(a_screen_that_consumes_the_key_keeps_it);
    RUN(no_queue_means_no_skip);
    RUN(play_pauses_on_every_screen_that_leaves_it_and_select_is_never_global);
    RUN(button_release_reaches_the_screen);
    return RESULT();
}
