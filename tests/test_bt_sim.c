// SPDX-License-Identifier: MPL-2.0
// Host simulation of the Bluetooth HAL (hal/host/hal_radio.c): bring-up, scan events, audio-sink flag, busy/stop rules.
#include <string.h>

#include "hal_radio.h"
#include "hal_system.h"
#include "unit.h"

static int g_states, g_devices, g_sinks, g_named, g_done;
static hal_bt_state_t g_last_state;

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

int main(void) {
    RUN(test_bring_up_and_scan);
    RUN(test_stop_ends_the_scan);
    return RESULT();
}
