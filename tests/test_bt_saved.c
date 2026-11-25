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
static const uint8_t A_FLIP[6] = {0x70, 0x99, 0x1C, 0x0B, 0x55, 0x66};
static const uint8_t A_CAR[6] = {0x04, 0x52, 0xC7, 0x31, 0x32, 0x33};   // refuses to pair
static const uint8_t A_PHONE[6] = {0xF4, 0x0E, 0x22, 0x9A, 0x8B, 0x7C};  // authenticates, but offers no A2DP sink


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
    if (!bt_saved_pair_begin(d)) {
        return HAL_EBUSY;
    }
    hal_status_t st = hal_bt_pair(d);
    if (st != HAL_OK) {
        bt_saved_pair_abort();
    }
    return st;
}
static void do_tick(void) {
    hal_bt_poll();
    bt_saved_drain_stale();
}

// The link keys are stored sealed: read them through the same adapter BTstack (and the simulator) use.
static bool key_present(const uint8_t a[6]) {
    const hal_bt_storage_t *bt = settings_bt_storage();
    for (uint32_t i = 0; i < HAL_BT_KEY_SLOTS; i++) {
        uint8_t v[HAL_BT_KEY_RECORD_LEN];
        if (bt->get(bt->ctx, HAL_BT_KEY_TAG(i), v, sizeof v) >= (int)(HAL_BT_KEY_ADDR_OFFSET + 6u) && memcmp(v + HAL_BT_KEY_ADDR_OFFSET, a, 6) == 0) {
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

TEST(test_a_long_airpods_name_is_kept_whole) {
    // "Stuart's AirPods Pro - Find My" with U+2019: 34 bytes, longer than the old 32-byte cap.
    const char *name = "Stuart\xE2\x80\x99s AirPods Pro 3 - Find My";
    ASSERT_TRUE(strlen(name) > 32 && strlen(name) <= HAL_BT_NAME_MAX);
    settings_init(NULL);
    settings_attach_storage(true);
    (void)settings_load();
    hal_bt_device_t d = dev_of(A_AIRPODS, name, 0x240404), got;
    ASSERT_TRUE(bt_saved_put(&d));
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(strcmp(got.name, name) == 0); // the raw name stays stored; the UI normalises it for display
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
    // the documented worst case is about 41 tags with this record; fill 47 others and it still fits
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
    ASSERT_FALSE(bt_saved_peek_stale(stale)); // nothing was replaced
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
    ASSERT_TRUE(bt_saved_peek_stale(stale));
    ASSERT_TRUE(memcmp(stale, A_AIRPODS, 6) == 0);
    bt_saved_drop_stale();
    ASSERT_FALSE(bt_saved_peek_stale(stale)); // once
    ASSERT_EQ(HAL_OK, hal_bt_forget(stale));
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

// A fake BTstack link key record in the store (the layout of btstack_link_key_db_tlv.c).
static void put_key_record(uint32_t slot, const uint8_t addr[6]) {
    const hal_bt_storage_t *bt = settings_bt_storage();
    uint8_t rec[HAL_BT_KEY_RECORD_LEN] = {0};
    memcpy(rec + HAL_BT_KEY_ADDR_OFFSET, addr, 6);
    ASSERT_EQ(0, bt->put(bt->ctx, HAL_BT_KEY_TAG(slot), rec, sizeof rec));
}

// Runs before the radio is started: the stack is OFF, so Forget has no one to ask and must still delete the key.
TEST(test_forget_while_the_stack_is_down_deletes_straight_from_the_store) {
    fresh_card();
    settings_init(NULL);
    settings_attach_storage(true);
    ASSERT_EQ(SETTINGS_LOAD_FRESH, settings_load());
    ASSERT_EQ(HAL_BT_OFF, hal_bt_state());
    put_key_record(2, A_XM5);
    put_key_record(3, A_AIRPODS);
    ASSERT_TRUE(key_present(A_XM5));
    ASSERT_EQ(HAL_ENODEV, hal_bt_forget(A_XM5)); // the HAL says it cannot
    ASSERT_TRUE(key_present(A_XM5));
    ASSERT_EQ(HAL_OK, bt_saved_delete_key(A_XM5)); // the caller's fallback does it
    ASSERT_FALSE(key_present(A_XM5));
    ASSERT_TRUE(key_present(A_AIRPODS)); // only that one
    bt_saved_queue_delete(A_AIRPODS); // and through the queue the tick drains
    bt_saved_drain_stale();
    ASSERT_FALSE(key_present(A_AIRPODS));
    uint8_t a[6];
    ASSERT_FALSE(bt_saved_peek_stale(a));
}

static void fresh_store(void) {
    fresh_card();
    settings_init(NULL);
    settings_attach_storage(true);
    ASSERT_EQ(SETTINGS_LOAD_FRESH, settings_load());
}

TEST(test_store_full_when_the_headset_cannot_be_saved) {
    fresh_store();
    for (uint32_t i = 0; i < SETTINGS_MAX_ITEMS; i++) { // every slot taken
        ASSERT_TRUE(settings_put(SETTINGS_TAG('Z', 'Z', (char)('0' + i / 10), (char)('0' + i % 10)), "x", 1));
    }
    hal_bt_device_t d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404), got;
    ASSERT_EQ(HAL_OK, do_pair(&d));
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state()); // the radio side worked ...
    ASSERT_FALSE(bt_saved_get(&got));                       // ... but there was no room to remember it
    ASSERT_EQ(BT_SAVED_NOTICE_FULL, bt_saved_notice());
    uint8_t a[6];
    ASSERT_TRUE(bt_saved_peek_stale(a)); // its key is queued for deletion
    ASSERT_TRUE(memcmp(a, A_AIRPODS, 6) == 0);
    do_tick();
    ASSERT_EQ(HAL_BT_LINK_DISCONNECTED, hal_bt_link_state()); // and the link goes with it (it was the current peer)
    ASSERT_FALSE(key_present(A_AIRPODS));
    ASSERT_FALSE(bt_saved_peek_stale(a));
    // the next Pair clears the notice
    fresh_store();
    ASSERT_EQ(HAL_OK, do_pair(&d));
    ASSERT_EQ(BT_SAVED_NOTICE_NONE, bt_saved_notice());
    pump_link_idle(3000);
    ASSERT_TRUE(bt_saved_get(&got));
    hal_bt_disconnect();
}

TEST(test_two_stale_addresses_are_both_handled_in_one_tick) {
    fresh_store();
    hal_bt_device_t d, got;
    uint8_t a[6];
    d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404);
    ASSERT_EQ(HAL_OK, do_pair(&d));
    pump_link_idle(3000);
    hal_bt_disconnect();
    d = dev_of(A_XM5, "WH-1000XM5", 0x240404);
    ASSERT_EQ(HAL_OK, do_pair(&d)); // replaces the AirPods ...
    pump_link_idle(3000);
    hal_bt_disconnect();
    d = dev_of(A_FLIP, "Flip 6", 0x240414);
    ASSERT_EQ(HAL_OK, do_pair(&d)); // ... and this replaces the XM5, before any tick ran
    pump_link_idle(3000);
    hal_bt_disconnect();
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(memcmp(got.addr, A_FLIP, 6) == 0);
    ASSERT_TRUE(key_present(A_AIRPODS));
    ASSERT_TRUE(key_present(A_XM5));
    ASSERT_TRUE(bt_saved_peek_stale(a)); // two queued, oldest first
    ASSERT_TRUE(memcmp(a, A_AIRPODS, 6) == 0);
    do_tick();
    ASSERT_FALSE(key_present(A_AIRPODS));
    ASSERT_FALSE(key_present(A_XM5));
    ASSERT_TRUE(key_present(A_FLIP));
    ASSERT_FALSE(bt_saved_peek_stale(a));
}

TEST(test_a_failed_or_cancelled_pair_of_the_saved_headset_clears_its_record) {
    fresh_store();
    hal_bt_device_t d = dev_of(A_CAR, "Car audio", 0x200420), got;
    ASSERT_TRUE(bt_saved_put(&d)); // pretend the car kit was remembered
    ASSERT_EQ(HAL_OK, do_pair(&d)); // re-pairing it: BTstack drops the old key first, and the car kit refuses
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_FAIL_AUTH, hal_bt_link_reason());
    ASSERT_FALSE(bt_saved_get(&got)); // the record would name a headset without a key
    do_tick();

    d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404);
    ASSERT_EQ(HAL_OK, do_pair(&d));
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    hal_bt_disconnect();
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(key_present(A_AIRPODS));
    ASSERT_EQ(HAL_OK, do_pair(&d)); // cancelled re-pair of the remembered one
    hal_bt_poll();
    hal_bt_disconnect();
    ASSERT_FALSE(bt_saved_get(&got));
    ASSERT_FALSE(key_present(A_AIRPODS));
}

// A link event of an earlier attempt (a cancelled Pair finishing late) must not make the new candidate "remembered".
TEST(test_an_event_of_an_older_attempt_is_ignored) {
    fresh_store();
    hal_bt_device_t d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404), got;
    bt_saved_pair_begin(&d);
    hal_bt_event_t old = {.type = HAL_BT_EVT_LINK, .link = HAL_BT_LINK_CONNECTED, .attempt = hal_bt_link_attempt()};
    memcpy(old.dev.addr, A_AIRPODS, 6);
    bt_saved_on_event(&old); // the attempt before the one that is about to start
    ASSERT_FALSE(bt_saved_get(&got));
    old.link = HAL_BT_LINK_FAILED;
    bt_saved_on_event(&old); // and a late failure does not drop the candidate either
    ASSERT_EQ(HAL_OK, hal_bt_pair(&d));
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    ASSERT_TRUE(bt_saved_get(&got)); // the real attempt (a larger number) still counts
    hal_bt_disconnect();
}

// Read-only / RAM-only store: a Pair is accepted into RAM only, and says so; Forget must not drop the record or the queue entry.
TEST(test_pair_and_forget_with_a_store_that_is_not_writable) {
    fresh_card();
    settings_init(NULL);
    settings_attach_storage(false); // no usable card: RAM-only
    hal_bt_device_t d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404), got;
    settings_status_t st;
    settings_get_status(&st);
    ASSERT_FALSE(st.persistent);
    ASSERT_EQ(HAL_OK, do_pair(&d));
    pump_link_idle(3000);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    ASSERT_TRUE(bt_saved_get(&got)); // remembered for the session ...
    ASSERT_EQ(BT_SAVED_NOTICE_NOT_SAVED, bt_saved_notice()); // ... and the UI is told it will not survive
    // Forget: the key delete cannot be made durable, so it stays queued and the record stays
    ASSERT_EQ(HAL_EBUSY, bt_saved_delete_key(A_AIRPODS));
    bt_saved_queue_delete(A_AIRPODS);
    bt_saved_clear();
    ASSERT_TRUE(bt_saved_get(&got));
    do_tick();
    uint8_t a[6];
    ASSERT_TRUE(bt_saved_peek_stale(a));
    // the card becomes usable: the delete goes through and the record can go
    fresh_store();
    do_tick();
    ASSERT_FALSE(bt_saved_peek_stale(a));
    ASSERT_FALSE(key_present(A_AIRPODS));
    bt_saved_clear();
    ASSERT_FALSE(bt_saved_get(&got));
    hal_bt_disconnect();
}

// A second Pair cannot overwrite the candidate of the one in flight.
TEST(test_pair_begin_refuses_while_a_candidate_is_live) {
    fresh_store();
    hal_bt_device_t d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404), e = dev_of(A_XM5, "WH-1000XM5", 0x240404), got;
    ASSERT_TRUE(bt_saved_pair_begin(&d));
    ASSERT_FALSE(bt_saved_pair_begin(&e));
    ASSERT_EQ(HAL_OK, hal_bt_pair(&d));
    pump_link_idle(3000);
    ASSERT_TRUE(bt_saved_get(&got));
    ASSERT_TRUE(memcmp(got.addr, A_AIRPODS, 6) == 0); // the first candidate survived
    hal_bt_disconnect();
    ASSERT_TRUE(bt_saved_pair_begin(&e)); // and once it is settled a new one is accepted
    bt_saved_pair_abort();
}

// Forget on an unwritable store must not keep hitting the radio: a Pair of the same address afterwards survives the ticks, and a queued
// delete of another address goes through exactly once when the card becomes writable.
TEST(test_unwritable_forget_does_not_kill_a_later_pair) {
    fresh_card();
    settings_init(NULL);
    settings_attach_storage(false);
    hal_bt_device_t d = dev_of(A_AIRPODS, "AirPods Pro 3", 0x240404), got;
    ASSERT_TRUE(bt_saved_put(&d));
    bt_saved_queue_delete(A_AIRPODS); // what bt_service_forget does when the store is not writable
    bt_saved_queue_delete(A_XM5);     // another address waiting too
    for (int i = 0; i < 5; i++) {
        do_tick();
    }
    ASSERT_EQ(HAL_OK, do_pair(&d)); // Pair the same headset again: its queue entry goes
    for (int t = 0; t < 3000; t += 20) {
        do_tick(); // every UI frame
        hal_delay_ms(20);
        if (hal_bt_link_state() == HAL_BT_LINK_CONNECTED) {
            break;
        }
    }
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    ASSERT_TRUE(key_present(A_AIRPODS));
    ASSERT_TRUE(bt_saved_get(&got));
    uint8_t a[6];
    ASSERT_TRUE(bt_saved_peek_stale(a));
    ASSERT_TRUE(memcmp(a, A_XM5, 6) == 0); // only the other address is still queued
    fresh_store(); // the card becomes writable (a new store: A's key is gone with it, the point here is XM5)
    do_tick();
    ASSERT_FALSE(bt_saved_peek_stale(a)); // drained, once (the simulator knows no key for it, so there is nothing more to check)
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state()); // and the live link to the other headset was not touched
    hal_bt_disconnect();
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
    RUN(test_a_long_airpods_name_is_kept_whole);
    RUN(test_damaged_records_are_ignored);
    RUN(test_capacity_has_room_for_the_record);
    RUN(test_forget_while_the_stack_is_down_deletes_straight_from_the_store);
    RUN(test_only_a_pair_that_ended_connected_is_remembered);
    RUN(test_store_full_when_the_headset_cannot_be_saved);
    RUN(test_two_stale_addresses_are_both_handled_in_one_tick);
    RUN(test_a_failed_or_cancelled_pair_of_the_saved_headset_clears_its_record);
    RUN(test_an_event_of_an_older_attempt_is_ignored);
    RUN(test_pair_and_forget_with_a_store_that_is_not_writable);
    RUN(test_pair_begin_refuses_while_a_candidate_is_live);
    RUN(test_unwritable_forget_does_not_kill_a_later_pair);
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", abs);
    int rc = system(cmd);
    (void)rc;
    return RESULT();
}
