// SPDX-License-Identifier: MPL-2.0
#include <string.h>

#include "hal_input.h"
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

TEST(signal_bars_unknown_rssi) {
    const ui_signal_thr_t *w = &k_ui_signal_wifi;
    ASSERT_EQ(0, ui_signal_bars(0, w, 0, 3));   /* failed read, nothing shown yet: no bars, not "excellent" */
    ASSERT_EQ(0, ui_signal_bars(127, &k_ui_signal_bt, 0, 0));
    ASSERT_EQ(2, ui_signal_bars(0, w, 2, 3));   /* held while the read fails */
    ASSERT_EQ(4, ui_signal_bars(0, w, 4, 3));
    ASSERT_TRUE(ui_signal_word(0)[0] == '-');
}

TEST(autodim_choices) {
    ASSERT_EQ(10u, ui_autodim_seconds(0));
    ASSERT_EQ(30u, ui_autodim_seconds(UI_AUTODIM_DEFAULT_IDX));
    ASSERT_EQ(300u, ui_autodim_seconds(UI_AUTODIM_STEPS - 1u));
    ASSERT_EQ(0u, ui_autodim_seconds(UI_AUTODIM_STEPS));
    ASSERT_EQ(0, strcmp("1 min", ui_autodim_name(3)));
    ASSERT_EQ(0, strcmp("-", ui_autodim_name(200)));
}

TEST(idle_dims_at_exactly_the_timeout) {
    ui_idle_t t;
    ui_idle_init(&t, 5000);
    ASSERT_FALSE(ui_idle_tick(&t, 5000 + 29999, true, false, 30000));
    ASSERT_FALSE(t.dimmed);
    ASSERT_TRUE(ui_idle_tick(&t, 5000 + 30000, true, false, 30000));
    ASSERT_TRUE(t.dimmed);
    ASSERT_FALSE(ui_idle_tick(&t, 5000 + 90000, true, false, 30000)); /* a transition once, not per tick */
}

TEST(idle_clock_starts_at_init_not_zero) {
    ui_idle_t t;
    ui_idle_init(&t, 600000); /* a long boot */
    ASSERT_FALSE(ui_idle_tick(&t, 600000, true, false, 10000));
    ASSERT_FALSE(t.dimmed);
}

TEST(idle_activity_wakes) {
    ui_idle_t t;
    ui_idle_init(&t, 0);
    ASSERT_FALSE(ui_idle_activity(&t, 100)); /* not dimmed: no transition */
    ASSERT_TRUE(ui_idle_tick(&t, 100 + 15000, true, false, 15000));
    ASSERT_TRUE(ui_idle_activity(&t, 20000));
    ASSERT_FALSE(t.dimmed);
    ASSERT_FALSE(ui_idle_tick(&t, 20000 + 14999, true, false, 15000)); /* the clock restarted */
    ASSERT_TRUE(ui_idle_tick(&t, 20000 + 15000, true, false, 15000));
}

TEST(idle_disabled_never_dims_and_wakes) {
    ui_idle_t t;
    ui_idle_init(&t, 0);
    ASSERT_FALSE(ui_idle_tick(&t, 1000000, false, false, 10000));
    ASSERT_FALSE(t.dimmed);
    ASSERT_TRUE(ui_idle_tick(&t, 1000000, true, false, 10000));
    ASSERT_TRUE(ui_idle_tick(&t, 1000001, false, false, 10000)); /* switched off while dimmed (late settings load) */
    ASSERT_FALSE(t.dimmed);
}

TEST(idle_timer_change_reevaluates) {
    ui_idle_t t;
    ui_idle_init(&t, 0);
    ASSERT_TRUE(ui_idle_tick(&t, 20000, true, false, 10000));
    ASSERT_TRUE(ui_idle_tick(&t, 20000, true, false, 60000)); /* longer timeout: no longer idle */
    ASSERT_FALSE(t.dimmed);
    ASSERT_TRUE(ui_idle_tick(&t, 60000, true, false, 60000));
}

TEST(idle_future_activity_is_not_idle) {
    ui_idle_t t;
    ui_idle_init(&t, 1000);
    ui_idle_activity(&t, 1000 + 5 * 300u); /* the tour stamps events ahead of the tick clock */
    ASSERT_FALSE(ui_idle_tick(&t, 1000, true, false, 0));
    ASSERT_FALSE(t.dimmed);
    ASSERT_TRUE(ui_idle_tick(&t, 1000 + 5 * 300u, true, false, 0));
}

TEST(idle_survives_tick_wrap) {
    ui_idle_t t;
    ui_idle_init(&t, 0xFFFFFF00u);
    ASSERT_FALSE(ui_idle_tick(&t, 0x00000100u, true, false, 1000)); /* 512 ms across the wrap */
    ASSERT_TRUE(ui_idle_tick(&t, 0x00000100u + 488u, true, false, 1000));
    ui_idle_activity(&t, 0xFFFFFFF0u);
    ASSERT_FALSE(ui_idle_tick(&t, 0x100u, true, false, 1000));
}

TEST(idle_long_idle_stays_dimmed) {
    ui_idle_t t;
    ui_idle_init(&t, 1000);
    ASSERT_TRUE(ui_idle_tick(&t, 1000 + 10000, true, false, 10000));
    uint32_t day25 = 25u * 24u * 3600u * 1000u; /* past the int32 limit of about 24.8 days */
    ASSERT_FALSE(ui_idle_tick(&t, 1000 + day25, true, false, 10000));
    ASSERT_TRUE(t.dimmed);
}

TEST(idle_inhibit_holds_and_restarts) {
    ui_idle_t t;
    ui_idle_init(&t, 0);
    ASSERT_TRUE(ui_idle_tick(&t, 20000, true, false, 10000));
    ASSERT_TRUE(ui_idle_tick(&t, 20000, true, true, 10000)); /* slider opened: wakes */
    ASSERT_FALSE(ui_idle_tick(&t, 90000, true, true, 10000));
    ASSERT_FALSE(t.dimmed);
    ASSERT_FALSE(ui_idle_tick(&t, 90000 + 9999, true, false, 10000)); /* closed: counts from now */
    ASSERT_TRUE(ui_idle_tick(&t, 90000 + 10000, true, false, 10000));
}

TEST(idle_future_stamp_window) {
    ui_idle_t t;
    ui_idle_init(&t, 0);
    ui_idle_activity(&t, 59000);
    ASSERT_FALSE(ui_idle_tick(&t, 0, true, false, 0)); /* 59 s ahead: within the tolerance */
    ASSERT_FALSE(t.dimmed);
}

TEST(backlight_effective_level) {
    ASSERT_EQ(100, ui_backlight_effective(100, false));
    ASSERT_EQ(10, ui_backlight_effective(100, true));
    ASSERT_EQ(10, ui_backlight_effective(11, true));
    ASSERT_EQ(10, ui_backlight_effective(10, true));
    ASSERT_EQ(5, ui_backlight_effective(5, true)); /* never brightens */
    ASSERT_EQ(5, ui_backlight_effective(5, false));
}

/* ---- Global keys ------------------------------------------------------------------------------------ */

TEST(global_keys_route_by_queue_state) {
    ASSERT_EQ(UI_KEY_BACK, ui_global_key(HAL_BTN_MENU, false));
    ASSERT_EQ(UI_KEY_BACK, ui_global_key(HAL_BTN_MENU, true));
    ASSERT_EQ(UI_KEY_PAUSE, ui_global_key(HAL_BTN_PLAY, false));
    ASSERT_EQ(UI_KEY_NEXT, ui_global_key(HAL_BTN_RIGHT, true));
    ASSERT_EQ(UI_KEY_PREV, ui_global_key(HAL_BTN_LEFT, true));
    ASSERT_EQ(UI_KEY_NONE, ui_global_key(HAL_BTN_RIGHT, false)); /* nothing queued: Right / Left stay free */
    ASSERT_EQ(UI_KEY_NONE, ui_global_key(HAL_BTN_LEFT, false));
    ASSERT_EQ(UI_KEY_NONE, ui_global_key(HAL_BTN_SELECT, true)); /* Select belongs to the screens */
}

/* ---- Now Playing: wheel mode, scrub, hold ---------------------------------------------------------- */

static const ui_npc_track_t k_wav = {.seekable = true, .pos_frames = 44100u * 60u, .total_frames = 44100u * 240u, .rate = 44100u};
static const ui_npc_track_t k_mp3 = {.seekable = false, .pos_frames = 44100u * 60u, .total_frames = 44100u * 240u, .rate = 44100u};

TEST(select_cycles_volume_and_seek) {
    ui_npc_t c;
    ui_npc_init(&c);
    ASSERT_EQ(UI_WHEEL_VOLUME, c.mode);
    ASSERT_FALSE(ui_npc_scroll(&c, &k_wav, 1, 1000)); /* volume mode: the caller changes the volume */
    ASSERT_TRUE(ui_npc_select(&c, &k_wav, 1000));
    ASSERT_EQ(UI_WHEEL_SEEK, c.mode);
    ASSERT_TRUE(ui_npc_select(&c, &k_wav, 1500)); /* a second Select goes back */
    ASSERT_EQ(UI_WHEEL_VOLUME, c.mode);
}

TEST(select_on_a_track_that_cannot_seek_stays_on_volume) {
    ui_npc_t c;
    ui_npc_init(&c);
    ASSERT_FALSE(ui_npc_select(&c, &k_mp3, 1000)); /* the screen says "Seek not available" */
    ASSERT_EQ(UI_WHEEL_VOLUME, c.mode);
    ASSERT_FALSE(ui_npc_scroll(&c, &k_mp3, 3, 1100)); /* the wheel still sets the volume */
    ui_npc_track_t zero = k_wav;
    zero.total_frames = 0;
    ASSERT_FALSE(ui_npc_select(&c, &zero, 1200)); /* unknown length: no seek either */
}

TEST(seek_mode_scrubs_and_commits_after_the_wheel_rests) {
    ui_npc_t c;
    ui_npc_init(&c);
    ASSERT_TRUE(ui_npc_select(&c, &k_wav, 1000));
    ASSERT_EQ(4, ui_seek_step_s(240)); /* a 4 min track: 4 s per step */
    ASSERT_TRUE(ui_npc_scroll(&c, &k_wav, 3, 1100));
    ASSERT_TRUE(c.pending);
    ASSERT_EQ((60u + 12u) * 44100u, c.target);
    ASSERT_EQ(c.target, ui_npc_position(&c, &k_wav));
    ASSERT_TRUE(ui_npc_scroll(&c, &k_wav, -1, 1200)); /* the next step starts from the target, not the playing position */
    ASSERT_EQ((60u + 8u) * 44100u, c.target);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, 1200 + UI_SEEK_COMMIT_MS - 1)); /* still scrubbing */
    ASSERT_EQ(UI_NPC_COMMIT, ui_npc_tick(&c, &k_wav, 1200 + UI_SEEK_COMMIT_MS));
    ui_npc_committed(&c);
    ASSERT_FALSE(c.pending);
    ASSERT_EQ(k_wav.pos_frames, ui_npc_position(&c, &k_wav)); /* back to the playing position */
    ASSERT_EQ(UI_WHEEL_SEEK, c.mode);                          /* still seek mode: more scrubbing is possible */
}

TEST(scrub_clamps_to_the_track) {
    ui_npc_t c;
    ui_npc_init(&c);
    ASSERT_TRUE(ui_npc_select(&c, &k_wav, 0));
    ASSERT_TRUE(ui_npc_scroll(&c, &k_wav, -1000, 10));
    ASSERT_EQ(0, c.target);
    ASSERT_TRUE(ui_npc_scroll(&c, &k_wav, 100000, 20));
    ASSERT_EQ(k_wav.total_frames, c.target); /* the end itself: the player treats it as the end of the track */
    ASSERT_EQ(1, ui_seek_step_s(10));
    ASSERT_EQ(30, ui_seek_step_s(3600 * 10));
}

TEST(seek_mode_drops_back_to_volume_after_three_seconds_idle) {
    ui_npc_t c;
    ui_npc_init(&c);
    ASSERT_EQ(3000, UI_SEEK_IDLE_MS);
    ASSERT_TRUE(ui_npc_select(&c, &k_wav, 5000));
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, 5000 + UI_SEEK_IDLE_MS - 1));
    ASSERT_EQ(UI_WHEEL_SEEK, c.mode);
    ASSERT_EQ(UI_NPC_TO_VOLUME, ui_npc_tick(&c, &k_wav, 5000 + UI_SEEK_IDLE_MS));
    ASSERT_EQ(UI_WHEEL_VOLUME, c.mode);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, 20000)); /* once */
}

TEST(wheel_input_restarts_the_idle_clock) {
    ui_npc_t c;
    ui_npc_init(&c);
    ASSERT_TRUE(ui_npc_select(&c, &k_wav, 0));
    ASSERT_TRUE(ui_npc_scroll(&c, &k_wav, 1, 2500));
    ASSERT_EQ(UI_NPC_COMMIT, ui_npc_tick(&c, &k_wav, 2500 + UI_SEEK_COMMIT_MS));
    ui_npc_committed(&c);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, 2500 + UI_SEEK_IDLE_MS - 1)); /* 3 s after the LAST turn, not after Select */
    ASSERT_EQ(UI_WHEEL_SEEK, c.mode);
    ASSERT_EQ(UI_NPC_TO_VOLUME, ui_npc_tick(&c, &k_wav, 2500 + UI_SEEK_IDLE_MS));
}

TEST(seek_mode_ends_when_the_track_cannot_seek_any_more) {
    ui_npc_t c;
    ui_npc_init(&c);
    ASSERT_TRUE(ui_npc_select(&c, &k_wav, 0));
    ASSERT_TRUE(ui_npc_scroll(&c, &k_wav, 1, 100));
    ASSERT_EQ(UI_NPC_TO_VOLUME, ui_npc_tick(&c, &k_mp3, 200)); /* a gapless handoff to an MP3: the scrub is dropped */
    ASSERT_FALSE(c.pending);
    ASSERT_EQ(UI_WHEEL_VOLUME, c.mode);
}

TEST(short_press_skips_on_release) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_key_down(&c, 1, 1000);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, 1000 + UI_HOLD_MS - 1));
    ASSERT_EQ(UI_NPC_SKIP, ui_npc_key_up(&c, 1, 1000 + UI_HOLD_MS - 1));
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, 1, 1900)); /* a release without a press here does nothing */
    ui_npc_key_down(&c, -1, 3000);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, 1, 3010)); /* the other key */
    ASSERT_EQ(UI_NPC_SKIP, ui_npc_key_up(&c, -1, 3020));
}

TEST(hold_right_fast_forwards_and_commits_on_release) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_key_down(&c, 1, 1000);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, 1000 + UI_HOLD_MS)); /* the hold starts: no movement yet */
    ASSERT_TRUE(c.holding);
    ASSERT_FALSE(c.pending);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, 1000 + UI_HOLD_MS + 1000)); /* a second held: 10 s further, nothing applied meanwhile */
    ASSERT_TRUE(c.pending);
    ASSERT_EQ((60u + UI_FF_SPEED) * 44100u, c.target);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, 1000 + UI_HOLD_MS + 5000));
    ASSERT_EQ((60u + 5u * UI_FF_SPEED) * 44100u, c.target);
    ASSERT_EQ(UI_NPC_COMMIT, ui_npc_key_up(&c, 1, 1000 + UI_HOLD_MS + 5000)); /* not a skip */
    ASSERT_FALSE(c.holding);
    ASSERT_EQ(0, c.hold_dir);
    ui_npc_committed(&c);
}

TEST(hold_left_rewinds_and_stops_at_the_start) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_key_down(&c, -1, 0);
    (void)ui_npc_tick(&c, &k_wav, UI_HOLD_MS);
    (void)ui_npc_tick(&c, &k_wav, UI_HOLD_MS + 2000);
    ASSERT_EQ((60u - 2u * UI_FF_SPEED) * 44100u, c.target);
    (void)ui_npc_tick(&c, &k_wav, UI_HOLD_MS + 60000);
    ASSERT_EQ(0, c.target);
    ASSERT_EQ(UI_NPC_COMMIT, ui_npc_key_up(&c, -1, UI_HOLD_MS + 60001));
}

TEST(hold_on_a_track_that_cannot_seek_says_so_and_does_not_skip) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_key_down(&c, 1, 0);
    ASSERT_EQ(UI_NPC_UNAVAILABLE, ui_npc_tick(&c, &k_mp3, UI_HOLD_MS));
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_mp3, UI_HOLD_MS + 500)); /* once */
    ASSERT_FALSE(c.pending);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, 1, UI_HOLD_MS + 600)); /* neither a skip nor a seek */
}

TEST(seek_timers_survive_the_tick_wrap) {
    ui_npc_t c;
    ui_npc_init(&c);
    uint32_t t0 = 0xFFFFFF00u;
    ASSERT_TRUE(ui_npc_select(&c, &k_wav, t0));
    ASSERT_TRUE(ui_npc_scroll(&c, &k_wav, 1, t0 + 10u));
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, t0 + 10u + UI_SEEK_COMMIT_MS - 1u));
    ASSERT_EQ(UI_NPC_COMMIT, ui_npc_tick(&c, &k_wav, t0 + 10u + UI_SEEK_COMMIT_MS)); /* across 2^32 */
}

int main(void) {
    RUN(signal_bars_unknown_rssi);
    RUN(signal_bars_thresholds);
    RUN(signal_bars_hysteresis);
    RUN(nav_clamps_and_window_follows);
    RUN(nav_short_and_empty_lists);
    RUN(nav_set_centres);
    RUN(accel_slow_is_one_step_per_four_counts);
    RUN(accel_fast_multiplies);
    RUN(accel_reversal_drops_residue);
    RUN(stack_push_pop);
    RUN(autodim_choices);
    RUN(idle_dims_at_exactly_the_timeout);
    RUN(idle_clock_starts_at_init_not_zero);
    RUN(idle_activity_wakes);
    RUN(idle_disabled_never_dims_and_wakes);
    RUN(idle_timer_change_reevaluates);
    RUN(idle_future_activity_is_not_idle);
    RUN(idle_survives_tick_wrap);
    RUN(idle_long_idle_stays_dimmed);
    RUN(idle_inhibit_holds_and_restarts);
    RUN(idle_future_stamp_window);
    RUN(backlight_effective_level);
    RUN(global_keys_route_by_queue_state);
    RUN(select_cycles_volume_and_seek);
    RUN(select_on_a_track_that_cannot_seek_stays_on_volume);
    RUN(seek_mode_scrubs_and_commits_after_the_wheel_rests);
    RUN(scrub_clamps_to_the_track);
    RUN(seek_mode_drops_back_to_volume_after_three_seconds_idle);
    RUN(wheel_input_restarts_the_idle_clock);
    RUN(seek_mode_ends_when_the_track_cannot_seek_any_more);
    RUN(short_press_skips_on_release);
    RUN(hold_right_fast_forwards_and_commits_on_release);
    RUN(hold_left_rewinds_and_stops_at_the_start);
    RUN(hold_on_a_track_that_cannot_seek_says_so_and_does_not_skip);
    RUN(seek_timers_survive_the_tick_wrap);
    return RESULT();
}
