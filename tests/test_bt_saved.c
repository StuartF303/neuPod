// SPDX-License-Identifier: MPL-2.0
// The remembered Bluetooth headset (app/runtime/bt_saved.c) over the real settings store and the host Bluetooth simulator:
// record layout and damage handling, persistence across a "reboot", capacity, the rule that only a Pair that ended CONNECTED
// is remembered, replacement of the old headset (its key is cleaned up), no orphan keys, and the link keys reaching the SD-card store.
#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bt_saved.h"
#include "hal_radio.h"
#include "hal_system.h"
#include "settings_store.h"
#include "unit.h"

static char g_tpl[] = "neupod_btsaved_test_XXXXXX";

static const uint8_t A_AIRPODS[6] = {0x28, 0xCD, 0xC1, 0x01, 0x02, 0x03};
static const uint8_t A_XM5[6] = {0x00, 0x1B, 0x7C, 0x4A, 0x10, 0x22};
static const uint8_t A_CAR[6] = {0x04, 0x52, 0xC7, 0x31, 0x32, 0x33};   // refuses to pair
static const uint8_t A_PHONE[6] = {0xF4, 0x0E, 0x22, 0x9A, 0x8B, 0x7C};  // authenticates, but offers no A2DP sink

#define KEY_TAG(i) SETTINGS_TAG('B', 'T', 'L', '0' + (i))

static hal_bt_device_t dev_of(const uint8_t a[6], const char *name, uint32_t cod) {
    hal_bt_device_t d;
    memset(&d, 0, sizeof d);
    memcpy(d.addr, a, 6);
    d.cod = cod;
    d.audio_sink = ((cod >> 8) & 0x1Fu) == HAL_BT_COD_MAJOR_AUDIO_VIDEO;
    d.rssi = -50;
    strncpy(d.name, name, HAL_BT_NAME_MAX);
    return d;
}

static char g_root[200];

// A new card: no settings files.
static void fresh_card(void) {
    char p[260];
    snprintf(p, sizeof p, "%s/.neupod/settings.a", g_root);
    unlink(p);
    snprintf(p, sizeof p, "%s/.neupod/settings.b", g_root);
    unlink(p);
}

static void reboot_store(void) {
    ASSERT_EQ(0, settings_flush());
    settings_init(NULL);
    settings_attach_storage(true);
    ASSERT_EQ(SETTINGS_LOAD_OK, settings_load());
}

static void on_event(const hal_bt_event_t *ev, void *ctx) {
    (void)ctx;
    bt_saved_on_event(ev); // what bt_service does
}

static void pump_link_idle(uint32_t max_ms) {
    for (uint32_t t = 0; t < max_ms; t += 20) {
        hal_bt_poll();
        hal_delay_ms(20);
        hal_bt_link_state_t l = hal_bt_link_state();
        if (l != HAL_BT_LINK_PAIRING && l != HAL_BT_LINK_CONNECTING) {
            return;
        }
    }
}

// bt_service_pair() and bt_service_tick() minus the RTOS queue.
static hal_status_t do_pair(const hal_bt_device_t *d) {
    bt_saved_pair_begin(d);
    hal_status_t st = hal_bt_pair(d);
    if (st != HAL_OK) {
        bt_saved_pair_abort();
    }
    return st;
}
static void do_tick(void) {
    hal_bt_poll();
    uint8_t a[6];
    while (bt_saved_take_stale(a)) {
        hal_bt_forget(a);
    }
}

// The link keys are stored sealed: read them through the same adapter BTstack (and the simulator) use.
static bool key_present(const uint8_t a[6]) {
    const hal_bt_storage_t *bt = settings_bt_storage();
    for (int i = 0; i < 8; i++) {
        uint8_t v[8];
        if (bt->get(bt->ctx, KEY_TAG(i), v, sizeof v) == 6 && memcmp(v, a, 6) == 0) {
            return true;
        }
    }
    return false;
}

TEST(test_record_round_trip_and_reboot) {
    fresh_card();
    settings_init(NULL);
    settings_attach_storage(true);
    ASSERT_EQ(SETTINGS_LOAD_FRESH, settings_load());
    hal_bt_device_t got;
    ASSERT_FALSE(bt_saved_get(&got));

    hal_bt_device_t d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404);
    ASSERT_TRUE(bt_saved_put(&d));
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(memcmp(got.addr, A_AIRPODS, 6) == 0);
    ASSERT_EQ(0x240404, got.cod);
    ASSERT_TRUE(got.audio_sink);
    ASSERT_EQ(HAL_BT_RSSI_UNKNOWN, got.rssi);
    ASSERT_TRUE(strcmp(got.name, "AirPods Pro 3") == 0);

    reboot_store(); // power cycle: it is on the card
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(memcmp(got.addr, A_AIRPODS, 6) == 0);
    ASSERT_TRUE(strcmp(got.name, "AirPods Pro 3") == 0);

    // full-length name, empty name, replacement
    char longname[HAL_BT_NAME_MAX + 1];
    memset(longname, 'n', HAL_BT_NAME_MAX);
    longname[HAL_BT_NAME_MAX] = '\0';
    d = dev_of(A_XM5, longname, 0x240418);
    ASSERT_TRUE(bt_saved_put(&d));
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(strcmp(got.name, longname) == 0);
    ASSERT_TRUE(memcmp(got.addr, A_XM5, 6) == 0); // one headset: the new one replaced the old
    d = dev_of(A_XM5, "", 0x240418);
    ASSERT_TRUE(bt_saved_put(&d));
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_EQ(0, got.name[0]);
    ASSERT_EQ(BT_SAVED_MIN_LEN, settings_get(SETTING_BT_DEVICE, NULL, 0));

    d = dev_of((const uint8_t[6]){0}, "nobody", 0x240404);
    ASSERT_FALSE(bt_saved_put(&d)); // no address: not storable
    ASSERT_FALSE(bt_saved_put(NULL));

    bt_saved_clear();
    ASSERT_FALSE(bt_saved_get(&got));
}

TEST(test_damaged_records_are_ignored) {
    hal_bt_device_t got;
    uint8_t v[BT_SAVED_MAX_LEN + 4];
    memset(v, 0, sizeof v);
    v[0] = BT_SAVED_VERSION;
    memcpy(&v[1], A_AIRPODS, 6);
    v[7] = 0x04;
    v[8] = 0x04;
    v[9] = 0x24;

    ASSERT_TRUE(settings_put(SETTING_BT_DEVICE, v, BT_SAVED_MIN_LEN));
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(settings_put(SETTING_BT_DEVICE, v, BT_SAVED_MIN_LEN - 1)); // truncated
    ASSERT_FALSE(bt_saved_get(&got));
    v[0] = BT_SAVED_VERSION + 1; // a newer layout
    ASSERT_TRUE(settings_put(SETTING_BT_DEVICE, v, BT_SAVED_MIN_LEN));
    ASSERT_FALSE(bt_saved_get(&got));
    v[0] = BT_SAVED_VERSION;
    memset(&v[1], 0, 6); // empty address
    ASSERT_TRUE(settings_put(SETTING_BT_DEVICE, v, BT_SAVED_MIN_LEN));
    ASSERT_FALSE(bt_saved_get(&got));

    memcpy(&v[1], A_AIRPODS, 6);
    memcpy(&v[BT_SAVED_MIN_LEN], "a\x1b[2Jb", 6); // a name from a file carries no terminal escapes
    ASSERT_TRUE(settings_put(SETTING_BT_DEVICE, v, BT_SAVED_MIN_LEN + 6));
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(strcmp(got.name, "a?[2Jb") == 0);

    ASSERT_TRUE(settings_put(SETTING_BT_DEVICE, v, BT_SAVED_MAX_LEN + 1)); // longer than any record we write
    ASSERT_FALSE(bt_saved_get(&got));
    bt_saved_clear();
}

TEST(test_capacity_has_room_for_the_record) {
    fresh_card();
    settings_init(NULL);
    settings_attach_storage(true);
    ASSERT_EQ(SETTINGS_LOAD_FRESH, settings_load());
    // the documented worst case is about 39 tags with this record; fill 47 others and it still fits
    for (uint32_t i = 0; i < SETTINGS_MAX_ITEMS - 1u; i++) {
        ASSERT_TRUE(settings_put(SETTINGS_TAG('Z', 'Z', (char)('0' + i / 10), (char)('0' + i % 10)), "x", 1));
    }
    hal_bt_device_t d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404), got;
    ASSERT_TRUE(bt_saved_put(&d));
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(bt_saved_put(&d)); // replacing needs no new slot
    // a full store refuses a NEW record cleanly and keeps the rest
    bt_saved_clear();
    ASSERT_TRUE(settings_put(SETTINGS_TAG('Z', 'Z', '9', '9'), "x", 1));
    ASSERT_FALSE(bt_saved_put(&d));
    ASSERT_FALSE(bt_saved_get(&got));
    ASSERT_EQ(1, settings_get(SETTINGS_TAG('Z', 'Z', '0', '0'), &got, 1));
}

TEST(test_only_a_pair_that_ended_connected_is_remembered) {
    fresh_card();
    settings_init(NULL);
    settings_attach_storage(true);
    ASSERT_EQ(SETTINGS_LOAD_FRESH, settings_load());
    ASSERT_EQ(HAL_OK, hal_bt_init(on_event, NULL, settings_bt_storage()));
    for (int i = 0; i < 100 && hal_bt_state() != HAL_BT_ON; i++) {
        hal_bt_poll();
        hal_delay_ms(20);
    }
    ASSERT_EQ(HAL_BT_ON, hal_bt_state());
    hal_bt_device_t got, d;

    // 1. a refused Pair remembers nothing
    d = dev_of(A_CAR, "Car audio", 0x200420);
    ASSERT_EQ(HAL_OK, do_pair(&d));
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_LINK_FAILED, hal_bt_link_state());
    ASSERT_FALSE(bt_saved_get(&got));

    // 2. a cancelled Pair remembers nothing
    d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404);
    ASSERT_EQ(HAL_OK, do_pair(&d));
    hal_bt_poll();
    hal_bt_disconnect();
    ASSERT_FALSE(bt_saved_get(&got));
    ASSERT_FALSE(hal_bt_accepting());

    // 3. a Pair that gets as far as a key but finds no A2DP sink remembers nothing and leaves no key behind
    d = dev_of(A_PHONE, "Pixel 9", 0x5A020C);
    ASSERT_EQ(HAL_OK, do_pair(&d));
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_FAIL_NO_SINK, hal_bt_link_reason());
    ASSERT_TRUE(key_present(A_PHONE)); // the simulator, like the stack, made the key before the SDP check
    ASSERT_FALSE(bt_saved_get(&got));
    do_tick(); // the orphan key is cleaned up
    ASSERT_FALSE(key_present(A_PHONE));

    // 4. a Pair that ends CONNECTED is remembered, with its key on the card
    d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404);
    ASSERT_EQ(HAL_OK, do_pair(&d));
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(memcmp(got.addr, A_AIRPODS, 6) == 0);
    ASSERT_TRUE(strcmp(got.name, "AirPods Pro 3") == 0);
    ASSERT_EQ(0x240404, got.cod);
    ASSERT_TRUE(key_present(A_AIRPODS));
    uint8_t stale[6];
    ASSERT_FALSE(bt_saved_take_stale(stale)); // nothing was replaced
    hal_bt_disconnect();

    // 5. the record and the key survive a reboot of the store
    reboot_store();
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(memcmp(got.addr, A_AIRPODS, 6) == 0);
    ASSERT_TRUE(key_present(A_AIRPODS));

    // 6. a failed Pair of ANOTHER device leaves the remembered headset alone
    d = dev_of(A_CAR, "Car audio", 0x200420);
    ASSERT_EQ(HAL_OK, do_pair(&d));
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_LINK_FAILED, hal_bt_link_state());
    do_tick();
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(memcmp(got.addr, A_AIRPODS, 6) == 0);
    ASSERT_TRUE(key_present(A_AIRPODS));
    ASSERT_EQ(HAL_OK, hal_bt_connect(A_AIRPODS)); // Connect on the remembered one still works
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    hal_bt_disconnect();

    // 7. pairing a second headset replaces the first and its key is deleted (one headset to begin with)
    d = dev_of(A_XM5, "WH-1000XM5", 0x240404);
    ASSERT_EQ(HAL_OK, do_pair(&d));
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(memcmp(got.addr, A_XM5, 6) == 0);
    ASSERT_TRUE(bt_saved_take_stale(stale));
    ASSERT_TRUE(memcmp(stale, A_AIRPODS, 6) == 0);
    ASSERT_FALSE(bt_saved_take_stale(stale)); // once
    hal_bt_forget(stale);
    ASSERT_FALSE(key_present(A_AIRPODS));
    ASSERT_TRUE(key_present(A_XM5));
    hal_bt_disconnect();

    // 8. forget (bt_service_forget): the key and the record go; Connect finds nothing to connect
    hal_bt_forget(A_XM5);
    bt_saved_clear();
    ASSERT_FALSE(bt_saved_get(&got));
    ASSERT_FALSE(key_present(A_XM5));
    ASSERT_EQ(HAL_OK, hal_bt_connect(A_XM5));
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_FAIL_AUTH, hal_bt_link_reason()); // and a stale remote record could not reconnect without pairing

    // 9. a Pair that hal_bt_pair() refuses leaves no candidate that a later event could promote
    d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404);
    ASSERT_EQ(HAL_OK, hal_bt_inquiry_start(10));
    ASSERT_EQ(HAL_EBUSY, do_pair(&d));
    hal_bt_inquiry_stop();
    hal_bt_event_t ev = {.type = HAL_BT_EVT_LINK, .link = HAL_BT_LINK_CONNECTED};
    memcpy(ev.dev.addr, A_AIRPODS, 6);
    bt_saved_on_event(&ev);
    ASSERT_FALSE(bt_saved_get(&got));
}

int main(void) {
    if (mkdtemp(g_tpl) == NULL) {
        perror("mkdtemp");
        return 2;
    }
    char abs[200];
    if (realpath(g_tpl, abs) == NULL) {
        return 2;
    }
    setenv("NEUPOD_FS_ROOT", abs, 1);
    snprintf(g_root, sizeof g_root, "%s", abs);
    RUN(test_record_round_trip_and_reboot);
    RUN(test_damaged_records_are_ignored);
    RUN(test_capacity_has_room_for_the_record);
    RUN(test_only_a_pair_that_ended_connected_is_remembered);
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", abs);
    int rc = system(cmd);
    (void)rc;
    return RESULT();
}
