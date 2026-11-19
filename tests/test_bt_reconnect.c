// SPDX-License-Identifier: MPL-2.0
// Automatic reconnection (app/runtime/bt_reconnect.c): the schedule, what holds it back, what ends it, and a run against the host Bluetooth simulator.
#include <string.h>

#include "bt_reconnect.h"
#include "hal_host.h"
#include "hal_radio.h"
#include "hal_system.h"
#include "unit.h"

static const bt_rc_in_t IDLE = {.bt_on = true, .have_saved = true, .link = HAL_BT_LINK_FAILED};

TEST(test_schedule) {
    ASSERT_EQ(2000, bt_rc_delay_ms(0));
    ASSERT_EQ(5000, bt_rc_delay_ms(1));
    ASSERT_EQ(10000, bt_rc_delay_ms(2));
    ASSERT_EQ(30000, bt_rc_delay_ms(3));
    ASSERT_EQ(30000, bt_rc_delay_ms(40));
}

TEST(test_after_a_loss_the_attempts_follow_the_schedule_and_stop_after_five_minutes) {
    bt_rc_t rc;
    bt_rc_init(&rc);
    ASSERT_FALSE(bt_rc_step(&rc, 100, &IDLE)); // nothing armed
    uint32_t t = 1000;
    bt_rc_lost(&rc, t);
    ASSERT_TRUE(bt_rc_active(&rc));
    ASSERT_FALSE(bt_rc_step(&rc, t + 1999, &IDLE));
    ASSERT_TRUE(bt_rc_step(&rc, t + 2000, &IDLE));
    bt_rc_attempt_made(&rc);
    bt_rc_in_t busy = IDLE;
    busy.link = HAL_BT_LINK_CONNECTING;
    ASSERT_FALSE(bt_rc_step(&rc, t + 2500, &busy)); // running
    uint32_t at = t + 22000; // the HAL timer ended it (20 s)
    ASSERT_FALSE(bt_rc_step(&rc, at, &IDLE)); // noticed: the next one is 5 s on
    ASSERT_TRUE(bt_rc_active(&rc));
    ASSERT_FALSE(bt_rc_step(&rc, at + 4999, &IDLE));
    ASSERT_TRUE(bt_rc_step(&rc, at + 5000, &IDLE));
    bt_rc_attempt_made(&rc);
    at += 5000 + 20000;
    ASSERT_FALSE(bt_rc_step(&rc, at, &IDLE));
    ASSERT_FALSE(bt_rc_step(&rc, at + 9999, &IDLE));
    ASSERT_TRUE(bt_rc_step(&rc, at + 10000, &IDLE)); // then 10 s
    bt_rc_attempt_made(&rc);
    at += 10000 + 20000;
    ASSERT_FALSE(bt_rc_step(&rc, at, &IDLE));
    ASSERT_FALSE(bt_rc_step(&rc, at + 29999, &IDLE));
    ASSERT_TRUE(bt_rc_step(&rc, at + 30000, &IDLE)); // then every 30 s
    bt_rc_attempt_made(&rc);
    // five minutes after the loss: no more
    ASSERT_FALSE(bt_rc_step(&rc, t + BT_RC_GIVE_UP_MS, &IDLE));
    ASSERT_FALSE(bt_rc_active(&rc));
}

TEST(test_it_waits_for_a_scan_and_for_someone_elses_connection_but_ends_on_user_action_or_forget) {
    bt_rc_t rc;
    bt_rc_init(&rc);
    bt_rc_lost(&rc, 0);
    bt_rc_in_t scanning = IDLE;
    scanning.bt_on = false; // a scan (or the radio down)
    ASSERT_FALSE(bt_rc_step(&rc, 5000, &scanning));
    ASSERT_TRUE(bt_rc_active(&rc)); // held, not dropped
    bt_rc_in_t pairing = IDLE;
    pairing.link = HAL_BT_LINK_PAIRING;
    ASSERT_FALSE(bt_rc_step(&rc, 5000, &pairing));
    pairing.link = HAL_BT_LINK_CONNECTING; // the user's own Connect
    ASSERT_FALSE(bt_rc_step(&rc, 5000, &pairing));
    ASSERT_TRUE(bt_rc_step(&rc, 5000, &IDLE)); // free again: it goes at once, it was due
    bt_rc_user_action(&rc);
    ASSERT_FALSE(bt_rc_active(&rc));
    ASSERT_FALSE(bt_rc_step(&rc, 999999, &IDLE));
    bt_rc_lost(&rc, 0);
    bt_rc_in_t gone = IDLE;
    gone.have_saved = false; // forgotten
    ASSERT_FALSE(bt_rc_step(&rc, 5000, &gone));
    ASSERT_FALSE(bt_rc_active(&rc));
}

TEST(test_power_up_tries_at_once_and_a_connection_ends_it) {
    bt_rc_t rc;
    bt_rc_init(&rc);
    bt_rc_powerup(&rc, 700);
    ASSERT_TRUE(bt_rc_step(&rc, 700, &IDLE));
    bt_rc_attempt_made(&rc);
    bt_rc_in_t up = IDLE;
    up.link = HAL_BT_LINK_CONNECTED;
    ASSERT_FALSE(bt_rc_step(&rc, 1500, &up));
    ASSERT_FALSE(bt_rc_active(&rc));
    // a refused call counts as a failed attempt
    bt_rc_powerup(&rc, 0);
    ASSERT_TRUE(bt_rc_step(&rc, 0, &IDLE));
    bt_rc_attempt_refused(&rc, 0);
    ASSERT_FALSE(bt_rc_step(&rc, 1999, &IDLE));
    ASSERT_TRUE(bt_rc_step(&rc, 2000, &IDLE));
    // connected by hand while waiting
    bt_rc_lost(&rc, 0);
    ASSERT_FALSE(bt_rc_step(&rc, 100, &up));
    ASSERT_FALSE(bt_rc_active(&rc));
}

TEST(test_clock_wrap) {
    bt_rc_t rc;
    bt_rc_init(&rc);
    bt_rc_lost(&rc, 0xFFFFFF00u);
    ASSERT_FALSE(bt_rc_step(&rc, 0xFFFFFF00u + 1999u, &IDLE));
    ASSERT_TRUE(bt_rc_step(&rc, 0xFFFFFF00u + 2000u, &IDLE)); // across 2^32
}

// ---- against the simulator: the headset walks away and comes back ----

static void noop(const hal_bt_event_t *e, void *c) {
    (void)e;
    (void)c;
}

TEST(test_it_reconnects_a_simulated_headset_that_dropped) {
    static const uint8_t A[6] = {0x28, 0xCD, 0xC1, 0x01, 0x02, 0x03};
    ASSERT_EQ(HAL_OK, hal_bt_init(noop, NULL, NULL));
    for (int i = 0; i < 100 && hal_bt_state() != HAL_BT_ON; i++) {
        hal_bt_poll();
        hal_delay_ms(20);
    }
    hal_bt_device_t d;
    memset(&d, 0, sizeof d);
    memcpy(d.addr, A, 6);
    ASSERT_EQ(HAL_OK, hal_bt_pair(&d));
    for (int i = 0; i < 200 && hal_bt_link_state() != HAL_BT_LINK_CONNECTED; i++) {
        hal_bt_poll();
        hal_delay_ms(20);
    }
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    hal_host_bt_drop_link();
    ASSERT_EQ(HAL_BT_LINK_FAILED, hal_bt_link_state());
    ASSERT_EQ(HAL_BT_FAIL_LINK_LOST, hal_bt_link_reason());
    bt_rc_t rc;
    bt_rc_init(&rc);
    uint32_t now = 0;
    bt_rc_lost(&rc, now);
    bool made = false;
    for (int i = 0; i < 400 && hal_bt_link_state() != HAL_BT_LINK_CONNECTED; i++) {
        now += 20; // the policy runs on its own clock; the simulator's Connect takes 0.5 s of real time
        hal_bt_poll();
        hal_delay_ms(20);
        bt_rc_in_t in = {.bt_on = hal_bt_state() == HAL_BT_ON, .have_saved = true, .link = hal_bt_link_state()};
        if (bt_rc_step(&rc, now, &in)) {
            ASSERT_EQ(HAL_OK, hal_bt_connect(A));
            bt_rc_attempt_made(&rc);
            made = true;
        }
    }
    ASSERT_TRUE(made);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    bt_rc_in_t in = {.bt_on = true, .have_saved = true, .link = HAL_BT_LINK_CONNECTED};
    ASSERT_FALSE(bt_rc_step(&rc, now + 20, &in));
    ASSERT_FALSE(bt_rc_active(&rc));
}

TEST(test_play_tries_at_once_twice_at_most_and_then_gives_up) {
    bt_rc_t rc;
    bt_rc_init(&rc);
    bt_rc_play(&rc, 1000);
    ASSERT_TRUE(bt_rc_active(&rc));
    ASSERT_TRUE(bt_rc_step(&rc, 1000, &IDLE)); // the first attempt is at once
    bt_rc_attempt_made(&rc);
    bt_rc_in_t busy = IDLE;
    busy.link = HAL_BT_LINK_CONNECTING;
    ASSERT_FALSE(bt_rc_step(&rc, 1500, &busy));
    uint32_t at = 21000; // the HAL's connect timer ended it (20 s)
    ASSERT_FALSE(bt_rc_step(&rc, at, &IDLE)); // noticed: the second one is 2 s on
    ASSERT_TRUE(bt_rc_active(&rc));
    ASSERT_FALSE(bt_rc_step(&rc, at + 1999, &IDLE));
    ASSERT_TRUE(bt_rc_step(&rc, at + 2000, &IDLE));
    bt_rc_attempt_made(&rc);
    ASSERT_FALSE(bt_rc_step(&rc, at + 2500, &busy));
    at += 22000;
    ASSERT_FALSE(bt_rc_step(&rc, at, &IDLE)); // the second failed too: that was the last
    ASSERT_FALSE(bt_rc_active(&rc));
    ASSERT_FALSE(bt_rc_step(&rc, at + 40000, &IDLE)); // and no background retry follows
}

TEST(test_play_counts_a_refused_call_as_an_attempt_and_a_connection_ends_it) {
    bt_rc_t rc;
    bt_rc_init(&rc);
    bt_rc_play(&rc, 0);
    ASSERT_TRUE(bt_rc_step(&rc, 0, &IDLE));
    bt_rc_attempt_refused(&rc, 0); // the radio would not take the call
    ASSERT_TRUE(bt_rc_active(&rc));
    ASSERT_TRUE(bt_rc_step(&rc, 2000, &IDLE));
    bt_rc_attempt_refused(&rc, 2000);
    ASSERT_FALSE(bt_rc_active(&rc)); // two attempts, both refused
    bt_rc_play(&rc, 5000);
    ASSERT_TRUE(bt_rc_step(&rc, 5000, &IDLE));
    bt_rc_attempt_made(&rc);
    bt_rc_in_t up = IDLE;
    up.link = HAL_BT_LINK_CONNECTED;
    ASSERT_FALSE(bt_rc_step(&rc, 5600, &up));
    ASSERT_FALSE(bt_rc_active(&rc)); // connected: done
}

TEST(test_play_joins_a_running_schedule_without_restarting_it_or_limiting_it) {
    bt_rc_t rc;
    bt_rc_init(&rc);
    bt_rc_lost(&rc, 1000); // first attempt due at 3000
    bt_rc_play(&rc, 2000);
    ASSERT_FALSE(bt_rc_step(&rc, 2999, &IDLE)); // not pulled forward
    ASSERT_TRUE(bt_rc_step(&rc, 3000, &IDLE));  // and not pushed back
    bt_rc_attempt_made(&rc);
    ASSERT_FALSE(bt_rc_step(&rc, 23000, &IDLE)); // failed: 5 s next, as the loss schedule says
    ASSERT_FALSE(bt_rc_step(&rc, 27999, &IDLE));
    ASSERT_TRUE(bt_rc_step(&rc, 28000, &IDLE));
    bt_rc_attempt_made(&rc);
    ASSERT_FALSE(bt_rc_step(&rc, 48000, &IDLE));
    ASSERT_TRUE(bt_rc_active(&rc)); // not ended after two: the loss schedule runs its five minutes
    ASSERT_FALSE(bt_rc_step(&rc, 1000 + BT_RC_GIVE_UP_MS, &IDLE));
    ASSERT_FALSE(bt_rc_active(&rc));
    // joining a power-up schedule is the same
    bt_rc_powerup(&rc, 0);
    bt_rc_play(&rc, 10);
    ASSERT_TRUE(bt_rc_step(&rc, 10, &IDLE));
    bt_rc_attempt_made(&rc);
    ASSERT_FALSE(bt_rc_step(&rc, 20000, &IDLE));
    ASSERT_TRUE(bt_rc_active(&rc));
}

TEST(test_play_waits_for_a_scan_and_a_user_action_ends_it) {
    bt_rc_t rc;
    bt_rc_init(&rc);
    bt_rc_play(&rc, 0);
    bt_rc_in_t scanning = IDLE;
    scanning.bt_on = false;
    ASSERT_FALSE(bt_rc_step(&rc, 50, &scanning)); // never a connect while scanning
    ASSERT_TRUE(bt_rc_active(&rc));
    ASSERT_TRUE(bt_rc_step(&rc, 100, &IDLE));
    bt_rc_user_action(&rc);
    ASSERT_FALSE(bt_rc_active(&rc));
    ASSERT_FALSE(bt_rc_step(&rc, 200, &IDLE));
    // a later loss schedule is not limited by the earlier play one
    bt_rc_lost(&rc, 1000);
    ASSERT_TRUE(bt_rc_step(&rc, 3000, &IDLE));
    bt_rc_attempt_made(&rc);
    ASSERT_FALSE(bt_rc_step(&rc, 23000, &IDLE));
    ASSERT_TRUE(bt_rc_active(&rc));
    ASSERT_TRUE(bt_rc_step(&rc, 28000, &IDLE));
    bt_rc_attempt_made(&rc);
    ASSERT_FALSE(bt_rc_step(&rc, 48000, &IDLE));
    ASSERT_TRUE(bt_rc_active(&rc));
}

static bool g_called;
static bool take_call(void *ctx) {
    (void)ctx;
    g_called = true;
    return true;
}

TEST(test_service_ends_a_play_schedule_with_the_wait_but_not_a_power_up_one_that_replaced_it) {
    bt_rc_t rc;
    bt_rc_init(&rc);
    uint32_t seen = 0;
    g_called = false;
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 1000, &IDLE, true, 1, &seen, take_call, NULL)); // a Play asks: its schedule starts and makes the first call
    ASSERT_TRUE(g_called);
    ASSERT_EQ(BT_RC_PLAY_ATTEMPTS, rc.max_attempts);
    bt_rc_powerup(&rc, 1500); // the power-up retry takes over before the wait ends
    g_called = false;
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 1500, &IDLE, false, 1, &seen, take_call, NULL)); // nobody waits any more
    ASSERT_TRUE(g_called);                                                                          // it went on with its own first attempt
    ASSERT_TRUE(bt_rc_active(&rc));
    // a play schedule on its own does end with the wait
    bt_rc_init(&rc);
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 2000, &IDLE, true, 2, &seen, take_call, NULL));
    ASSERT_TRUE(bt_rc_active(&rc));
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 2100, &IDLE, false, 2, &seen, take_call, NULL));
    ASSERT_FALSE(bt_rc_active(&rc));
}

TEST(test_service_joins_a_running_schedule_and_reports_how_a_schedule_ended) {
    bt_rc_t rc;
    bt_rc_init(&rc);
    uint32_t seen = 0;
    bt_rc_lost(&rc, 0);
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 100, &IDLE, true, 1, &seen, take_call, NULL)); // the Play joins the loss schedule: nothing changes
    ASSERT_EQ(0, rc.max_attempts);
    ASSERT_EQ(0, rc.failed);
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 200, &IDLE, false, 1, &seen, take_call, NULL)); // and the end of the wait leaves it running
    ASSERT_TRUE(bt_rc_active(&rc));
    bt_rc_in_t up = IDLE;
    up.link = HAL_BT_LINK_CONNECTED;
    ASSERT_EQ(BT_RC_ENDED_BACK, bt_rc_service(&rc, 300, &up, false, 1, &seen, take_call, NULL));
    bt_rc_init(&rc);
    seen = 0;
    bt_rc_play(&rc, 1000);
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 1000, &IDLE, true, 0, &seen, take_call, NULL)); // first attempt made
    bt_rc_in_t busy = IDLE;
    busy.link = HAL_BT_LINK_CONNECTING;
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 1500, &busy, true, 0, &seen, take_call, NULL));
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 21000, &IDLE, true, 0, &seen, take_call, NULL)); // it failed: the second is due 2 s on
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 23000, &IDLE, true, 0, &seen, take_call, NULL));
    ASSERT_EQ(BT_RC_ENDED_NONE, bt_rc_service(&rc, 23500, &busy, true, 0, &seen, take_call, NULL));
    ASSERT_EQ(BT_RC_ENDED_AWAY, bt_rc_service(&rc, 43000, &IDLE, true, 0, &seen, take_call, NULL)); // both used up, the headset is still away
}

int main(void) {
    RUN(test_play_tries_at_once_twice_at_most_and_then_gives_up);
    RUN(test_play_counts_a_refused_call_as_an_attempt_and_a_connection_ends_it);
    RUN(test_play_joins_a_running_schedule_without_restarting_it_or_limiting_it);
    RUN(test_play_waits_for_a_scan_and_a_user_action_ends_it);
    RUN(test_schedule);
    RUN(test_after_a_loss_the_attempts_follow_the_schedule_and_stop_after_five_minutes);
    RUN(test_it_waits_for_a_scan_and_for_someone_elses_connection_but_ends_on_user_action_or_forget);
    RUN(test_power_up_tries_at_once_and_a_connection_ends_it);
    RUN(test_clock_wrap);
    RUN(test_service_ends_a_play_schedule_with_the_wait_but_not_a_power_up_one_that_replaced_it);
    RUN(test_service_joins_a_running_schedule_and_reports_how_a_schedule_ended);
    RUN(test_it_reconnects_a_simulated_headset_that_dropped);
    return RESULT();
}
