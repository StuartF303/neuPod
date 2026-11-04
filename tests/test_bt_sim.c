// SPDX-License-Identifier: MPL-2.0
// Host simulation of the Bluetooth HAL (hal/host/hal_radio.c): bring-up, scan events, audio-sink flag, busy/stop rules, and (stage 2a)
// the pairing / connection state machine: Pair, Connect, Disconnect, Forget, every failure reason, the discoverable-only-while-pairing rule.
#include <string.h>

#include "hal_host.h"
#include "hal_radio.h"
#include "hal_system.h"
#include "unit.h"

static int g_states, g_devices, g_sinks, g_named, g_done;
static hal_bt_state_t g_last_state;
static hal_bt_link_state_t g_last_link;
static hal_bt_fail_t g_last_reason;
static hal_bt_link_state_t g_seq[16]; // the link states in the order they were reported
static int g_nseq;
static bool g_accepting_at_event; // hal_bt_accepting() as seen from inside the last link event

static void on_event(const hal_bt_event_t *ev, void *ctx) {
    (void)ctx;
    switch (ev->type) {
    case HAL_BT_EVT_STATE:
        g_states++;
        g_last_state = ev->state;
        break;
    case HAL_BT_EVT_DEVICE:
        g_devices++;
        g_sinks += ev->dev.audio_sink;
        g_named += ev->dev.name[0] != '\0';
        break;
    case HAL_BT_EVT_SCAN_DONE:
        g_done++;
        break;
    case HAL_BT_EVT_LINK:
        g_last_link = ev->link;
        g_last_reason = ev->reason;
        g_accepting_at_event = hal_bt_accepting();
        if (g_nseq < 16) {
            g_seq[g_nseq++] = ev->link;
        }
        break;
    }
}

static void pump_until(hal_bt_state_t want, uint32_t max_ms) {
    for (uint32_t t = 0; t < max_ms && hal_bt_state() != want; t += 20) {
        hal_bt_poll();
        hal_delay_ms(20);
    }
}

TEST(test_bring_up_and_scan) {
    uint8_t a[6];
    ASSERT_EQ(HAL_BT_OFF, hal_bt_state());
    ASSERT_EQ(HAL_ENODEV, hal_bt_inquiry_start(10)); // not up yet
    ASSERT_FALSE(hal_bt_local_address(a));
    ASSERT_EQ(HAL_OK, hal_bt_init(on_event, NULL, NULL));
    ASSERT_EQ(HAL_BT_STARTING, hal_bt_state());
    ASSERT_EQ(HAL_EBUSY, hal_bt_init(on_event, NULL, NULL));
    pump_until(HAL_BT_ON, 2000);
    ASSERT_EQ(HAL_BT_ON, hal_bt_state());
    ASSERT_TRUE(hal_bt_local_address(a));

    ASSERT_EQ(HAL_OK, hal_bt_inquiry_start(10));
    ASSERT_EQ(HAL_EBUSY, hal_bt_inquiry_start(10)); // one scan at a time
    ASSERT_EQ(HAL_BT_SCANNING, hal_bt_state());
    pump_until(HAL_BT_ON, 6000);
    ASSERT_EQ(HAL_BT_ON, hal_bt_state());
    ASSERT_EQ(1, g_done);
    ASSERT_TRUE(g_sinks >= 4); // headsets, a speaker and the car kit (reported twice: before and after its name arrives)
    ASSERT_TRUE(g_devices >= 7);
    ASSERT_TRUE(g_named >= 5);
}

TEST(test_stop_ends_the_scan) {
    g_done = 0;
    ASSERT_EQ(HAL_OK, hal_bt_inquiry_start(10));
    hal_bt_poll();
    hal_bt_inquiry_stop();
    ASSERT_EQ(HAL_BT_ON, hal_bt_state());
    ASSERT_EQ(1, g_done);
    hal_bt_inquiry_stop(); // idle: no second "done"
    ASSERT_EQ(1, g_done);
}

/* ---- pairing and the connection (stage 2a) ---- */

static const uint8_t A_AIRPODS[6] = {0x28, 0xCD, 0xC1, 0x01, 0x02, 0x03};
static const uint8_t A_XM5[6] = {0x00, 0x1B, 0x7C, 0x4A, 0x10, 0x22};
static const uint8_t A_CAR[6] = {0x04, 0x52, 0xC7, 0x31, 0x32, 0x33};   // refuses to pair
static const uint8_t A_PHONE[6] = {0xF4, 0x0E, 0x22, 0x9A, 0x8B, 0x7C};  // no A2DP sink
static const uint8_t A_SILENT[6] = {0x3C, 0x22, 0xFB, 0xDE, 0xAD, 0x01}; // does not answer
static const uint8_t A_UNKNOWN[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

static hal_bt_device_t dev_of(const uint8_t a[6], const char *name) {
    hal_bt_device_t d;
    memset(&d, 0, sizeof d);
    memcpy(d.addr, a, 6);
    d.cod = 0x240404;
    d.audio_sink = true;
    d.rssi = -50;
    strncpy(d.name, name, HAL_BT_NAME_MAX);
    return d;
}

static void pump_link(hal_bt_link_state_t want, uint32_t max_ms) {
    for (uint32_t t = 0; t < max_ms && hal_bt_link_state() != want; t += 20) {
        hal_bt_poll();
        hal_delay_ms(20);
    }
}

// Run a Pair to its end; returns the final link state (-1 if it was refused).
static int pair_to_end(const uint8_t a[6], const char *name) {
    hal_bt_device_t d = dev_of(a, name);
    g_nseq = 0;
    if (hal_bt_pair(&d) != HAL_OK) {
        return -1;
    }
    for (uint32_t t = 0; t < 3000 && hal_bt_link_state() == HAL_BT_LINK_PAIRING; t += 20) {
        hal_bt_poll();
        hal_delay_ms(20);
    }
    return (int)hal_bt_link_state();
}

TEST(test_pair_connects_and_is_discoverable_only_meanwhile) {
    hal_bt_device_t d = dev_of(A_AIRPODS, "AirPods Pro 3"), got;
    ASSERT_EQ(HAL_BT_LINK_DISCONNECTED, hal_bt_link_state());
    ASSERT_FALSE(hal_bt_accepting()); // stage 1's rule: nobody can find or connect to us
    ASSERT_FALSE(hal_bt_link_device(&got));
    g_nseq = 0;
    ASSERT_EQ(HAL_OK, hal_bt_pair(&d));
    ASSERT_EQ(HAL_BT_LINK_PAIRING, hal_bt_link_state());
    ASSERT_TRUE(hal_bt_accepting()); // only while the Pair action runs
    ASSERT_TRUE(g_accepting_at_event);
    ASSERT_TRUE(hal_bt_link_device(&got));
    ASSERT_TRUE(memcmp(got.addr, A_AIRPODS, 6) == 0);
    ASSERT_TRUE(strcmp(got.name, "AirPods Pro 3") == 0);
    // one link at a time
    ASSERT_EQ(HAL_EBUSY, hal_bt_pair(&d));
    ASSERT_EQ(HAL_EBUSY, hal_bt_connect(A_XM5));
    ASSERT_EQ(HAL_EBUSY, hal_bt_inquiry_start(10)); // an inquiry would starve the link set-up
    pump_link(HAL_BT_LINK_CONNECTED, 3000);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    ASSERT_EQ(HAL_BT_FAIL_NONE, hal_bt_link_reason());
    ASSERT_FALSE(hal_bt_accepting()); // off again as soon as the Pair is over
    ASSERT_FALSE(g_accepting_at_event);
    ASSERT_EQ(2, g_nseq);
    ASSERT_EQ(HAL_BT_LINK_PAIRING, g_seq[0]);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, g_seq[1]);
    ASSERT_EQ(HAL_EBUSY, hal_bt_pair(&d)); // connected: disconnect first
    ASSERT_EQ(HAL_BT_ON, hal_bt_state());
    hal_bt_disconnect();
    ASSERT_EQ(HAL_BT_LINK_DISCONNECTED, hal_bt_link_state());
    ASSERT_EQ(HAL_BT_LINK_DISCONNECTED, g_last_link);
    ASSERT_FALSE(hal_bt_link_device(&got));
    hal_bt_disconnect(); // idle: nothing more
    ASSERT_EQ(3, g_nseq);
}

TEST(test_pair_failures_report_their_reason_and_allow_a_retry) {
    ASSERT_EQ(HAL_BT_LINK_FAILED, pair_to_end(A_CAR, "Car audio"));
    ASSERT_EQ(HAL_BT_FAIL_AUTH, hal_bt_link_reason());
    ASSERT_EQ(HAL_BT_FAIL_AUTH, g_last_reason);
    ASSERT_FALSE(hal_bt_accepting());
    hal_bt_device_t peer;
    ASSERT_TRUE(hal_bt_link_device(&peer)); // FAILED still says whose failure it is
    ASSERT_TRUE(memcmp(peer.addr, A_CAR, 6) == 0);

    ASSERT_EQ(HAL_BT_LINK_FAILED, pair_to_end(A_PHONE, "Pixel 9")); // retry straight from FAILED
    ASSERT_EQ(HAL_BT_FAIL_NO_SINK, hal_bt_link_reason());
    ASSERT_EQ(HAL_BT_LINK_FAILED, pair_to_end(A_SILENT, ""));
    ASSERT_EQ(HAL_BT_FAIL_NOT_FOUND, hal_bt_link_reason());
    ASSERT_EQ(HAL_BT_LINK_FAILED, pair_to_end(A_UNKNOWN, "Nothing"));
    ASSERT_EQ(HAL_BT_FAIL_NOT_FOUND, hal_bt_link_reason());
    ASSERT_EQ(2, g_nseq); // the last attempt: PAIRING, FAILED
    ASSERT_EQ(HAL_BT_LINK_FAILED, g_seq[1]);

    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_XM5, "WH-1000XM5")); // and then a good one works
    ASSERT_EQ(HAL_BT_FAIL_NONE, hal_bt_link_reason());                  // the reason is cleared
    hal_bt_disconnect();
}

TEST(test_cancel_is_not_a_failure) {
    hal_bt_device_t d = dev_of(A_AIRPODS, "AirPods Pro 3");
    g_nseq = 0;
    ASSERT_EQ(HAL_OK, hal_bt_pair(&d));
    hal_bt_poll();
    hal_bt_disconnect();
    ASSERT_EQ(HAL_BT_LINK_DISCONNECTED, hal_bt_link_state());
    ASSERT_EQ(HAL_BT_FAIL_NONE, hal_bt_link_reason());
    ASSERT_FALSE(hal_bt_accepting());
    ASSERT_EQ(2, g_nseq);
    ASSERT_EQ(HAL_BT_LINK_DISCONNECTED, g_seq[1]); // PAIRING then DISCONNECTED, never FAILED
    for (int i = 0; i < 100; i++) {                // the cancelled attempt does not come back to life
        hal_bt_poll();
        hal_delay_ms(20);
    }
    ASSERT_EQ(HAL_BT_LINK_DISCONNECTED, hal_bt_link_state());
    ASSERT_EQ(2, g_nseq);
}

TEST(test_connect_needs_the_key_and_forget_removes_it) {
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_AIRPODS, "AirPods Pro 3"));
    hal_bt_disconnect();
    // Connect to a paired device: CONNECTING, then CONNECTED; never discoverable
    g_nseq = 0;
    ASSERT_EQ(HAL_OK, hal_bt_connect(A_AIRPODS));
    ASSERT_EQ(HAL_BT_LINK_CONNECTING, hal_bt_link_state());
    ASSERT_FALSE(hal_bt_accepting());
    pump_link(HAL_BT_LINK_CONNECTED, 3000);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    ASSERT_EQ(HAL_BT_LINK_CONNECTING, g_seq[0]);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, g_seq[1]);
    // Forgetting the current peer drops the link and the key
    hal_bt_forget(A_AIRPODS);
    ASSERT_EQ(HAL_BT_LINK_DISCONNECTED, hal_bt_link_state());
    ASSERT_EQ(HAL_OK, hal_bt_connect(A_AIRPODS));
    pump_link(HAL_BT_LINK_FAILED, 3000);
    ASSERT_EQ(HAL_BT_LINK_FAILED, hal_bt_link_state());
    ASSERT_EQ(HAL_BT_FAIL_AUTH, hal_bt_link_reason()); // no key any more: Connect never pairs by itself
    // never paired (or forgotten: the earlier test paired the XM5)
    hal_bt_forget(A_XM5);
    ASSERT_EQ(HAL_OK, hal_bt_connect(A_XM5));
    pump_link(HAL_BT_LINK_FAILED, 3000);
    ASSERT_EQ(HAL_BT_LINK_FAILED, hal_bt_link_state());
    ASSERT_EQ(HAL_BT_FAIL_AUTH, hal_bt_link_reason());
    // paired, disconnected, connected again
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_XM5, "WH-1000XM5"));
    hal_bt_disconnect();
    hal_bt_forget(A_UNKNOWN); // never paired: harmless
    ASSERT_EQ(HAL_OK, hal_bt_connect(A_XM5));
    pump_link(HAL_BT_LINK_CONNECTED, 3000);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    hal_bt_disconnect();
}

TEST(test_link_loss_is_a_failure_with_its_reason) {
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_AIRPODS, "AirPods Pro 3"));
    hal_host_bt_drop_link();
    ASSERT_EQ(HAL_BT_LINK_FAILED, hal_bt_link_state());
    ASSERT_EQ(HAL_BT_FAIL_LINK_LOST, hal_bt_link_reason());
    ASSERT_EQ(HAL_BT_FAIL_LINK_LOST, g_last_reason);
    hal_host_bt_drop_link(); // nothing connected: no second event
    ASSERT_EQ(3, g_nseq);
}

TEST(test_the_headset_hanging_up_is_not_a_failure_when_connected_but_ends_a_setup) {
    // connected, nothing open: the headset hangs up (0x13 on the board): DISCONNECTED, no failure
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_AIRPODS, "AirPods Pro 3"));
    hal_host_bt_remote_hangup();
    ASSERT_EQ(HAL_BT_LINK_DISCONNECTED, hal_bt_link_state());
    ASSERT_EQ(HAL_BT_FAIL_NONE, hal_bt_link_reason());
    ASSERT_EQ(HAL_BT_LINK_DISCONNECTED, g_last_link);
    // a link that dies abnormally is still a failure
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_AIRPODS, "AirPods Pro 3"));
    hal_host_bt_drop_link();
    ASSERT_EQ(HAL_BT_FAIL_LINK_LOST, hal_bt_link_reason());
    // dropped during a Pair: the attempt ends at once and the Pair window closes (no waiting for the 30 s timer)
    hal_bt_device_t d = dev_of(A_XM5, "WH-1000XM5");
    ASSERT_EQ(HAL_OK, hal_bt_pair(&d));
    ASSERT_TRUE(hal_bt_accepting());
    hal_bt_poll();
    hal_host_bt_remote_hangup();
    ASSERT_EQ(HAL_BT_LINK_FAILED, hal_bt_link_state());
    ASSERT_EQ(HAL_BT_FAIL_BUSY, hal_bt_link_reason());
    ASSERT_FALSE(hal_bt_accepting());
    hal_bt_forget(A_XM5);
    ASSERT_EQ(HAL_OK, hal_bt_connect(A_AIRPODS)); // dropped during a Connect
    hal_host_bt_remote_hangup();
    ASSERT_EQ(HAL_BT_FAIL_NOT_FOUND, hal_bt_link_reason());
    ASSERT_EQ(HAL_BT_LINK_FAILED, hal_bt_link_state());
}

TEST(test_bad_requests_and_scan_exclusion) {
    hal_bt_device_t d = dev_of(A_AIRPODS, "x");
    ASSERT_EQ(HAL_EINVAL, hal_bt_pair(NULL));
    ASSERT_EQ(HAL_EINVAL, hal_bt_connect(NULL));
    hal_bt_device_t z;
    memset(&z, 0, sizeof z);
    ASSERT_EQ(HAL_EINVAL, hal_bt_pair(&z));
    static const uint8_t zero[6];
    ASSERT_EQ(HAL_EINVAL, hal_bt_connect(zero));
    ASSERT_EQ(HAL_OK, hal_bt_inquiry_start(10)); // a scan blocks a link set-up, as the other way round
    ASSERT_EQ(HAL_EBUSY, hal_bt_pair(&d));
    ASSERT_EQ(HAL_EBUSY, hal_bt_connect(A_AIRPODS));
    hal_bt_inquiry_stop();
    ASSERT_EQ(HAL_BT_ON, hal_bt_state());
}

int main(void) {
    RUN(test_bring_up_and_scan);
    RUN(test_stop_ends_the_scan);
    RUN(test_pair_connects_and_is_discoverable_only_meanwhile);
    RUN(test_pair_failures_report_their_reason_and_allow_a_retry);
    RUN(test_cancel_is_not_a_failure);
    RUN(test_connect_needs_the_key_and_forget_removes_it);
    RUN(test_link_loss_is_a_failure_with_its_reason);
    RUN(test_the_headset_hanging_up_is_not_a_failure_when_connected_but_ends_a_setup);
    RUN(test_bad_requests_and_scan_exclusion);
    return RESULT();
}
