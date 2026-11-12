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
static hal_bt_stream_state_t g_last_stream;
static hal_bt_fail_t g_last_stream_reason;
static hal_bt_codec_t g_last_codec;
static hal_bt_stream_state_t g_sseq[16];
static int g_nsseq;
static bool g_accepting_at_event; // hal_bt_accepting() as seen from inside the last link event
static int g_volumes, g_keys;     // HAL_BT_EVT_VOLUME / HAL_BT_EVT_KEY events
static uint8_t g_last_volume;
static hal_bt_key_t g_last_key;

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
    case HAL_BT_EVT_STREAM:
        g_last_stream = ev->stream;
        g_last_stream_reason = ev->reason;
        g_last_codec = ev->codec;
        if (g_nsseq < 16) {
            g_sseq[g_nsseq++] = ev->stream;
        }
        break;
    case HAL_BT_EVT_VOLUME:
        g_volumes++;
        g_last_volume = ev->volume;
        break;
    case HAL_BT_EVT_KEY:
        g_keys++;
        g_last_key = ev->key;
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

// ---- the audio stream (stage 2b) ----

static uint32_t g_ring_frames; // frames "in the ring" of the fake PCM source
static uint32_t src_available(void *c) { (void)c; return g_ring_frames; }
static uint32_t src_capacity(void *c) { (void)c; return 4096; }
static uint32_t src_read(void *c, int16_t *d, uint32_t n) {
    (void)c;
    uint32_t g = n < g_ring_frames ? n : g_ring_frames;
    memset(d, 0, g * 4u);
    g_ring_frames -= g;
    return g;
}
static void src_flush(void *c) { (void)c; g_ring_frames = 0; }
static const hal_bt_pcm_source_t k_src = {NULL, src_available, src_capacity, src_read, src_flush, NULL};

static void pump_stream(hal_bt_stream_state_t want, uint32_t max_ms) {
    for (uint32_t t = 0; t < max_ms && hal_bt_stream_state() != want; t += 20) {
        hal_bt_poll();
        hal_delay_ms(20);
    }
}

TEST(test_the_stream_opens_by_itself_and_reports_its_codec) {
    hal_bt_set_pcm_source(&k_src);
    g_nsseq = 0;
    ASSERT_EQ(HAL_BT_STREAM_CLOSED, hal_bt_stream_state());
    ASSERT_EQ(HAL_ENODEV, hal_bt_stream_open()); // no link
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_AIRPODS, "AirPods Pro 3"));
    ASSERT_EQ(HAL_BT_STREAM_OPENING, hal_bt_stream_state());
    hal_bt_codec_t c;
    ASSERT_FALSE(hal_bt_stream_codec(&c)); // not negotiated yet
    pump_stream(HAL_BT_STREAM_OPEN, 2000);
    ASSERT_EQ(HAL_BT_STREAM_OPEN, hal_bt_stream_state());
    ASSERT_TRUE(hal_bt_stream_codec(&c));
    ASSERT_EQ(HAL_BT_CODEC_SBC, c.codec);
    ASSERT_EQ(44100, (int)c.rate);
    char l[16];
    hal_bt_codec_label(&c, l, sizeof l);
    ASSERT_TRUE(strcmp(l, "SBC 44.1") == 0);
    ASSERT_EQ(HAL_BT_CODEC_SBC, g_last_codec.codec);
    ASSERT_EQ(HAL_EBUSY, hal_bt_stream_open()); // one stream
    ASSERT_EQ(2, g_nsseq); // OPENING, OPEN
}

TEST(test_start_consumes_pcm_at_the_real_rate_and_counts_underruns) {
    ASSERT_EQ(HAL_OK, hal_bt_stream_start());
    pump_stream(HAL_BT_STREAM_STREAMING, 2000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
    hal_bt_stats_t st0, st1;
    hal_bt_stats(&st0);
    g_ring_frames = 4096;
    for (int i = 0; i < 10; i++) { // 200 ms: 8820 frames wanted, 4096 there: the ring runs dry
        hal_bt_poll();
        hal_delay_ms(20);
    }
    hal_bt_stats(&st1);
    ASSERT_EQ(0, (int)g_ring_frames);
    ASSERT_TRUE(st1.underruns > st0.underruns);
    ASSERT_TRUE(st1.frames_encoded >= 30 && st1.frames_encoded <= 33); // 4096 / 128 = 32
    hal_bt_stream_suspend();
    ASSERT_EQ(HAL_BT_STREAM_SUSPENDED, hal_bt_stream_state());
    g_ring_frames = 1000;
    for (int i = 0; i < 5; i++) {
        hal_bt_poll();
        hal_delay_ms(20);
    }
    ASSERT_EQ(1000, (int)g_ring_frames); // nothing is consumed while suspended
    ASSERT_EQ(HAL_OK, hal_bt_stream_start()); // resume
    pump_stream(HAL_BT_STREAM_STREAMING, 2000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
}

TEST(test_a_dropped_link_closes_the_stream_first) {
    g_nsseq = 0;
    g_nseq = 0;
    hal_host_bt_drop_link();
    ASSERT_EQ(HAL_BT_STREAM_CLOSED, hal_bt_stream_state());
    ASSERT_EQ(HAL_BT_STREAM_CLOSED, g_last_stream);
    ASSERT_EQ(HAL_BT_LINK_FAILED, hal_bt_link_state());
    ASSERT_EQ(HAL_ENODEV, hal_bt_stream_start());
    // a clean disconnect and a headset that releases the stream by itself
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_AIRPODS, "AirPods Pro 3"));
    pump_stream(HAL_BT_STREAM_OPEN, 2000);
    hal_host_bt_stream_dropped_by_headset();
    ASSERT_EQ(HAL_BT_STREAM_CLOSED, hal_bt_stream_state());
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state()); // the link stays up
    ASSERT_EQ(HAL_OK, hal_bt_stream_open());               // and the stream can be opened again
    pump_stream(HAL_BT_STREAM_OPEN, 2000);
    ASSERT_EQ(HAL_BT_STREAM_OPEN, hal_bt_stream_state());
    hal_bt_disconnect();
    ASSERT_EQ(HAL_BT_STREAM_CLOSED, hal_bt_stream_state());
}

TEST(test_the_high_bitpool_headset_reports_sbc_xq_and_an_opening_can_fail) {
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_XM5, "WH-1000XM5"));
    pump_stream(HAL_BT_STREAM_OPEN, 2000);
    hal_bt_codec_t c;
    ASSERT_TRUE(hal_bt_stream_codec(&c));
    ASSERT_EQ(HAL_BT_CODEC_SBC_XQ, c.codec);
    char l[16];
    hal_bt_codec_label(&c, l, sizeof l);
    ASSERT_TRUE(strcmp(l, "SBC-XQ") == 0);
    hal_bt_disconnect();
    hal_host_bt_fail_next_stream(HAL_BT_FAIL_STREAM);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_AIRPODS, "AirPods Pro 3"));
    pump_stream(HAL_BT_STREAM_FAILED, 2000);
    ASSERT_EQ(HAL_BT_STREAM_FAILED, hal_bt_stream_state());
    ASSERT_EQ(HAL_BT_FAIL_STREAM, hal_bt_stream_reason());
    ASSERT_EQ(HAL_BT_FAIL_STREAM, g_last_stream_reason);
    ASSERT_TRUE(strlen(hal_bt_fail_text(HAL_BT_FAIL_STREAM)) <= 19 && strlen(hal_bt_fail_text(HAL_BT_FAIL_CODEC)) <= 19);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state()); // the link itself is fine
    ASSERT_EQ(HAL_OK, hal_bt_stream_open());               // retry
    pump_stream(HAL_BT_STREAM_OPEN, 2000);
    ASSERT_EQ(HAL_BT_STREAM_OPEN, hal_bt_stream_state());
    ASSERT_EQ(HAL_BT_FAIL_NONE, hal_bt_stream_reason());
}

// ---- AVRCP: volume, keys, track information (stage 2c) ----

static void pump_ms(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 20) {
        hal_bt_poll();
        hal_delay_ms(20);
    }
}

TEST(test_absolute_volume_is_reported_after_connect_and_set_by_the_app) {
    hal_bt_disconnect();
    ASSERT_FALSE(hal_bt_volume_supported());
    ASSERT_EQ(-1, hal_bt_volume_get());
    ASSERT_EQ(HAL_ENODEV, hal_bt_volume_set(10)); // nothing to set
    g_volumes = 0;
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_AIRPODS, "AirPods Pro 3"));
    ASSERT_FALSE(hal_bt_volume_supported()); // AVRCP is not up the moment the link is
    pump_ms(400);
    ASSERT_TRUE(hal_bt_volume_supported());
    ASSERT_EQ(80, hal_bt_volume_get());
    ASSERT_EQ(1, g_volumes);
    ASSERT_EQ(80, g_last_volume);
    // our set: get() follows at once, the headset's answer is a second event
    uint32_t sets = hal_host_bt_volume_sets();
    ASSERT_EQ(HAL_OK, hal_bt_volume_set(200)); // clamped to 127
    ASSERT_EQ(127, hal_bt_volume_get());
    ASSERT_EQ(sets + 1, hal_host_bt_volume_sets());
    pump_ms(100);
    ASSERT_EQ(2, g_volumes);
    ASSERT_EQ(127, g_last_volume);
    // a swipe on the stem
    hal_host_bt_headset_volume(23);
    ASSERT_EQ(23, hal_bt_volume_get());
    ASSERT_EQ(23, g_last_volume);
    // the link goes: so does the volume
    hal_host_bt_drop_link();
    ASSERT_FALSE(hal_bt_volume_supported());
    ASSERT_EQ(-1, hal_bt_volume_get());
    ASSERT_EQ(HAL_ENODEV, hal_bt_volume_set(1));
    // the headset that was left at 0 %: the first report is 0, and it still counts as supported
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_XM5, "WH-1000XM5"));
    pump_ms(400);
    ASSERT_TRUE(hal_bt_volume_supported());
    ASSERT_EQ(0, hal_bt_volume_get());
    hal_bt_disconnect();
}

TEST(test_a_headset_without_absolute_volume_never_reports_one) {
    static const uint8_t A_FLIP[6] = {0x70, 0x99, 0x1C, 0x0B, 0x55, 0x66};
    g_volumes = 0;
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_FLIP, "Flip 6")); // the speaker has none
    pump_ms(500);
    ASSERT_FALSE(hal_bt_volume_supported());
    ASSERT_EQ(-1, hal_bt_volume_get());
    ASSERT_EQ(0, g_volumes);
    hal_host_bt_headset_volume(10); // ignored
    ASSERT_EQ(0, g_volumes);
    hal_bt_disconnect();
    hal_host_bt_set_no_absolute_volume(true); // the same for any headset on request
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_AIRPODS, "AirPods Pro 3"));
    pump_ms(500);
    ASSERT_FALSE(hal_bt_volume_supported());
    hal_bt_disconnect();
    hal_host_bt_set_no_absolute_volume(false);
}

TEST(test_headset_keys_arrive_as_events_only_on_a_connected_link) {
    g_keys = 0;
    hal_host_bt_headset_key(HAL_BT_KEY_NEXT); // no link
    ASSERT_EQ(0, g_keys);
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, pair_to_end(A_AIRPODS, "AirPods Pro 3"));
    hal_host_bt_headset_key(HAL_BT_KEY_PLAY_PAUSE);
    ASSERT_EQ(1, g_keys);
    ASSERT_EQ(HAL_BT_KEY_PLAY_PAUSE, g_last_key);
    hal_host_bt_headset_key(HAL_BT_KEY_PREV);
    ASSERT_EQ(HAL_BT_KEY_PREV, g_last_key);
    hal_bt_disconnect();
}

TEST(test_track_information_is_kept_and_cut_on_a_character_boundary) {
    hal_bt_track_t t;
    memset(&t, 0, sizeof t);
    t.track_id = 7;
    hal_bt_meta_copy(t.title, sizeof t.title, "Sigur R\xC3\xB3s - Hopp\xC3\xADpolla");
    t.duration_ms = 4000;
    t.status = HAL_BT_PLAY_PLAYING;
    uint32_t calls0 = 0;
    (void)hal_host_bt_track_info(NULL, &calls0);
    ASSERT_TRUE(hal_bt_track_info(&t));
    hal_bt_track_t got;
    uint32_t calls = 0;
    ASSERT_TRUE(hal_host_bt_track_info(&got, &calls));
    ASSERT_EQ(calls0 + 1, calls);
    ASSERT_EQ(7, got.track_id);
    ASSERT_TRUE(strcmp(got.title, t.title) == 0);
    ASSERT_TRUE(hal_bt_track_info(NULL));
    ASSERT_FALSE(hal_host_bt_track_info(NULL, NULL));
    // the cut: 4-byte buffer ("a" + a two-byte character that does not fit whole)
    char b[3];
    hal_bt_meta_copy(b, sizeof b, "a\xC3\xA9z"); // a, e-acute, z: 'a' + the first byte of the accent would be 2 bytes: cut before the accent
    ASSERT_TRUE(strcmp(b, "a") == 0);
    char c[4];
    hal_bt_meta_copy(c, sizeof c, "a\xC3\xA9z");
    ASSERT_TRUE(strcmp(c, "a\xC3\xA9") == 0);
    char d[2];
    hal_bt_meta_copy(d, sizeof d, "\xE2\x82\xAC"); // a three-byte character into one byte: nothing
    ASSERT_TRUE(d[0] == '\0');
    char e[8];
    hal_bt_meta_copy(e, sizeof e, "short");
    ASSERT_TRUE(strcmp(e, "short") == 0);
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
    RUN(test_the_stream_opens_by_itself_and_reports_its_codec);
    RUN(test_start_consumes_pcm_at_the_real_rate_and_counts_underruns);
    RUN(test_a_dropped_link_closes_the_stream_first);
    RUN(test_the_high_bitpool_headset_reports_sbc_xq_and_an_opening_can_fail);
    RUN(test_absolute_volume_is_reported_after_connect_and_set_by_the_app);
    RUN(test_a_headset_without_absolute_volume_never_reports_one);
    RUN(test_headset_keys_arrive_as_events_only_on_a_connected_link);
    RUN(test_track_information_is_kept_and_cut_on_a_character_boundary);
    return RESULT();
}
