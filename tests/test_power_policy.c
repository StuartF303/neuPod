// SPDX-License-Identifier: MPL-2.0
// The sleep / empty-battery policy (app/power/power_policy.c, docs/adr/0021-power-sleep.md): idle counting, what holds the player awake, the
// 24-hour-free battery rule, and the clock cases (wrap, a stamp ahead of now, a clock that jumps back).
#include "power_policy.h"
#include "unit.h"

#define MIN(n) ((n) * 60u * 1000u)

static power_inputs_t idle_at(uint32_t now, uint32_t last) {
    return (power_inputs_t){.now_ms = now, .last_activity_ms = last, .sleep_after_s = 1800u, .battery_valid = true, .battery_pct = 80, .battery_mv = 3900};
}

TEST(the_choice_table_and_default) {
    ASSERT_EQ(0u, power_sleep_seconds(0));
    ASSERT_EQ(600u, power_sleep_seconds(1));
    ASSERT_EQ(1800u, power_sleep_seconds(2));
    ASSERT_EQ(3600u, power_sleep_seconds(3));
    ASSERT_EQ(0u, power_sleep_seconds(POWER_SLEEP_STEPS)); // out of range: never
    ASSERT_EQ(1800u, power_sleep_seconds(POWER_SLEEP_DEFAULT_IDX));
    ASSERT_TRUE(power_sleep_name(0)[0] == 'N');
    ASSERT_EQ('3', power_sleep_name(POWER_SLEEP_DEFAULT_IDX)[0]);
    ASSERT_EQ('-', power_sleep_name(9)[0]);
}

TEST(sleeps_exactly_after_the_setting_with_nothing_happening) {
    power_policy_t p;
    power_policy_init(&p, 1000);
    power_inputs_t in = idle_at(1000, 1000);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = 1000 + MIN(30) - 1;
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = 1000 + MIN(30);
    ASSERT_EQ(POWER_ACT_SLEEP, power_policy_tick(&p, &in));
    ASSERT_EQ(POWER_ACT_SLEEP, power_policy_tick(&p, &in)); // repeats until the caller restarts it
    power_policy_restart(&p, in.now_ms);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
}

TEST(never_never_sleeps) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(0x7fffff00u, 0);
    in.sleep_after_s = 0;
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
}

TEST(any_input_restarts_the_count) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(MIN(29), 0);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.last_activity_ms = MIN(29); // a key at 29 min
    in.now_ms = MIN(58);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = MIN(59);
    ASSERT_EQ(POWER_ACT_SLEEP, power_policy_tick(&p, &in));
}

TEST(a_paused_player_is_idle_but_a_playing_one_is_not) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(0, 0);
    in.audio_playing = true;
    for (uint32_t t = 0; t <= MIN(120); t += MIN(1)) { // two hours of an album with no key pressed
        in.now_ms = t;
        ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    }
    in.audio_playing = false; // it ended: the whole time counts from here, not from the last key two hours ago
    in.now_ms = MIN(120) + MIN(29);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = MIN(120) + MIN(30);
    ASSERT_EQ(POWER_ACT_SLEEP, power_policy_tick(&p, &in));
}

TEST(busy_work_holds_it_awake_and_the_time_starts_when_it_ends) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(0, 0);
    in.busy = true; // a library build, a pairing, a write
    in.now_ms = MIN(45);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.busy = false;
    in.now_ms = MIN(45) + MIN(29);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = MIN(45) + MIN(30);
    ASSERT_EQ(POWER_ACT_SLEEP, power_policy_tick(&p, &in));
}

TEST(never_on_external_power) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(0, 0);
    in.external_power = true;
    in.now_ms = MIN(300);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.external_power = false; // unplugged: counted from the unplugging
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms += MIN(30);
    ASSERT_EQ(POWER_ACT_SLEEP, power_policy_tick(&p, &in));
}

TEST(an_empty_battery_for_thirty_seconds_powers_off) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(10000, 10000);
    in.battery_pct = POWER_CRIT_PCT;
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in)); // the streak starts
    in.now_ms = 10000 + POWER_CRIT_HOLD_MS - 1;
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = 10000 + POWER_CRIT_HOLD_MS;
    ASSERT_EQ(POWER_ACT_POWEROFF_CRITICAL, power_policy_tick(&p, &in));
    in.last_activity_ms = in.now_ms; // being used does not save it: the battery rule is not about idleness
    ASSERT_EQ(POWER_ACT_POWEROFF_CRITICAL, power_policy_tick(&p, &in));
}

TEST(a_break_in_the_streak_starts_it_again) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(0, 0);
    in.battery_pct = 2;
    power_policy_tick(&p, &in);
    in.now_ms = 20000;
    in.battery_pct = 4; // a reading above the threshold
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.battery_pct = 2;
    in.now_ms = 25000;
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = 25000 + POWER_CRIT_HOLD_MS - 1;
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = 25000 + POWER_CRIT_HOLD_MS;
    ASSERT_EQ(POWER_ACT_POWEROFF_CRITICAL, power_policy_tick(&p, &in));
}

TEST(the_battery_rule_needs_a_valid_reading_and_no_charger) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(0, 0);
    in.battery_pct = 0;
    in.battery_valid = false; // no gauge, a failed read, or no battery fitted: unknown is not empty
    power_policy_tick(&p, &in);
    in.now_ms = MIN(5);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.battery_valid = true;
    in.external_power = true; // a charger lifts the rule
    power_policy_tick(&p, &in);
    in.now_ms = MIN(10);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
}

TEST(a_low_voltage_alone_counts_and_an_unknown_voltage_does_not) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(0, 0);
    in.battery_pct = 40; // a gauge whose percentage lags a sagging cell
    in.battery_mv = POWER_CRIT_MV - 1;
    power_policy_tick(&p, &in);
    in.now_ms = POWER_CRIT_HOLD_MS;
    ASSERT_EQ(POWER_ACT_POWEROFF_CRITICAL, power_policy_tick(&p, &in));
    power_policy_init(&p, 0);
    in.now_ms = 0;
    in.battery_mv = 0; // unknown: judged by the percentage only
    power_policy_tick(&p, &in);
    in.now_ms = MIN(2);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
}

TEST(the_battery_rule_comes_before_sleep) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(0, 0);
    in.battery_pct = 1;
    power_policy_tick(&p, &in);
    in.now_ms = MIN(31); // idle for the full time and empty for longer than 30 s
    ASSERT_EQ(POWER_ACT_POWEROFF_CRITICAL, power_policy_tick(&p, &in));
}

TEST(the_counter_wrap_changes_nothing) {
    power_policy_t p;
    const uint32_t start = 0xFFFFFFFFu - MIN(10);
    power_policy_init(&p, start);
    power_inputs_t in = idle_at(start, start);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = start + MIN(29); // wrapped past zero by now
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = start + MIN(30);
    ASSERT_EQ(POWER_ACT_SLEEP, power_policy_tick(&p, &in));
    // the battery streak across the wrap
    power_policy_init(&p, 0xFFFFFFF0u);
    in = idle_at(0xFFFFFFF0u, 0xFFFFFFF0u);
    in.battery_pct = 1;
    power_policy_tick(&p, &in);
    in.now_ms = 0xFFFFFFF0u + POWER_CRIT_HOLD_MS;
    ASSERT_EQ(POWER_ACT_POWEROFF_CRITICAL, power_policy_tick(&p, &in));
}

TEST(a_stamp_slightly_ahead_of_now_is_not_idle_and_a_clock_that_jumps_back_restarts) {
    power_policy_t p;
    power_policy_init(&p, 100000);
    power_inputs_t in = idle_at(100000, 100000 + 60000); // the host tour stamps keys ahead of the tick clock
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = 100000 + 60000 + MIN(30) - 1;
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = 100000 + 60000 + MIN(30);
    ASSERT_EQ(POWER_ACT_SLEEP, power_policy_tick(&p, &in));
    // the clock steps back by an hour: no instant sleep, and the count starts afresh from the new time
    power_policy_init(&p, MIN(120));
    in = idle_at(MIN(120), MIN(120));
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = MIN(60);
    in.last_activity_ms = MIN(120);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = MIN(60) + MIN(29);
    ASSERT_EQ(POWER_ACT_NONE, power_policy_tick(&p, &in));
    in.now_ms = MIN(60) + MIN(30);
    ASSERT_EQ(POWER_ACT_SLEEP, power_policy_tick(&p, &in));
}

TEST(the_battery_streak_survives_a_restart_of_the_idle_clock) {
    power_policy_t p;
    power_policy_init(&p, 0);
    power_inputs_t in = idle_at(0, 0);
    in.battery_pct = 1;
    power_policy_tick(&p, &in);
    power_policy_restart(&p, 20000); // a failed sleep, or a wake
    in.now_ms = POWER_CRIT_HOLD_MS;
    ASSERT_EQ(POWER_ACT_POWEROFF_CRITICAL, power_policy_tick(&p, &in));
}

TEST(two_failed_sleeps_back_off_for_an_hour) {
    power_guard_t g;
    power_guard_init(&g);
    ASSERT_TRUE(power_guard_allows(&g, 1000));
    ASSERT_FALSE(power_guard_sleep_failed(&g, 1000));
    ASSERT_TRUE(power_guard_allows(&g, 1000));
    power_guard_sleep_ok(&g); // a success in between resets the count
    ASSERT_FALSE(power_guard_sleep_failed(&g, 2000));
    ASSERT_TRUE(power_guard_sleep_failed(&g, 3000)); // the second in a row
    ASSERT_FALSE(power_guard_allows(&g, 3000 + POWER_SLEEP_BACKOFF_MS - 1));
    ASSERT_TRUE(power_guard_allows(&g, 3000 + POWER_SLEEP_BACKOFF_MS));
    ASSERT_FALSE(power_guard_sleep_failed(&g, 9000000)); // the count started again
}

TEST(a_back_off_does_not_outlive_a_clock_that_jumps_back) {
    power_guard_t g;
    power_guard_init(&g);
    power_guard_sleep_failed(&g, 10000000);
    power_guard_sleep_failed(&g, 10000000);
    ASSERT_FALSE(power_guard_allows(&g, 10000000 + 1000));
    ASSERT_TRUE(power_guard_allows(&g, 1000)); // hours back: not an endless wait
}

TEST(a_refused_power_off_latches_until_the_gauge_shows_a_discharge_or_six_hours) {
    power_guard_t g;
    power_guard_init(&g);
    power_guard_poweroff_refused(&g, 5000, 4100);
    ASSERT_FALSE(power_guard_allows(&g, 5000));
    power_guard_observe(&g, 6000, true, 500, 4090); // still charging, voltage steady
    ASSERT_FALSE(power_guard_allows(&g, 6000));
    power_guard_observe(&g, 7000, false, -9000, 4000); // an invalid reading says nothing
    ASSERT_FALSE(power_guard_allows(&g, 7000));
    power_guard_observe(&g, 8000, true, -3000, 4090); // discharging
    ASSERT_TRUE(power_guard_allows(&g, 8000));
    power_guard_poweroff_refused(&g, 9000, 4100);
    power_guard_observe(&g, 10000, true, 0, 4000); // the cell fell 100 mV
    ASSERT_TRUE(power_guard_allows(&g, 10000));
    power_guard_poweroff_refused(&g, 20000, 0);
    power_guard_observe(&g, 20000 + POWER_REFUSED_CLEAR_MS - 1, false, 0, 0);
    ASSERT_FALSE(power_guard_allows(&g, 20000));
    power_guard_observe(&g, 20000 + POWER_REFUSED_CLEAR_MS, false, 0, 0);
    ASSERT_TRUE(power_guard_allows(&g, 20000 + POWER_REFUSED_CLEAR_MS));
}

int main(void) {
    RUN(the_choice_table_and_default);
    RUN(sleeps_exactly_after_the_setting_with_nothing_happening);
    RUN(never_never_sleeps);
    RUN(any_input_restarts_the_count);
    RUN(a_paused_player_is_idle_but_a_playing_one_is_not);
    RUN(busy_work_holds_it_awake_and_the_time_starts_when_it_ends);
    RUN(never_on_external_power);
    RUN(an_empty_battery_for_thirty_seconds_powers_off);
    RUN(a_break_in_the_streak_starts_it_again);
    RUN(the_battery_rule_needs_a_valid_reading_and_no_charger);
    RUN(a_low_voltage_alone_counts_and_an_unknown_voltage_does_not);
    RUN(the_battery_rule_comes_before_sleep);
    RUN(the_counter_wrap_changes_nothing);
    RUN(a_stamp_slightly_ahead_of_now_is_not_idle_and_a_clock_that_jumps_back_restarts);
    RUN(the_battery_streak_survives_a_restart_of_the_idle_clock);
    RUN(two_failed_sleeps_back_off_for_an_hour);
    RUN(a_back_off_does_not_outlive_a_clock_that_jumps_back);
    RUN(a_refused_power_off_latches_until_the_gauge_shows_a_discharge_or_six_hours);
    return RESULT();
}
