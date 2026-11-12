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
    ui_npc_key_down(&c, 1, &k_wav, 1000);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, 1000 + UI_HOLD_MS - 1));
    ASSERT_EQ(UI_NPC_SKIP, ui_npc_key_up(&c, 1, 1000 + UI_HOLD_MS - 1));
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, 1, 1900)); /* a release without a press here does nothing */
    ui_npc_key_down(&c, -1, &k_wav, 3000);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, 1, 3010)); /* the other key */
    ASSERT_EQ(UI_NPC_SKIP, ui_npc_key_up(&c, -1, 3020));
}

TEST(hold_right_fast_forwards_and_commits_on_release) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_key_down(&c, 1, &k_wav, 1000);
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
    ui_npc_key_down(&c, -1, &k_wav, 0);
    (void)ui_npc_tick(&c, &k_wav, UI_HOLD_MS);
    (void)ui_npc_tick(&c, &k_wav, UI_HOLD_MS + 2000);
    ASSERT_EQ((60u - 2u * UI_FF_SPEED) * 44100u, c.target);
    ASSERT_EQ(UI_NPC_COMMIT, ui_npc_tick(&c, &k_wav, UI_HOLD_MS + 60000)); /* the start of the track: applied, the hold is over */
    ASSERT_EQ(0, c.target);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, -1, UI_HOLD_MS + 60001));
}

TEST(hold_on_a_track_that_cannot_seek_says_so_and_does_not_skip) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_key_down(&c, 1, &k_wav, 0);
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

static ui_npc_track_t with_id(ui_npc_track_t t, uint32_t id) {
    t.id = id;
    return t;
}

TEST(a_hold_through_a_track_change_does_not_seek_or_skip_the_new_track) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_track_t a = with_id(k_wav, 1), b = with_id(k_wav, 2);
    ui_npc_key_down(&c, 1, &a, 1000);
    (void)ui_npc_tick(&c, &a, 1000 + UI_HOLD_MS);
    (void)ui_npc_tick(&c, &a, 1000 + UI_HOLD_MS + 2000);
    ASSERT_TRUE(c.pending);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &b, 1000 + UI_HOLD_MS + 2100)); /* the gapless handoff happened */
    ASSERT_FALSE(c.pending);
    ASSERT_FALSE(c.holding);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, 1, 1000 + UI_HOLD_MS + 2200)); /* neither a commit nor a skip */
    ASSERT_FALSE(ui_npc_commit_valid(&c, &b));
}

TEST(a_scrub_made_on_another_track_is_not_applied) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_track_t a = with_id(k_wav, 1), b = with_id(k_wav, 2);
    ASSERT_TRUE(ui_npc_select(&c, &a, 0));
    ASSERT_TRUE(ui_npc_scroll(&c, &a, 5, 10));
    ASSERT_TRUE(ui_npc_commit_valid(&c, &a));
    ASSERT_FALSE(ui_npc_commit_valid(&c, &b)); /* what np_commit asks just before it seeks */
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &b, 20)); /* the tick drops it before the commit delay */
    ASSERT_FALSE(c.pending);
    ASSERT_TRUE(ui_npc_scroll(&c, &b, 1, 30)); /* a new scrub starts from the new track's position, not the old target */
    ASSERT_EQ(b.pos_frames + 4u * 44100u, c.target);
    ASSERT_EQ(2, c.id);
}

TEST(another_key_or_the_wheel_drops_a_hold_whose_release_may_be_lost) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_key_down(&c, 1, &k_wav, 0);
    (void)ui_npc_tick(&c, &k_wav, UI_HOLD_MS);
    (void)ui_npc_tick(&c, &k_wav, UI_HOLD_MS + 1000);
    ASSERT_TRUE(c.pending);
    ui_npc_cancel_hold(&c); /* Select / Play / the wheel / the screen going away */
    ASSERT_FALSE(c.pending); /* a fast-forward that was never released is not applied */
    ASSERT_EQ(0, c.hold_dir);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, UI_HOLD_MS + 5000));
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, 1, UI_HOLD_MS + 5001));
    ui_npc_key_down(&c, 1, &k_wav, 9000); /* a second key while one is down replaces it */
    ui_npc_key_down(&c, -1, &k_wav, 9100);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, 1, 9200));
    ASSERT_EQ(UI_NPC_SKIP, ui_npc_key_up(&c, -1, 9250));
}

TEST(a_hold_whose_release_never_comes_ends_itself) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_track_t big = k_wav; /* a 10 h track: the end is not reached before the watchdog */
    big.total_frames = 44100u * 36000u;
    ui_npc_key_down(&c, 1, &big, 0);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &big, UI_HOLD_MS));
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &big, UI_HOLD_MAX_MS - 1));
    ASSERT_TRUE(c.holding);
    ASSERT_EQ(UI_NPC_COMMIT, ui_npc_tick(&c, &big, UI_HOLD_MAX_MS)); /* what was reached is applied */
    ASSERT_FALSE(c.holding);
    ASSERT_EQ(0, c.hold_dir);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, 1, UI_HOLD_MAX_MS + 100)); /* the late release does nothing */
}

TEST(a_hold_that_reaches_the_end_of_the_track_applies_it_at_once) {
    ui_npc_t c;
    ui_npc_init(&c);
    ui_npc_key_down(&c, 1, &k_wav, 0); /* 180 s left at 10x: 18 s of holding */
    (void)ui_npc_tick(&c, &k_wav, UI_HOLD_MS);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_tick(&c, &k_wav, UI_HOLD_MS + 17000));
    ASSERT_EQ(UI_NPC_COMMIT, ui_npc_tick(&c, &k_wav, UI_HOLD_MS + 19000)); /* the end: the player takes it as the end of the track */
    ASSERT_EQ(k_wav.total_frames, c.target);
    ASSERT_FALSE(c.holding);
    ASSERT_EQ(UI_NPC_NONE, ui_npc_key_up(&c, 1, UI_HOLD_MS + 19500));
}

static void disp(char *o, size_t n, const char *raw) { ui_text_display(o, n, raw); }

TEST(text_display_keeps_what_the_fonts_have) {
    char o[96];
    const char *keep = "Stuart\xE2\x80\x99s \xE2\x80\x9CQ\xE2\x80\x9D \xE2\x80\x93 \xE2\x80\x94 x\xE2\x80\xA6 \xE2\x82\xAC " // typographic marks, euro
                       "Caf\xC3\xA9 \xC3\xBC\xC3\xB1\xC3\xB8\xC3\xA5\xC3\x9F \xC2\xBF? \xC2\xA0\xC2\xB0 "                 // Latin-1
                       "\xEF\x81\xBB \xEF\x80\x81";                                                                      // LV_SYMBOL_DIRECTORY, AUDIO
    ASSERT_TRUE(ui_text_is_clean(keep));
    disp(o, sizeof o, keep);
    ASSERT_EQ(0, strcmp(keep, o));
    disp(o, sizeof o, "02 Have You Fed the Fish\xC2\xBF.flac"); // the ripper's stand-in for '?' is in the font
    ASSERT_EQ(0, strcmp("02 Have You Fed the Fish\xC2\xBF.flac", o));
}

TEST(text_display_maps_the_rest) {
    char o[64];
    disp(o, sizeof o, "\xE2\x80\x90-\xE2\x80\x91\xE2\x88\x92"); // other hyphens and the minus sign
    ASSERT_EQ(0, strcmp("----", o));
    disp(o, sizeof o, "\xE2\x80\x9A" "a\xE2\x80\x9B" "b\xE2\x80\x9E" "c\xE2\x80\xB2\xE2\x80\xB3");
    ASSERT_EQ(0, strcmp(",a'b\"c'\"", o));
    disp(o, sizeof o, "\xC5\x81\xC3\xB3" "d\xC5\xBA \xC5\x92uvre \xC4\xB2 \xC5\xA1\xC4\x8D\xC5\x91"); // Latin Extended-A: base letters
    ASSERT_EQ(0, strcmp("L\xC3\xB3" "dz OEuvre IJ sco", o));
    disp(o, sizeof o, "e\xCC\x81 a\xCC\x88"); // decomposed accents (macOS names): the combining mark is dropped, the letter stays
    ASSERT_EQ(0, strcmp("e a", o));
    disp(o, sizeof o, "\xEF\xBC\xA1\xEF\xBD\x82\xEF\xBC\x91"); // fullwidth A b 1
    ASSERT_EQ(0, strcmp("Ab1", o));
    disp(o, sizeof o, "a\xE2\x80\x8B" "b\xC2\xAD" "c\xEF\xBB\xBF" "d\xE2\x80\x83" "e\xE2\x84\xA2"); // zero width, soft hyphen, BOM, em space, TM
    ASSERT_EQ(0, strcmp("abcd eTM", o));
    disp(o, sizeof o, "a\tb\rc\x7F" "d\n" "e"); // controls to a space; a line break stays
    ASSERT_EQ(0, strcmp("a b c d\ne", o));
}

TEST(text_display_replaces_scripts_and_emoji_with_a_question_mark) {
    char o[64];
    disp(o, sizeof o, "\xE4\xBD\xA0\xE5\xA5\xBD"); // CJK: one '?' per character
    ASSERT_EQ(0, strcmp("??", o));
    disp(o, sizeof o, "x\xF0\x9F\x8E\xB5y"); // emoji (4 bytes): one '?'
    ASSERT_EQ(0, strcmp("x?y", o));
    disp(o, sizeof o, "\xD0\x9F\xD1\x80\xD0\xB8"); // Cyrillic
    ASSERT_EQ(0, strcmp("???", o));
    disp(o, sizeof o, "\xEF\x80\x82"); // a private-use code point that is not one of our symbols
    ASSERT_EQ(0, strcmp("?", o));
    ASSERT_FALSE(ui_text_is_clean("\xE4\xBD\xA0"));
    ASSERT_FALSE(ui_text_is_clean("a\nb"));
}

TEST(text_display_survives_malformed_utf8) {
    char o[64];
    disp(o, sizeof o, "a\x80" "b"); // stray continuation byte
    ASSERT_EQ(0, strcmp("a?b", o));
    disp(o, sizeof o, "a\xC3" "b"); // lead byte followed by an ASCII byte: the lead alone is replaced
    ASSERT_EQ(0, strcmp("a?b", o));
    disp(o, sizeof o, "a\xE2\x80" "b"); // 3-byte sequence broken at its third byte
    ASSERT_EQ(0, strcmp("a?b", o));
    disp(o, sizeof o, "a\xC0\x80" "b"); // overlong NUL
    ASSERT_EQ(0, strcmp("a??b", o));
    disp(o, sizeof o, "a\xED\xA0\x80" "b"); // UTF-16 surrogate
    ASSERT_EQ(0, strcmp("a?b", o));
    disp(o, sizeof o, "a\xF4\x90\x80\x80" "b"); // beyond U+10FFFF
    ASSERT_EQ(0, strcmp("a?b", o));
    disp(o, sizeof o, "ab\xE2\x80"); // cut off at the end of the string: dropped, not read past the NUL
    ASSERT_EQ(0, strcmp("ab", o));
    ASSERT_FALSE(ui_text_is_clean("ab\xE2\x80"));
    disp(o, sizeof o, "");
    ASSERT_EQ(0, strcmp("", o));
}

TEST(text_display_cuts_on_a_character_boundary) {
    char o[7];
    disp(o, sizeof o, "abcde\xC3\xA9\xC3\xA9"); // 'e acute' is 2 bytes: 5 + 2 = 7 > 6 available
    ASSERT_EQ(0, strcmp("abcde", o));
    char o2[5];
    disp(o2, sizeof o2, "ab\xE2\x80\xA6"); // an ellipsis needs 3 bytes
    ASSERT_EQ(0, strcmp("ab", o2));
    char o3[3];
    disp(o3, sizeof o3, "\xC5\x92x"); // OE needs 2 bytes + NUL
    ASSERT_EQ(0, strcmp("OE", o3));
    char o4[1];
    disp(o4, sizeof o4, "abc");
    ASSERT_EQ(0, strcmp("", o4));
}

TEST(text_display_is_never_longer_than_its_input) {
    // ui_label_set_text() builds the result in strlen(raw) + 1 bytes: every code point 1..0x10FFFF must respect that.
    for (uint32_t cp = 1; cp <= 0x10FFFFu; cp++) {
        char in[5] = {0};
        if (cp < 0x80u) {
            in[0] = (char)cp;
        } else if (cp < 0x800u) {
            in[0] = (char)(0xC0u | (cp >> 6));
            in[1] = (char)(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            in[0] = (char)(0xE0u | (cp >> 12));
            in[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            in[2] = (char)(0x80u | (cp & 0x3Fu));
        } else {
            in[0] = (char)(0xF0u | (cp >> 18));
            in[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
            in[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            in[3] = (char)(0x80u | (cp & 0x3Fu));
        }
        char o[8];
        memset(o, 'x', sizeof o);
        ui_text_display(o, strlen(in) + 1, in);
        ASSERT_TRUE(strlen(o) <= strlen(in));
        if (ui_text_has_glyph(cp)) {
            ASSERT_EQ(0, strcmp(in, o));
            ASSERT_TRUE(ui_text_is_clean(in));
        }
    }
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
    RUN(a_hold_through_a_track_change_does_not_seek_or_skip_the_new_track);
    RUN(a_scrub_made_on_another_track_is_not_applied);
    RUN(another_key_or_the_wheel_drops_a_hold_whose_release_may_be_lost);
    RUN(a_hold_whose_release_never_comes_ends_itself);
    RUN(a_hold_that_reaches_the_end_of_the_track_applies_it_at_once);
    RUN(text_display_keeps_what_the_fonts_have);
    RUN(text_display_maps_the_rest);
    RUN(text_display_replaces_scripts_and_emoji_with_a_question_mark);
    RUN(text_display_survives_malformed_utf8);
    RUN(text_display_cuts_on_a_character_boundary);
    RUN(text_display_is_never_longer_than_its_input);
    return RESULT();
}
