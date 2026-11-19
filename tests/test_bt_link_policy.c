// SPDX-License-Identifier: MPL-2.0
// The A2DP link policy (hal/common/bt_btstack/bt_link_policy.c): the catch-up budget and the bitpool ladder.
#include <stddef.h>

#include "bt_link_policy.h"
#include "unit.h"

TEST(test_catchup_budget_bounds_what_is_owed_and_counts_the_rest) {
    uint32_t dropped = 99;
    const uint32_t cap = 44100u * BT_LINK_CATCHUP_MS / 1000u; // 7056 samples
    ASSERT_EQ(cap, bt_link_owed_clamp(cap, 44100, &dropped));
    ASSERT_EQ(0, dropped);
    ASSERT_EQ(500, bt_link_owed_clamp(500, 44100, &dropped));
    ASSERT_EQ(0, dropped);
    ASSERT_EQ(cap, bt_link_owed_clamp(cap + 1000u, 44100, &dropped));
    ASSERT_EQ(1000, dropped);
    ASSERT_EQ(cap, bt_link_owed_clamp(cap + 5u, 44100, NULL)); // dropped not wanted
    ASSERT_TRUE(BT_LINK_CATCHUP_MS > BT_LINK_STALL_MS);        // a bigger budget than the old 40 ms cap
    ASSERT_TRUE(BT_LINK_CATCHUP_MS >= 150u && BT_LINK_CATCHUP_MS <= 200u);
}

TEST(test_pressure_is_a_long_wait_or_a_big_debt) {
    ASSERT_FALSE(bt_link_pressured(0, 0));
    ASSERT_FALSE(bt_link_pressured(BT_LINK_PRESSURE_WAIT_MS - 1u, BT_LINK_PRESSURE_OWED_MS - 1u));
    ASSERT_TRUE(bt_link_pressured(BT_LINK_PRESSURE_WAIT_MS, 0));
    ASSERT_TRUE(bt_link_pressured(0, BT_LINK_PRESSURE_OWED_MS));
}

TEST(test_ladder_follows_the_headsets_limits) {
    ASSERT_EQ(3, bt_link_levels(53, 2));
    ASSERT_EQ(53, bt_link_bitpool(53, 2, 0));
    ASSERT_EQ(45, bt_link_bitpool(53, 2, 1));
    ASSERT_EQ(35, bt_link_bitpool(53, 2, 2));
    ASSERT_EQ(35, bt_link_bitpool(53, 2, 9)); // past the end: the last
    // a headset with a floor at 40: the 35 step becomes 40, still a step of its own below 45
    ASSERT_EQ(3, bt_link_levels(53, 40));
    ASSERT_EQ(40, bt_link_bitpool(53, 40, 2));
    // a floor at 45: 45 and 35 coincide
    ASSERT_EQ(2, bt_link_levels(53, 45));
    ASSERT_EQ(45, bt_link_bitpool(53, 45, 1));
    // a floor at the ceiling cannot step at all
    ASSERT_EQ(1, bt_link_levels(53, 53));
    ASSERT_EQ(53, bt_link_bitpool(53, 53, 2));
    // a headset whose maximum is already 40: one step (35)
    ASSERT_EQ(2, bt_link_levels(40, 2));
    ASSERT_EQ(40, bt_link_bitpool(40, 2, 0));
    ASSERT_EQ(35, bt_link_bitpool(40, 2, 1));
    // SBC-XQ ceiling: 76 -> 45 -> 35
    ASSERT_EQ(76, bt_link_bitpool(76, 2, 0));
    ASSERT_EQ(45, bt_link_bitpool(76, 2, 1));
    ASSERT_EQ(35, bt_link_bitpool(76, 2, 2));
}

// ---- The bitpool policy, driven with 8 ms ticks of (wait_ms, owed_ms) ----

typedef struct {
    bt_link_policy_t p;
    uint32_t t;
    uint8_t ceil;
    uint8_t min_bp;
    uint8_t bp;            // the bitpool in force
    int changes;
    int early_ups;         // step ups less than 30 s after a step down
    uint32_t last_down_t;
    bool has_down;
    unsigned k;            // tick counter (spreads the pressured ticks evenly)
} sim_t;

static void sim_init(sim_t *s, uint32_t t0, uint8_t ceil, uint8_t min_bp) {
    *s = (sim_t){.t = t0, .ceil = ceil, .min_bp = min_bp, .bp = ceil};
    bt_link_policy_init(&s->p, t0);
}

// One 8 ms tick. Returns the policy's verdict (0 = no change).
static uint8_t tk(sim_t *s, uint32_t wait_ms, uint32_t owed_ms) {
    const uint8_t r = bt_link_policy_tick(&s->p, s->t, wait_ms, owed_ms, s->ceil, s->min_bp);
    if (r != 0) {
        s->changes++;
        if (r < s->bp) {
            s->last_down_t = s->t;
            s->has_down = true;
        } else if (s->has_down && (uint32_t)(s->t - s->last_down_t) < 30000u) {
            s->early_ups++;
        }
        s->bp = r;
    }
    s->t += 8u;
    return r;
}

static void idle(sim_t *s, uint32_t ms) {
    for (uint32_t end = s->t + ms; (int32_t)(end - s->t) > 0;) {
        tk(s, 0, 0);
    }
}

// pressured (wait 30 ms, below any stall) for @p pct of the ticks, over @p ms
static void pressure(sim_t *s, uint32_t ms, unsigned pct) {
    for (uint32_t end = s->t + ms; (int32_t)(end - s->t) > 0;) {
        tk(s, (s->k++ * pct) % 100u < pct ? 30u : 0u, 0);
    }
}

// A stall episode as the pacer sees it: the debt rises 8 ms a tick to @p peak_ms and falls back (the wait grows with it, up to 25 ms: no pressure by itself).
static void episode(sim_t *s, uint32_t peak_ms) {
    for (uint32_t owed = 0; owed <= peak_ms; owed += 8u) {
        tk(s, 0, owed);
    }
    for (uint32_t owed = peak_ms; owed >= 8u; owed -= 8u) {
        tk(s, 0, owed - 8u);
    }
}

// Run the first second (the start grace) clean, then some more.
static void settle_in(sim_t *s) { idle(s, BT_LINK_START_GRACE_MS + 1000u); }

// Idle until the policy changes the bitpool: it must take about @p hold_ms (a window boundary can make it up to a second late), and the bitpool goes up.
static void expect_up_after(sim_t *s, uint32_t hold_ms) {
    const uint8_t low = s->bp;
    const uint32_t t0 = s->t;
    while (s->bp == low && (uint32_t)(s->t - t0) < hold_ms + 5000u) {
        tk(s, 0, 0);
    }
    ASSERT_TRUE(s->bp > low);
    ASSERT_TRUE((uint32_t)(s->t - t0) + 100u >= hold_ms);
    ASSERT_TRUE((uint32_t)(s->t - t0) <= hold_ms + 1100u);
}

TEST(test_the_first_stall_steps_down_on_the_tick_it_crosses_40_ms) {
    sim_t s;
    sim_init(&s, 5000, 53, 2);
    settle_in(&s);
    uint32_t owed = 0;
    for (; owed < BT_LINK_STALL_MS; owed += 8u) {
        ASSERT_EQ(0, tk(&s, 0, owed));
    }
    ASSERT_EQ(BT_LINK_STALL_MS, owed);
    ASSERT_EQ(45, tk(&s, 0, owed)); // 40 ms owed: the episode starts, one level down at once
    for (owed += 8u; owed <= 48u; owed += 8u) {
        ASSERT_EQ(0, tk(&s, 0, owed));
    }
    idle(&s, 3000);
    ASSERT_EQ(1, s.changes);
}

TEST(test_a_severe_stall_jumps_to_the_last_level) {
    sim_t a;
    sim_init(&a, 0, 53, 2);
    settle_in(&a);
    ASSERT_EQ(35, tk(&a, 0, BT_LINK_SEVERE_OWED_MS)); // owed 90: 53 -> 35 in one change
    ASSERT_EQ(0, tk(&a, 0, 160));
    ASSERT_EQ(1, a.changes);

    sim_t w;
    sim_init(&w, 0, 53, 2);
    settle_in(&w);
    ASSERT_EQ(0, tk(&w, BT_LINK_SEVERE_WAIT_MS - 1u, 0)); // 119 ms: not severe (only pressure)
    ASSERT_EQ(35, tk(&w, BT_LINK_SEVERE_WAIT_MS, 0));     // one packet waiting 120 ms
    ASSERT_EQ(1, w.changes);

    // from 45 straight to 35, ignoring the settle time
    sim_t b;
    sim_init(&b, 0, 53, 2);
    settle_in(&b);
    episode(&b, 48);
    ASSERT_EQ(45, b.bp);
    idle(&b, 200);
    ASSERT_EQ(35, tk(&b, 0, 100));
    ASSERT_EQ(2, b.changes);
}

TEST(test_a_long_stall_like_the_walk_lands_on_35) {
    // Jumped straight in (a late tick): exactly one change, 53 -> 35.
    sim_t j;
    sim_init(&j, 0, 53, 2);
    settle_in(&j);
    ASSERT_EQ(35, tk(&j, 0, 160));
    for (uint32_t wait = 8; wait < 1700u; wait += 8u) {
        ASSERT_EQ(0, tk(&j, wait, 160));
    }
    ASSERT_EQ(1, j.changes);
    // Built up tick by tick (owed +8 ms a tick, the wait growing to 1666 ms): 45 at 40 ms, then 35 at 90 ms, and nothing after.
    sim_t s;
    sim_init(&s, 0, 53, 2);
    settle_in(&s);
    uint32_t wait = 0;
    for (uint32_t owed = 0; owed < 160u; owed += 8u, wait += 8u) {
        tk(&s, wait, owed);
    }
    for (; wait < 1666u; wait += 8u) {
        ASSERT_EQ(0, tk(&s, wait, 160));
    }
    ASSERT_EQ(35, s.bp);
    ASSERT_EQ(2, s.changes);
    idle(&s, 5000);
    ASSERT_EQ(2, s.changes);
}

TEST(test_stall_episodes_are_rate_limited_by_the_settle_time) {
    sim_t s;
    sim_init(&s, 0, 53, 2);
    settle_in(&s);
    episode(&s, 48);
    ASSERT_EQ(45, s.bp);
    ASSERT_EQ(1, s.changes);
    idle(&s, 300 - 104); // the next episode starts 300 ms after the first
    episode(&s, 48);
    ASSERT_EQ(45, s.bp); // too soon after the step: still one step
    ASSERT_EQ(1, s.changes);
    idle(&s, 1200);
    episode(&s, 48);
    ASSERT_EQ(35, s.bp);
    ASSERT_EQ(2, s.changes);
}

TEST(test_the_first_second_of_a_stream_ignores_the_start_up_burst) {
    sim_t s;
    sim_init(&s, 0, 53, 2);
    // media_start owes up to 40 ms and the first can-send-now can take a while
    for (int i = 0; i < 40; i++) {
        tk(&s, 0, 40u + (unsigned)(i % 3) * 4u);
    }
    ASSERT_EQ(0, s.changes);
    tk(&s, 0, 0); // stall over; an episode and a severe tick inside the grace do nothing either
    tk(&s, 0, 100);
    tk(&s, 0, 0);
    while (s.t < BT_LINK_START_GRACE_MS) {
        tk(&s, 0, 0);
    }
    ASSERT_EQ(0, s.changes);
    episode(&s, 48); // the same thing after the grace does
    ASSERT_EQ(45, s.bp);
    ASSERT_EQ(1, s.changes);
}

TEST(test_one_bad_window_steps_down_one_level) {
    sim_t s;
    sim_init(&s, 0, 53, 2);
    pressure(&s, 5000, 20); // 20 % pressured: not bad
    ASSERT_EQ(0, s.changes);
    sim_t b;
    sim_init(&b, 0, 53, 2);
    idle(&b, 3000);
    uint8_t got = 0;
    for (uint32_t end = b.t + 1100u; (int32_t)(end - b.t) > 0;) {
        uint8_t r = tk(&b, (b.k++ * 30u) % 100u < 30u ? 30u : 0u, 0);
        got = r != 0 ? r : got;
    }
    ASSERT_EQ(45, got); // one window of >= 25 % is enough
    ASSERT_EQ(1, b.changes);
    // sustained: one level a second, then the bottom
    pressure(&b, 10000, 60);
    ASSERT_EQ(35, b.bp);
    ASSERT_EQ(2, b.changes);
}

TEST(test_it_steps_up_only_after_a_long_clean_spell) {
    sim_t s;
    sim_init(&s, 0, 53, 2);
    settle_in(&s);
    ASSERT_EQ(35, tk(&s, 0, 100));
    expect_up_after(&s, BT_LINK_UP_HOLD_MS);
    ASSERT_EQ(45, s.bp);
    expect_up_after(&s, BT_LINK_UP_HOLD_MS); // each level needs its own full hold
    ASSERT_EQ(53, s.bp);
    ASSERT_EQ(3, s.changes);
    ASSERT_EQ(0, s.early_ups);
}

TEST(test_a_stall_tick_or_a_middling_window_restarts_the_hold) {
    sim_t s;
    sim_init(&s, 0, 53, 2);
    settle_in(&s);
    tk(&s, 0, 100);
    idle(&s, 20000);
    tk(&s, 0, BT_LINK_STALL_MS); // one stall tick at the bottom: no step possible, but the clean spell restarts
    expect_up_after(&s, BT_LINK_UP_HOLD_MS);
    ASSERT_EQ(45, s.bp);

    sim_t m;
    sim_init(&m, 0, 53, 2);
    settle_in(&m);
    tk(&m, 0, 100);
    idle(&m, 20000);
    pressure(&m, 1000, 10); // 10 % pressured: neither bad nor clean
    ASSERT_EQ(1, m.changes);
    expect_up_after(&m, BT_LINK_UP_HOLD_MS);
    ASSERT_EQ(45, m.bp);
}

TEST(test_a_quick_return_to_trouble_doubles_the_hold_up_to_a_cap) {
    sim_t s;
    sim_init(&s, 0, 53, 2);
    settle_in(&s);
    tk(&s, 0, 100);
    ASSERT_EQ(35, s.bp);
    expect_up_after(&s, BT_LINK_UP_HOLD_MS);
    ASSERT_EQ(45, s.bp);
    const uint32_t holds[] = {2u * BT_LINK_UP_HOLD_MS, 4u * BT_LINK_UP_HOLD_MS, BT_LINK_UP_HOLD_MAX_MS, BT_LINK_UP_HOLD_MAX_MS};
    for (unsigned i = 0; i < 4; i++) {
        idle(&s, 10000);
        episode(&s, 48); // within 120 s of the step up
        ASSERT_EQ(35, s.bp);
        expect_up_after(&s, holds[i]);
        ASSERT_EQ(45, s.bp);
    }
    ASSERT_EQ(240000u, holds[2]);
    ASSERT_EQ(0, s.early_ups);
    // a step down long after the last step up starts again from the base hold
    idle(&s, BT_LINK_QUICK_DOWN_MS + 5000u);
    episode(&s, 48);
    ASSERT_EQ(35, s.bp);
    expect_up_after(&s, BT_LINK_UP_HOLD_MS);
    ASSERT_EQ(45, s.bp);
}

TEST(test_a_walk_with_bad_patches_changes_rarely_and_never_climbs_soon_after_a_fall) {
    sim_t s;
    sim_init(&s, 123456u, 53, 2);
    settle_in(&s);
    const uint32_t patch_s[] = {20, 40, 25, 30, 35, 20};
    const uint32_t clean_s[] = {60, 40, 90, 45, 80, 50};
    for (unsigned i = 0; i < 6; i++) {
        for (uint32_t end = s.t + patch_s[i] * 1000u; (int32_t)(end - s.t) > 0;) {
            episode(&s, i % 2 ? 100 : 56); // a burst every few seconds, some of them severe
            pressure(&s, 2000u + 500u * (i % 3), 0);
        }
        idle(&s, clean_s[i] * 1000u);
    }
    ASSERT_TRUE(s.t - 123456u > 500000u);
    ASSERT_TRUE(s.changes <= 6);
    ASSERT_EQ(0, s.early_ups);
}

TEST(test_a_headset_without_room_to_step_never_changes) {
    sim_t s;
    sim_init(&s, 0, 40, 40);
    settle_in(&s);
    for (int i = 0; i < 2500; i++) {
        tk(&s, 500, 160); // pressured, stalled and severe at once
    }
    episode(&s, 48);
    pressure(&s, 5000, 100);
    ASSERT_EQ(0, s.changes);
    ASSERT_EQ(40, s.bp);
}

TEST(test_the_xq_ceiling_steps_to_35_on_a_severe_stall) {
    sim_t s;
    sim_init(&s, 0, 76, 2);
    settle_in(&s);
    ASSERT_EQ(35, tk(&s, 0, BT_LINK_SEVERE_OWED_MS));
    expect_up_after(&s, BT_LINK_UP_HOLD_MS);
    ASSERT_EQ(45, s.bp);
    expect_up_after(&s, BT_LINK_UP_HOLD_MS);
    ASSERT_EQ(76, s.bp);
}

TEST(test_the_clock_wraps) {
    // the stall (and the grace before it) straddles the wrap
    sim_t s;
    sim_init(&s, 0xFFFFFFFFu - 1500u, 53, 2);
    idle(&s, 1200);
    ASSERT_EQ(0, s.changes);
    episode(&s, 48);
    ASSERT_EQ(45, s.bp);
    // down and up across the wrap
    sim_t u;
    sim_init(&u, 0xFFFFFFFFu - 20000u, 53, 2);
    settle_in(&u);
    ASSERT_EQ(35, tk(&u, 0, 100));
    expect_up_after(&u, BT_LINK_UP_HOLD_MS);
    ASSERT_EQ(45, u.bp);
    ASSERT_TRUE(u.t < 100000u); // the clock did wrap
    ASSERT_EQ(0, u.early_ups);
}


TEST(test_phantom_acl_is_reset_once_and_only_without_a_real_link) {
    ASSERT_TRUE(bt_link_phantom_acl(0x0B, false, false));
    ASSERT_FALSE(bt_link_phantom_acl(0x0B, false, true));  // already reset for this attempt: fail as before
    ASSERT_FALSE(bt_link_phantom_acl(0x0B, true, false));  // we do hold a connection to it: a real link, not a phantom
    ASSERT_FALSE(bt_link_phantom_acl(0x04, false, false)); // page timeout
    ASSERT_FALSE(bt_link_phantom_acl(0x00, false, false));
    ASSERT_FALSE(bt_link_phantom_acl(0x0C, false, false)); // command disallowed: a different refusal
}

TEST(test_phantom_reset_is_rate_limited_per_peer_and_wrap_safe) {
    ASSERT_FALSE(bt_link_phantom_due(1000u, 1000u));
    ASSERT_FALSE(bt_link_phantom_due(1000u + BT_LINK_PHANTOM_REPEAT_MS - 1u, 1000u));
    ASSERT_TRUE(bt_link_phantom_due(1000u + BT_LINK_PHANTOM_REPEAT_MS, 1000u));
    ASSERT_TRUE(bt_link_phantom_due(5u, 0xFFFFFFFFu - BT_LINK_PHANTOM_REPEAT_MS - 10u)); // the clock wrapped between the two
    ASSERT_FALSE(bt_link_phantom_due(5u, 0xFFFFFFF0u));
    ASSERT_TRUE(BT_LINK_PHANTOM_BOUND_MS > 5000u); // longer than a power cycle (about 1.3 s + the patch download)
}

TEST(test_ending_an_attempt_drops_a_reset_only_before_the_power_off) {
    ASSERT_FALSE(bt_link_phantom_abandon_only(PH_NONE));
    ASSERT_FALSE(bt_link_phantom_abandon_only(PH_OFF)); // nothing done to the controller yet: drop the reset
    ASSERT_TRUE(bt_link_phantom_abandon_only(PH_DOWN)); // off issued: finish the cycle, skip the retry
    ASSERT_TRUE(bt_link_phantom_abandon_only(PH_ON));
}

int main(void) {
    RUN(test_catchup_budget_bounds_what_is_owed_and_counts_the_rest);
    RUN(test_pressure_is_a_long_wait_or_a_big_debt);
    RUN(test_ladder_follows_the_headsets_limits);
    RUN(test_the_first_stall_steps_down_on_the_tick_it_crosses_40_ms);
    RUN(test_a_severe_stall_jumps_to_the_last_level);
    RUN(test_a_long_stall_like_the_walk_lands_on_35);
    RUN(test_stall_episodes_are_rate_limited_by_the_settle_time);
    RUN(test_the_first_second_of_a_stream_ignores_the_start_up_burst);
    RUN(test_one_bad_window_steps_down_one_level);
    RUN(test_it_steps_up_only_after_a_long_clean_spell);
    RUN(test_a_stall_tick_or_a_middling_window_restarts_the_hold);
    RUN(test_a_quick_return_to_trouble_doubles_the_hold_up_to_a_cap);
    RUN(test_a_walk_with_bad_patches_changes_rarely_and_never_climbs_soon_after_a_fall);
    RUN(test_a_headset_without_room_to_step_never_changes);
    RUN(test_the_xq_ceiling_steps_to_35_on_a_severe_stall);
    RUN(test_the_clock_wraps);
    RUN(test_phantom_acl_is_reset_once_and_only_without_a_real_link);
    RUN(test_phantom_reset_is_rate_limited_per_peer_and_wrap_safe);
    RUN(test_ending_an_attempt_drops_a_reset_only_before_the_power_off);
    return RESULT();
}
