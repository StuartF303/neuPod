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

// Run the policy for @p ms in 8 ms ticks with a fixed verdict (@p bad_percent of the ticks pressured). Returns the last change (0 if none); counts changes.
static uint8_t run(bt_link_policy_t *p, uint32_t *t, uint32_t ms, unsigned bad_percent, int *changes) {
    uint8_t last = 0;
    for (uint32_t end = *t + ms; (int32_t)(end - *t) > 0; *t += 8u) {
        const unsigned n = (unsigned)((*t / 8u) % 100u);
        uint8_t r = bt_link_policy_tick(p, *t, n < bad_percent, 53, 2);
        if (r != 0) {
            last = r;
            (*changes)++;
        }
    }
    return last;
}

TEST(test_one_stall_is_not_sustained_but_two_bad_seconds_step_down) {
    bt_link_policy_t p;
    uint32_t t = 1000;
    bt_link_policy_init(&p, t);
    int changes = 0;
    ASSERT_EQ(0, run(&p, &t, 3000, 0, &changes));
    ASSERT_EQ(0, run(&p, &t, 1100, 100, &changes)); // one bad window
    ASSERT_EQ(0, run(&p, &t, 3000, 0, &changes));   // clean again: the count restarts
    ASSERT_EQ(0, run(&p, &t, 1100, 100, &changes));
    ASSERT_EQ(0, run(&p, &t, 2000, 0, &changes));
    ASSERT_EQ(0, changes);
    ASSERT_EQ(45, run(&p, &t, 2300, 100, &changes)); // two in a row
    ASSERT_EQ(1, changes);
}

TEST(test_sustained_pressure_walks_down_to_the_last_step_and_stays) {
    bt_link_policy_t p;
    uint32_t t = 0;
    bt_link_policy_init(&p, t);
    int changes = 0;
    uint8_t last = 0, cur = 53;
    for (int i = 0; i < 30; i++) {
        uint8_t r = run(&p, &t, 1000, 60, &changes);
        if (r != 0) {
            ASSERT_TRUE(r < cur); // only downward under sustained pressure
            cur = r;
            last = r;
        }
    }
    ASSERT_EQ(2, changes);
    ASSERT_EQ(35, last);
}

TEST(test_it_steps_back_up_after_a_clean_spell_not_before) {
    bt_link_policy_t p;
    uint32_t t = 0;
    bt_link_policy_init(&p, t);
    int changes = 0;
    ASSERT_EQ(45, run(&p, &t, 2300, 100, &changes));
    // clean: no change until the hold is over (15 s from the last bad window)
    ASSERT_EQ(0, run(&p, &t, BT_LINK_UP_HOLD_MS - 2000u, 0, &changes));
    ASSERT_EQ(53, run(&p, &t, 4000, 0, &changes));
    ASSERT_EQ(2, changes);
}

TEST(test_a_middling_link_neither_steps_down_nor_up) {
    bt_link_policy_t p;
    uint32_t t = 0;
    bt_link_policy_init(&p, t);
    int changes = 0;
    ASSERT_EQ(45, run(&p, &t, 2300, 100, &changes));
    ASSERT_EQ(0, run(&p, &t, 60000, 10, &changes)); // 10 % pressured: not bad (25), not clean (5)
    ASSERT_EQ(1, changes);
}

TEST(test_a_quick_return_to_pressure_lengthens_the_up_hold) {
    bt_link_policy_t p;
    uint32_t t = 0;
    bt_link_policy_init(&p, t);
    int changes = 0;
    ASSERT_EQ(45, run(&p, &t, 2300, 100, &changes));
    ASSERT_EQ(53, run(&p, &t, BT_LINK_UP_HOLD_MS + 2000u, 0, &changes));
    ASSERT_EQ(45, run(&p, &t, 2300, 100, &changes)); // soon after the step up: the hold doubles to 30 s
    ASSERT_EQ(0, run(&p, &t, BT_LINK_UP_HOLD_MS + 2000u, 0, &changes));
    ASSERT_EQ(53, run(&p, &t, BT_LINK_UP_HOLD_MS + 2000u, 0, &changes));
    ASSERT_EQ(4, changes);
}

TEST(test_a_headset_without_room_to_step_never_changes) {
    bt_link_policy_t p;
    uint32_t t = 0;
    bt_link_policy_init(&p, t);
    int changes = 0;
    for (int i = 0; i < 20; i++) {
        for (int k = 0; k < 125; k++, t += 8u) {
            if (bt_link_policy_tick(&p, t, true, 40, 40) != 0) {
                changes++;
            }
        }
    }
    ASSERT_EQ(0, changes);
}

TEST(test_the_clock_wraps) {
    bt_link_policy_t p;
    uint32_t t = 0xFFFFFFFFu - 5000u;
    bt_link_policy_init(&p, t);
    int changes = 0;
    ASSERT_EQ(45, run(&p, &t, 2300, 100, &changes));
    ASSERT_EQ(53, run(&p, &t, BT_LINK_UP_HOLD_MS + 2000u, 0, &changes));
    ASSERT_EQ(2, changes);
}

int main(void) {
    RUN(test_catchup_budget_bounds_what_is_owed_and_counts_the_rest);
    RUN(test_pressure_is_a_long_wait_or_a_big_debt);
    RUN(test_ladder_follows_the_headsets_limits);
    RUN(test_one_stall_is_not_sustained_but_two_bad_seconds_step_down);
    RUN(test_sustained_pressure_walks_down_to_the_last_step_and_stays);
    RUN(test_it_steps_back_up_after_a_clean_spell_not_before);
    RUN(test_a_middling_link_neither_steps_down_nor_up);
    RUN(test_a_quick_return_to_pressure_lengthens_the_up_hold);
    RUN(test_a_headset_without_room_to_step_never_changes);
    RUN(test_the_clock_wraps);
    return RESULT();
}
