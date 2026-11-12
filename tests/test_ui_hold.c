// SPDX-License-Identifier: MPL-2.0
// Hold and the Power-key gestures (ui_hold.c, docs/adr/0020-hold.md): the 2 s toggle, the 10 s power-off with its countdown from 5 s left, release
// cancelling, the short press that sleeps the screen, the press that only wakes it, and the slide switch / auto-lock setting the same state.
#include "ui_hold.h"
#include "unit.h"

TEST(a_short_press_sleeps_the_screen_and_changes_nothing_else) {
    ui_hold_t h;
    ui_hold_init(&h);
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_power_down(&h, 1000, false));
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_tick(&h, 1100, true));
    uint32_t r = ui_hold_power_up(&h, 1300);
    ASSERT_TRUE((r & UI_HOLD_SLEEP) != 0);
    ASSERT_FALSE(h.locked);
    ASSERT_EQ(0, r & (UI_HOLD_LOCKED | UI_HOLD_POWEROFF));
    // a tap too short for the ring to show does not ask to take an overlay down
    ui_hold_power_down(&h, 5000, false);
    r = ui_hold_power_up(&h, 5100);
    ASSERT_EQ(UI_HOLD_SLEEP, r);
}

TEST(an_aborted_hold_does_nothing) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 1000, false);
    ASSERT_EQ(UI_HOLD_P_RING, ui_hold_progress(&h, 2500, true).phase); // the ring is showing ...
    uint32_t r = ui_hold_power_up(&h, 2600); // ... and the finger lifts at 1.6 s: neither lock nor sleep, just take the ring down
    ASSERT_EQ(UI_HOLD_CANCELLED, r);
    ASSERT_FALSE(h.locked);
    ui_hold_power_down(&h, 5000, false); // 399 ms is still a tap, 400 ms is not
    ASSERT_EQ(UI_HOLD_SLEEP | 0u, ui_hold_power_up(&h, 5399) & UI_HOLD_SLEEP);
    ui_hold_power_down(&h, 8000, false);
    ASSERT_EQ(0, ui_hold_power_up(&h, 8400) & UI_HOLD_SLEEP);
}

TEST(a_press_that_woke_the_screen_does_not_put_it_back_to_sleep) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 1000, true);
    uint32_t r = ui_hold_power_up(&h, 1200);
    ASSERT_EQ(0, r & UI_HOLD_SLEEP);
}

TEST(two_seconds_toggles_hold_once_while_still_held) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 1000, false);
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_tick(&h, 2999, true)); // 1999 ms
    ASSERT_EQ(UI_HOLD_LOCKED, ui_hold_tick(&h, 3000, true));
    ASSERT_TRUE(h.locked);
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_tick(&h, 3500, true)); // once per press
    uint32_t r = ui_hold_power_up(&h, 4000);
    ASSERT_EQ(0, r & (UI_HOLD_SLEEP | UI_HOLD_LOCKED | UI_HOLD_UNLOCKED)); // the release after a toggle is not a short press
    // the next hold unlocks
    ui_hold_power_down(&h, 9000, false);
    ASSERT_EQ(UI_HOLD_UNLOCKED, ui_hold_tick(&h, 11000, true));
    ASSERT_FALSE(h.locked);
}

TEST(a_poll_gap_longer_than_the_hold_still_toggles_at_release) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 0, false);
    uint32_t r = ui_hold_power_up(&h, 2500); // no tick in between
    ASSERT_TRUE((r & UI_HOLD_LOCKED) != 0);
    ASSERT_EQ(0, r & UI_HOLD_SLEEP);
    ASSERT_TRUE(h.locked);
}

TEST(ten_seconds_asks_for_power_off_once_and_only_where_it_can) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 0, false);
    ASSERT_EQ(UI_HOLD_LOCKED, ui_hold_tick(&h, 2000, true));
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_tick(&h, 9999, true));
    ASSERT_EQ(UI_HOLD_POWEROFF, ui_hold_tick(&h, 10000, true));
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_tick(&h, 12000, true)); // once
    ui_hold_power_up(&h, 12500);
    // a platform that cannot power off never asks, however long the key is held
    ui_hold_power_down(&h, 20000, false);
    ASSERT_EQ(UI_HOLD_UNLOCKED, ui_hold_tick(&h, 22000, false));
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_tick(&h, 60000, false));
}

TEST(releasing_late_never_powers_off) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 0, false);
    uint32_t r = ui_hold_power_up(&h, 15000); // the UI was stalled: no tick saw 10 s
    ASSERT_EQ(0, r & UI_HOLD_POWEROFF);
}

TEST(release_cancels_the_countdown) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 0, false);
    ui_hold_tick(&h, 2000, true);
    ui_hold_tick(&h, 7000, true);
    uint32_t r = ui_hold_power_up(&h, 7500);
    ASSERT_TRUE((r & UI_HOLD_CANCELLED) != 0);
    ASSERT_EQ(0, r & UI_HOLD_POWEROFF);
    ASSERT_EQ(UI_HOLD_P_NONE, ui_hold_progress(&h, 7600, true).phase);
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_tick(&h, 20000, true)); // nothing carries over
    ui_hold_power_down(&h, 30000, false); // and a new press starts clean
    ASSERT_EQ(UI_HOLD_P_NONE, ui_hold_progress(&h, 30100, true).phase);
}

TEST(the_ring_fills_towards_the_toggle_and_says_which_way) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 1000, false);
    ASSERT_EQ(UI_HOLD_P_NONE, ui_hold_progress(&h, 1399, true).phase);
    ui_hold_progress_t p = ui_hold_progress(&h, 1500, true);
    ASSERT_EQ(UI_HOLD_P_RING, p.phase);
    ASSERT_EQ(25, p.pct);
    ASSERT_TRUE(p.to_lock);
    p = ui_hold_progress(&h, 2900, true);
    ASSERT_EQ(UI_HOLD_P_RING, p.phase);
    ASSERT_EQ(95, p.pct);
    ui_hold_tick(&h, 3000, true); // locked now: the ring is done
    ASSERT_EQ(UI_HOLD_P_NONE, ui_hold_progress(&h, 3100, true).phase);
    ui_hold_power_up(&h, 3200);
    ui_hold_power_down(&h, 4000, false); // locked: the next ring is for unlocking
    ASSERT_FALSE(ui_hold_progress(&h, 5000, true).to_lock);
}

TEST(the_countdown_starts_at_five_seconds_left) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 0, false);
    ui_hold_tick(&h, 2000, true);
    ASSERT_EQ(UI_HOLD_P_NONE, ui_hold_progress(&h, 4999, true).phase);
    ui_hold_progress_t p = ui_hold_progress(&h, 5000, true);
    ASSERT_EQ(UI_HOLD_P_COUNTDOWN, p.phase);
    ASSERT_EQ(5, p.secs);
    ASSERT_EQ(100, p.pct);
    ASSERT_EQ(5, ui_hold_progress(&h, 5001, true).secs);
    ASSERT_EQ(4, ui_hold_progress(&h, 6000, true).secs);
    ASSERT_EQ(1, ui_hold_progress(&h, 9000, true).secs);
    ASSERT_EQ(1, ui_hold_progress(&h, 9999, true).secs);
    p = ui_hold_progress(&h, 7500, true);
    ASSERT_EQ(3, p.secs);
    ASSERT_EQ(50, p.pct);
    ASSERT_EQ(UI_HOLD_P_NONE, ui_hold_progress(&h, 7500, false).phase); // no countdown where nothing can power off
    ui_hold_tick(&h, 10000, true);
    ASSERT_EQ(UI_HOLD_P_POWEROFF, ui_hold_progress(&h, 10001, true).phase);
}

TEST(timing_survives_the_clock_wrap) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 0xFFFFF000u, false);
    /* 0xFFFFF000 to 0x000003FF is 4096 + 1023 = 5119 ms across the wrap */
    ASSERT_EQ(UI_HOLD_LOCKED, ui_hold_tick(&h, 0x000003FFu, true));
    ASSERT_EQ(UI_HOLD_P_NONE, ui_hold_progress(&h, 0x000003FFu, false).phase);
    ASSERT_EQ(UI_HOLD_P_COUNTDOWN, ui_hold_progress(&h, 0x000003FFu, true).phase);
}

TEST(a_second_down_without_an_up_keeps_the_first_press) {
    ui_hold_t h;
    ui_hold_init(&h);
    ui_hold_power_down(&h, 1000, false);
    ui_hold_power_down(&h, 2500, false); // a duplicate: the press time does not move
    ASSERT_EQ(UI_HOLD_LOCKED, ui_hold_tick(&h, 3000, true));
}

TEST(an_up_without_a_down_and_a_lost_press_are_ignored) {
    ui_hold_t h;
    ui_hold_init(&h);
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_power_up(&h, 500));
    ui_hold_power_down(&h, 1000, false);
    ui_hold_power_lost(&h);
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_tick(&h, 20000, true));
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_power_up(&h, 20100));
    ASSERT_FALSE(h.locked);
}

TEST(the_switch_the_console_key_and_auto_lock_set_the_same_state) {
    ui_hold_t h;
    ui_hold_init(&h);
    ASSERT_EQ(UI_HOLD_LOCKED, ui_hold_set(&h, true)); // switch on, or auto-lock
    ASSERT_EQ(UI_HOLD_NONE, ui_hold_set(&h, true));   // already: no second confirmation
    ASSERT_EQ(UI_HOLD_UNLOCKED, ui_hold_set(&h, false));
    ASSERT_EQ(UI_HOLD_LOCKED, ui_hold_toggle(&h));
    ASSERT_EQ(UI_HOLD_UNLOCKED, ui_hold_toggle(&h));
    // the gesture and the switch agree: unlocking with Power after the switch locked it
    ui_hold_set(&h, true);
    ui_hold_power_down(&h, 0, false);
    ASSERT_EQ(UI_HOLD_UNLOCKED, ui_hold_tick(&h, 2000, true));
}

int main(void) {
    RUN(a_short_press_sleeps_the_screen_and_changes_nothing_else);
    RUN(an_aborted_hold_does_nothing);
    RUN(a_press_that_woke_the_screen_does_not_put_it_back_to_sleep);
    RUN(two_seconds_toggles_hold_once_while_still_held);
    RUN(a_poll_gap_longer_than_the_hold_still_toggles_at_release);
    RUN(ten_seconds_asks_for_power_off_once_and_only_where_it_can);
    RUN(releasing_late_never_powers_off);
    RUN(release_cancels_the_countdown);
    RUN(the_ring_fills_towards_the_toggle_and_says_which_way);
    RUN(the_countdown_starts_at_five_seconds_left);
    RUN(timing_survives_the_clock_wrap);
    RUN(a_second_down_without_an_up_keeps_the_first_press);
    RUN(an_up_without_a_down_and_a_lost_press_are_ignored);
    RUN(the_switch_the_console_key_and_auto_lock_set_the_same_state);
    return RESULT();
}
