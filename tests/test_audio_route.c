// SPDX-License-Identifier: MPL-2.0
// Audio routing (app/runtime/audio_route.c) over the host Bluetooth simulator, the real settings store and the real Bluetooth ring, with fakes
// for the audio core commands and the player: Auto / Wired, the quiet change of output, pause on link loss with the output staying on
// Bluetooth, start / suspend of the stream, and the notices for the UI.
#include <string.h>

#include "audio_route.h"
#include "bt_pcm.h"
#include "bt_reconnect.h"
#include "bt_saved.h"
#include "hal_host.h"
#include "hal_radio.h"
#include "hal_system.h"
#include "player.h"
#include "settings_store.h"
#include "unit.h"

// ---- fakes ----
static uint32_t g_now = 100000;
uint32_t os_tick_ms(void) { return g_now; }

static char g_calls[256]; // the audio core commands, in order: "C" close, "B" / "W" output, "O" open
static int g_ncalls;
static void call(char c) {
    if (g_ncalls < (int)sizeof g_calls - 1) {
        g_calls[g_ncalls++] = c;
        g_calls[g_ncalls] = '\0';
    }
}
bool audio_close(void) { call('C'); return true; }
bool audio_set_output(audio_output_t o, uint32_t rate) { call(o == AUDIO_OUT_BT ? 'B' : 'W'); return rate == 44100u; }
bool audio_open(uint32_t r, uint8_t c, uint8_t b) { (void)r; (void)c; (void)b; call('O'); return true; }

static player_state_t g_pstate = PLAYER_IDLE;
static int g_stops, g_pauses;
static output_path_t g_path;
void player_get(player_info_t *o) {
    memset(o, 0, sizeof *o);
    o->state = g_pstate;
}
void player_stop(void) { g_stops++; g_pstate = PLAYER_IDLE; }
void player_pause(void) { g_pauses++; if (g_pstate == PLAYER_PLAYING) g_pstate = PLAYER_PAUSED; }
void player_set_output_path(output_path_t p) { g_path = p; }
static bool g_vol_locked;
void player_volume_set_locked(bool l) { g_vol_locked = l; }
bool player_volume_locked(void) { return g_vol_locked; }

// ---- helpers ----

// What bt_service.c's reconnect_tick() does for a Play, through the same bt_rc_service() (bt_service.c itself needs the RTOS, the control queue and the event plumbing):
// the route's connect request is taken in, the play schedule runs over the simulator, and the route is told when it gave up.
static bt_rc_t g_rc;
static uint32_t g_play_seen;
static bool test_connect(void *ctx) {
    (void)ctx;
    hal_bt_device_t d;
    return bt_saved_get(&d) && hal_bt_connect(d.addr) == HAL_OK;
}
static void svc_step(void) {
    const bt_rc_in_t in = {.bt_on = hal_bt_state() == HAL_BT_ON, .have_saved = bt_saved_get(NULL), .link = hal_bt_link_state()};
    const bt_rc_end_t end = bt_rc_service(&g_rc, g_now, &in, audio_route_holding(), audio_route_connect_request_seq(), &g_play_seen, test_connect, NULL);
    if (end == BT_RC_ENDED_AWAY && audio_route_holding()) {
        audio_route_connect_gave_up();
    }
}

static void tick(uint32_t ms) {
    g_now += ms;
    hal_delay_ms(ms > 50 ? 50 : ms); // the simulator runs on the real clock
    hal_bt_poll();
    svc_step();
    audio_route_service();
}

static void pump_stream(hal_bt_stream_state_t want, uint32_t max_ms) {
    for (uint32_t t = 0; t < max_ms && hal_bt_stream_state() != want; t += 50) {
        tick(50);
    }
}

static const uint8_t A_AIRPODS[6] = {0x28, 0xCD, 0xC1, 0x01, 0x02, 0x03};
static bool g_inited;

static void no_event(const hal_bt_event_t *e, void *c) { // (hal_bt_power() needs a registered callback)
    (void)e;
    (void)c;
}

static void connect_headset(void) {
    if (!g_inited) {
        g_inited = true;
        settings_init(NULL);
        ASSERT_EQ(HAL_OK, hal_bt_init(no_event, NULL, NULL));
        hal_bt_set_pcm_source(bt_pcm_source());
        for (int i = 0; i < 40 && hal_bt_state() != HAL_BT_ON; i++) {
            hal_delay_ms(20);
            hal_bt_poll();
        }
    }
    hal_bt_device_t d;
    memset(&d, 0, sizeof d);
    memcpy(d.addr, A_AIRPODS, 6);
    d.cod = 0x240404;
    d.audio_sink = true;
    strcpy(d.name, "AirPods Pro 3");
    ASSERT_EQ(HAL_OK, hal_bt_pair(&d));
    for (int i = 0; i < 100 && hal_bt_link_state() != HAL_BT_LINK_CONNECTED; i++) {
        hal_delay_ms(20);
        hal_bt_poll();
    }
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
}

static void reset(void) {
    settings_delete(SETTING_BT_DEVICE); // no remembered headset unless a test remembers one
    bt_rc_init(&g_rc);
    g_play_seen = audio_route_connect_request_seq();
    hal_host_bt_set_unreachable(false);
    hal_host_bt_ignore_starts(0);
    hal_bt_disconnect();
    hal_bt_poll();
    // a fresh router: leave Bluetooth the way the user does (Disconnect), then settle
    audio_route_user_released();
    for (int i = 0; i < 10; i++) tick(100);
    g_ncalls = 0;
    g_calls[0] = '\0';
    g_pstate = PLAYER_IDLE;
    g_stops = g_pauses = 0;
    audio_route_set_pref(AUDIO_ROUTE_AUTO);
    for (int i = 0; i < 3; i++) tick(100);
}

static void go_bt(void) {
    reset();
    connect_headset();
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    g_ncalls = 0;
    g_calls[0] = '\0';
    g_stops = g_pauses = 0;
}

TEST(wired_until_a_stream_is_open_then_bluetooth_with_a_quiet_change) {
    audio_route_init();
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    tick(100);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output()); // no headset: nothing changes
    ASSERT_EQ(0, g_ncalls);
    connect_headset();
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    ASSERT_TRUE(strcmp(g_calls, "CBO") == 0); // close, select Bluetooth, open: in this order
    ASSERT_EQ(OUT_PATH_BT, g_path);
    ASSERT_EQ(0, g_stops); // nothing was playing
}

TEST(a_track_playing_is_stopped_for_the_change_of_output) {
    reset();
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    g_pstate = PLAYER_PLAYING;
    connect_headset();
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    ASSERT_EQ(1, g_stops); // the output only changes with a quiet audio core
    ASSERT_EQ(PLAYER_IDLE, g_pstate);
}

TEST(wired_setting_keeps_the_sound_on_the_wired_output) {
    reset();
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    audio_route_set_pref(AUDIO_ROUTE_WIRED);
    ASSERT_EQ(AUDIO_ROUTE_WIRED, audio_route_pref());
    connect_headset();
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    for (int i = 0; i < 5; i++) tick(100);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    ASSERT_EQ(0, g_ncalls);
    audio_route_set_pref(AUDIO_ROUTE_AUTO); // back to Auto: the open stream becomes the output
    tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
}

TEST(forcing_wired_while_on_bluetooth_switches_back_and_stops_the_stream) {
    go_bt();
    g_pstate = PLAYER_PLAYING;
    audio_route_set_pref(AUDIO_ROUTE_WIRED);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    ASSERT_TRUE(strcmp(g_calls, "CWO") == 0);
    ASSERT_EQ(1, g_stops);
    ASSERT_EQ(OUT_PATH_WIRED, g_path);
    audio_route_set_pref(AUDIO_ROUTE_AUTO);
}

TEST(the_stream_starts_with_playback_and_suspends_after_silence) {
    go_bt();
    ASSERT_EQ(HAL_BT_STREAM_OPEN, hal_bt_stream_state());
    g_pstate = PLAYER_PLAYING;
    pump_stream(HAL_BT_STREAM_STREAMING, 3000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state()); // playing on Bluetooth starts the stream
    g_pstate = PLAYER_PAUSED;
    for (int i = 0; i < 10; i++) tick(100); // 1 s: still streaming (a short pause is not worth a suspend)
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
    g_now += AUDIO_ROUTE_SUSPEND_MS;
    tick(100);
    ASSERT_EQ(HAL_BT_STREAM_SUSPENDED, hal_bt_stream_state());
    g_pstate = PLAYER_PLAYING; // and playing again resumes it
    pump_stream(HAL_BT_STREAM_STREAMING, 3000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
}

TEST(a_lost_link_pauses_at_once_keeps_bluetooth_and_raises_the_notice) {
    go_bt();
    g_pstate = PLAYER_PLAYING;
    uint32_t seq0, seq1;
    (void)audio_route_notice(&seq0);
    hal_host_bt_drop_link(); // the headset is switched off or out of range
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output()); // nothing plays from the wired output by itself
    ASSERT_EQ(0, g_ncalls);                         // no change of output, the stream of the audio core is untouched
    ASSERT_EQ(ROUTE_NOTICE_HEADSET_DROPPED, audio_route_notice(&seq1));
    ASSERT_TRUE(seq1 != seq0);
    int pauses = g_pauses;
    for (int i = 0; i < 5; i++) tick(100);
    ASSERT_EQ(pauses, g_pauses); // once
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
}

TEST(play_with_no_headset_on_the_bluetooth_output_pauses_again_and_says_so) {
    go_bt();
    hal_host_bt_drop_link();
    tick(100); // the drop is taken in while paused
    uint32_t seq0, seq1;
    (void)audio_route_notice(&seq0);
    g_pstate = PLAYER_PLAYING; // the user pressed Play
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    ASSERT_EQ(ROUTE_NOTICE_NOT_CONNECTED, audio_route_notice(&seq1));
    ASSERT_TRUE(seq1 != seq0);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
}

TEST(a_link_lost_while_paused_is_silent) {
    go_bt();
    g_pstate = PLAYER_PAUSED;
    uint32_t seq0, seq1;
    (void)audio_route_notice(&seq0);
    hal_host_bt_drop_link();
    tick(100);
    (void)audio_route_notice(&seq1);
    ASSERT_EQ(seq0, seq1); // nothing was playing: no banner
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
}

TEST(reconnecting_after_a_drop_lets_playback_resume_without_another_change) {
    go_bt();
    hal_host_bt_drop_link();
    tick(100);
    g_ncalls = 0;
    g_calls[0] = '\0';
    ASSERT_EQ(HAL_OK, hal_bt_connect(A_AIRPODS));
    for (int i = 0; i < 100 && hal_bt_link_state() != HAL_BT_LINK_CONNECTED; i++) {
        tick(50);
    }
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    ASSERT_EQ(0, g_ncalls); // already on Bluetooth: no close / open
    g_pstate = PLAYER_PLAYING;
    pump_stream(HAL_BT_STREAM_STREAMING, 3000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
}

TEST(letting_the_headset_go_on_purpose_returns_to_wired) {
    go_bt();
    g_pstate = PLAYER_IDLE;
    g_ncalls = 0;
    g_calls[0] = '\0';
    audio_route_user_released(); // Settings > Bluetooth > Disconnect
    hal_bt_disconnect();
    for (int i = 0; i < 5; i++) tick(100);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    ASSERT_TRUE(strcmp(g_calls, "CWO") == 0);
}

TEST(a_stream_released_by_the_headset_is_reopened_a_few_times_then_left) {
    go_bt();
    hal_host_bt_stream_dropped_by_headset(); // link stays up
    ASSERT_EQ(HAL_BT_STREAM_CLOSED, hal_bt_stream_state());
    g_now += 5000;
    tick(100);
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    ASSERT_EQ(HAL_BT_STREAM_OPEN, hal_bt_stream_state());
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    ASSERT_EQ(0, g_stops);
}

TEST(headphones_pulled_out_while_playing_pause_with_a_notice_and_do_not_resume) {
    reset();
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    hal_host_set_wired_attached(true);
    tick(100);
    uint32_t seq0;
    (void)audio_route_notice(&seq0);
    g_pstate = PLAYER_PLAYING;
    hal_host_set_wired_attached(false);
    tick(100);
    uint32_t seq1;
    ASSERT_EQ(ROUTE_NOTICE_HEADPHONES_UNPLUGGED, audio_route_notice(&seq1));
    ASSERT_TRUE(seq1 != seq0);
    ASSERT_EQ(1, g_pauses);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    hal_host_set_wired_attached(true); // plugging back in is not a Play
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    ASSERT_EQ(1, g_pauses);
    // pulled out while not playing: nothing to pause, no notice
    hal_host_set_wired_attached(false);
    tick(100);
    uint32_t seq2;
    (void)audio_route_notice(&seq2);
    ASSERT_EQ(seq1, seq2);
    // playing with nothing plugged in from the start (the null sink): not an unplug, no pause
    g_pstate = PLAYER_PLAYING;
    tick(100);
    tick(100);
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    ASSERT_EQ(1, g_pauses);
    hal_host_set_wired_attached(true);
    g_pstate = PLAYER_IDLE;
    tick(100);
}

TEST(the_bluetooth_setting_is_the_output_with_or_without_a_headset_and_survives_a_disconnect) {
    reset();
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    audio_route_set_pref(AUDIO_ROUTE_BLUETOOTH);
    ASSERT_EQ(AUDIO_ROUTE_BLUETOOTH, audio_route_pref());
    tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output()); // no headset at all: Bluetooth anyway
    ASSERT_TRUE(strcmp(g_calls, "CBO") == 0);
    uint32_t seq0, seq1;
    (void)audio_route_notice(&seq0);
    g_pstate = PLAYER_PLAYING; // Play with nothing connected: paused again, never the wired output
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    ASSERT_TRUE(audio_route_notice(&seq1) == ROUTE_NOTICE_NOT_CONNECTED && seq1 != seq0);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    connect_headset();
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    hal_bt_disconnect(); // Disconnect: under Auto this would return to wired
    audio_route_user_released();
    for (int i = 0; i < 80; i++) tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    audio_route_set_pref(AUDIO_ROUTE_WIRED); // only the setting moves it
    tick(100);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    audio_route_set_pref(AUDIO_ROUTE_AUTO);
}

TEST(leaving_the_bluetooth_setting_for_auto_with_no_headset_returns_to_wired) {
    reset();
    audio_route_set_pref(AUDIO_ROUTE_BLUETOOTH);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    audio_route_set_pref(AUDIO_ROUTE_AUTO);
    for (int i = 0; i < 20; i++) tick(100);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
}

TEST(the_saved_output_setting_reads_back_and_an_unknown_value_is_auto) {
    audio_route_set_pref(AUDIO_ROUTE_WIRED);
    ASSERT_EQ(AUDIO_ROUTE_WIRED, audio_route_pref());
    audio_route_set_pref(AUDIO_ROUTE_BLUETOOTH);
    ASSERT_EQ(AUDIO_ROUTE_BLUETOOTH, audio_route_pref());
    audio_route_set_pref(AUDIO_ROUTE_AUTO);
    ASSERT_EQ(AUDIO_ROUTE_AUTO, audio_route_pref());
    settings_set_u8(SETTING_AUDIO_OUTPUT, 9); // written by a newer firmware: Auto, as before the Bluetooth value existed
    ASSERT_EQ(AUDIO_ROUTE_AUTO, audio_route_pref());
    settings_set_u8(SETTING_AUDIO_OUTPUT, 1); // a card from before this change: Wired stays Wired
    ASSERT_EQ(AUDIO_ROUTE_WIRED, audio_route_pref());
    audio_route_set_pref(AUDIO_ROUTE_AUTO);
}
// ---- reconnect-on-play ----

static void remember_headset(void) {
    hal_bt_device_t d;
    memset(&d, 0, sizeof d);
    memcpy(d.addr, A_AIRPODS, 6);
    d.cod = 0x240404;
    d.audio_sink = true;
    strcpy(d.name, "AirPods Pro 3");
    ASSERT_TRUE(bt_saved_put(&d));
}

static void settle(uint32_t ms) { // time passes with nothing happening
    for (uint32_t t = 0; t < ms; t += 100) tick(100);
}

// The headset goes away for good with the output on Bluetooth and the release window of the setup long over; a headset is remembered.
static void go_bt_remembered_and_gone(bool idle_hangup) {
    go_bt();
    remember_headset();
    g_now += 6000; // the release window of the set-up (5 s) is over
    tick(100);
    if (idle_hangup) {
        hal_host_bt_remote_hangup(); // reason 0x13: DISCONNECTED, no loss
    } else {
        hal_host_bt_drop_link();     // reason 0x08: LINK_LOST
    }
    settle(300);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output()); // nothing moves the output by itself
}

TEST(play_after_an_idle_hang_up_waits_connects_and_starts_the_stream) {
    go_bt_remembered_and_gone(true);
    const uint32_t c0 = hal_host_bt_connect_count();
    uint32_t seq0, seq1;
    (void)audio_route_notice(&seq0);
    g_pstate = PLAYER_PLAYING; // the user presses Play
    tick(100);
    ASSERT_EQ(PLAYER_PLAYING, g_pstate); // not paused: the track waits
    ASSERT_EQ(0, g_pauses);
    ASSERT_TRUE(audio_route_holding());
    ASSERT_TRUE(audio_route_notice(&seq1) == ROUTE_NOTICE_CONNECTING && seq1 != seq0);
    pump_stream(HAL_BT_STREAM_STREAMING, 8000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state()); // connected, the stream opened and started by the route
    ASSERT_EQ(c0 + 1u, hal_host_bt_connect_count());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    ASSERT_EQ(0, g_pauses);
    ASSERT_FALSE(audio_route_holding());
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    ASSERT_TRUE(strcmp(g_calls, "") == 0); // the output was not touched
}

TEST(a_fresh_track_started_while_the_headset_is_away_waits_in_the_same_way) {
    go_bt_remembered_and_gone(true);
    g_pstate = PLAYER_OPENING; // a track chosen from Files is opening for a moment
    tick(100);
    ASSERT_FALSE(audio_route_holding()); // nothing to wait for yet
    g_pstate = PLAYER_PLAYING;
    tick(100);
    ASSERT_TRUE(audio_route_holding());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    pump_stream(HAL_BT_STREAM_STREAMING, 8000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
}

TEST(a_drop_mid_play_pauses_then_a_play_after_the_reconnect_starts_the_stream) {
    go_bt();
    remember_headset();
    g_pstate = PLAYER_PLAYING;
    pump_stream(HAL_BT_STREAM_STREAMING, 3000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
    uint32_t seq0, seq1;
    (void)audio_route_notice(&seq0);
    hal_host_bt_drop_link();
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate); // ADR-0019: a drop pauses ...
    ASSERT_TRUE(audio_route_notice(&seq1) == ROUTE_NOTICE_HEADSET_DROPPED && seq1 != seq0);
    ASSERT_FALSE(audio_route_holding());
    ASSERT_EQ(HAL_OK, hal_bt_connect(A_AIRPODS)); // ... and the automatic reconnect brings the headset back
    for (int i = 0; i < 100 && hal_bt_link_state() != HAL_BT_LINK_CONNECTED; i++) {
        tick(50);
    }
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    settle(300);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate); // ... without resuming
    ASSERT_TRUE(hal_bt_stream_state() != HAL_BT_STREAM_STREAMING);
    g_pstate = PLAYER_PLAYING; // the user presses Play
    pump_stream(HAL_BT_STREAM_STREAMING, 3000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
}

TEST(play_during_the_reconnect_after_a_drop_waits_and_goes_on_when_the_headset_is_back) {
    go_bt();
    remember_headset();
    g_pstate = PLAYER_PLAYING;
    pump_stream(HAL_BT_STREAM_STREAMING, 3000);
    hal_host_bt_drop_link();
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    bt_rc_lost(&g_rc, g_now); // bt_service: the loss starts the automatic schedule (first attempt in 2 s) ...
    g_pstate = PLAYER_PLAYING; // ... and the user presses Play while "Reconnecting..." is up
    tick(100);
    ASSERT_TRUE(audio_route_holding());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    pump_stream(HAL_BT_STREAM_STREAMING, 10000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
}

TEST(when_the_headset_cannot_be_reached_play_tries_twice_then_pauses_with_a_notice_and_stays_on_bluetooth) {
    go_bt_remembered_and_gone(true);
    hal_host_bt_set_unreachable(true); // in its case
    const uint32_t c0 = hal_host_bt_connect_count();
    uint32_t seq0, seq1;
    (void)audio_route_notice(&seq0);
    g_pstate = PLAYER_PLAYING;
    tick(100);
    ASSERT_TRUE(audio_route_holding());
    for (int i = 0; i < 400 && g_pstate == PLAYER_PLAYING; i++) {
        tick(100);
    }
    ASSERT_EQ(PLAYER_PAUSED, g_pstate); // place kept, not stopped
    ASSERT_EQ(0, g_stops);
    ASSERT_EQ(c0 + BT_RC_PLAY_ATTEMPTS, hal_host_bt_connect_count()); // two attempts, no more
    ASSERT_TRUE(audio_route_notice(&seq1) == ROUTE_NOTICE_CONNECT_FAILED && seq1 != seq0);
    ASSERT_FALSE(audio_route_holding());
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output()); // the firmware never falls back to wired by itself
    g_now += 120000; // and nothing retries in the background
    settle(300);
    ASSERT_EQ(c0 + BT_RC_PLAY_ATTEMPTS, hal_host_bt_connect_count());
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    ASSERT_EQ(1, g_pauses);
}

TEST(the_wait_is_bounded_even_if_nothing_answers) {
    go_bt_remembered_and_gone(true);
    g_pstate = PLAYER_PLAYING;
    tick(100);
    ASSERT_TRUE(audio_route_holding());
    g_now += AUDIO_ROUTE_HOLD_MS; // the schedule is not serviced here: only the route's own bound can end it
    audio_route_service();
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    uint32_t seq;
    ASSERT_EQ(ROUTE_NOTICE_CONNECT_FAILED, audio_route_notice(&seq));
    ASSERT_FALSE(audio_route_holding());
}

TEST(pausing_while_waiting_ends_the_wait_and_the_play_schedule) {
    go_bt_remembered_and_gone(true);
    hal_host_bt_set_unreachable(true);
    g_pstate = PLAYER_PLAYING;
    tick(100);
    ASSERT_TRUE(audio_route_holding());
    tick(100); // bt_service takes the request in
    ASSERT_TRUE(bt_rc_active(&g_rc));
    g_pstate = PLAYER_PAUSED; // the user pauses
    tick(100);
    ASSERT_FALSE(audio_route_holding());
    tick(100);
    ASSERT_FALSE(bt_rc_active(&g_rc)); // no further attempt
    uint32_t seq;
    ASSERT_TRUE(audio_route_notice(&seq) == ROUTE_NOTICE_CONNECTING); // no failure notice: the user chose
    g_now += 60000;
    settle(300);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
}

TEST(a_user_action_on_the_link_ends_the_wait_and_pauses_as_before) {
    go_bt_remembered_and_gone(true);
    hal_host_bt_set_unreachable(true);
    g_pstate = PLAYER_PLAYING;
    tick(100);
    ASSERT_TRUE(audio_route_holding());
    audio_route_connect_cancel(); // Settings > Bluetooth > Connect / Pair
    tick(100);
    ASSERT_FALSE(audio_route_holding());
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    uint32_t seq;
    ASSERT_EQ(ROUTE_NOTICE_NOT_CONNECTED, audio_route_notice(&seq));
}

TEST(play_with_no_remembered_headset_still_says_not_connected) {
    go_bt();
    g_now += 6000; // the release window of the set-up (5 s) is over
    tick(100);
    hal_host_bt_remote_hangup();
    settle(300);
    const uint32_t c0 = hal_host_bt_connect_count();
    g_pstate = PLAYER_PLAYING;
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    ASSERT_FALSE(audio_route_holding());
    uint32_t seq;
    ASSERT_EQ(ROUTE_NOTICE_NOT_CONNECTED, audio_route_notice(&seq));
    ASSERT_EQ(c0, hal_host_bt_connect_count());
}

TEST(under_auto_on_the_wired_output_play_plays_wired_and_connects_nothing) {
    reset();
    remember_headset();
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    const uint32_t c0 = hal_host_bt_connect_count();
    const uint32_t req0 = audio_route_connect_request_seq();
    g_pstate = PLAYER_PLAYING;
    settle(500);
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    ASSERT_FALSE(audio_route_holding());
    ASSERT_EQ(req0, audio_route_connect_request_seq());
    ASSERT_EQ(c0, hal_host_bt_connect_count());
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    g_pstate = PLAYER_IDLE;
}

TEST(with_the_bluetooth_setting_play_connects_to_the_remembered_headset) {
    reset();
    remember_headset();
    audio_route_set_pref(AUDIO_ROUTE_BLUETOOTH);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    const uint32_t c0 = hal_host_bt_connect_count();
    g_pstate = PLAYER_PLAYING;
    tick(100);
    ASSERT_TRUE(audio_route_holding());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    pump_stream(HAL_BT_STREAM_STREAMING, 8000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
    ASSERT_EQ(c0 + 1u, hal_host_bt_connect_count());
    g_pstate = PLAYER_IDLE;
    audio_route_set_pref(AUDIO_ROUTE_AUTO);
}

TEST(a_headset_that_ignores_the_start_is_asked_again_then_the_stream_is_closed_and_reopened_once) {
    go_bt();
    remember_headset();
    hal_host_bt_ignore_starts(3); // the first three requests are lost
    g_pstate = PLAYER_PLAYING;
    for (int i = 0; i < 100 && hal_bt_stream_state() != HAL_BT_STREAM_STREAMING; i++) {
        tick(100);
        ASSERT_EQ(PLAYER_PLAYING, g_pstate); // never paused on the way
    }
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    ASSERT_EQ(0, g_pauses);
    ASSERT_FALSE(audio_route_holding());
}

TEST(a_stream_the_headset_will_not_start_ends_in_a_pause_with_the_failure_notice) {
    go_bt();
    remember_headset();
    hal_host_bt_ignore_starts(1000);
    g_pstate = PLAYER_PLAYING;
    for (int i = 0; i < 200 && g_pstate == PLAYER_PLAYING; i++) {
        tick(100);
    }
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    uint32_t seq;
    ASSERT_EQ(ROUTE_NOTICE_CONNECT_FAILED, audio_route_notice(&seq));
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    ASSERT_FALSE(audio_route_holding());
    hal_host_bt_ignore_starts(0);
}

TEST(play_with_the_link_up_and_the_reopen_tries_used_up_tries_again_and_starts_the_stream) {
    go_bt();
    remember_headset();
    g_now += 6000; // the release window of the set-up is over
    tick(100);
    hal_host_bt_stream_dropped_by_headset(); // the link stays up, the stream is gone
    for (int k = 0; k < 3; k++) { // the three reopen tries of the route all fail
        hal_host_bt_fail_next_stream(HAL_BT_FAIL_STREAM);
        g_now += 3000;
        tick(100);
        pump_stream(HAL_BT_STREAM_FAILED, 3000);
        ASSERT_EQ(HAL_BT_STREAM_FAILED, hal_bt_stream_state());
    }
    g_now += 10000;
    settle(500); // given up: nothing more is tried while idle
    ASSERT_EQ(HAL_BT_STREAM_FAILED, hal_bt_stream_state());
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    g_pstate = PLAYER_PLAYING; // Play: it must try again, not give up on the spot
    tick(100);
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    ASSERT_EQ(0, g_pauses);
    pump_stream(HAL_BT_STREAM_STREAMING, 5000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    ASSERT_EQ(0, g_pauses);
    ASSERT_FALSE(audio_route_holding());
}

TEST(a_play_schedule_replaced_by_the_power_up_one_survives_the_end_of_the_wait) {
    go_bt_remembered_and_gone(true);
    hal_host_bt_set_unreachable(true);
    g_pstate = PLAYER_PLAYING;
    tick(100);
    tick(100); // bt_service takes the request in: a play schedule
    ASSERT_TRUE(bt_rc_active(&g_rc));
    ASSERT_EQ(BT_RC_PLAY_ATTEMPTS, g_rc.max_attempts);
    bt_rc_powerup(&g_rc, g_now); // the settings finished loading / Bluetooth came on: the boot schedule takes over (5 minutes, no cap)
    ASSERT_EQ(0, g_rc.max_attempts);
    g_pstate = PLAYER_PAUSED; // the user pauses: the wait ends
    tick(100);
    tick(100);
    ASSERT_FALSE(audio_route_holding());
    ASSERT_TRUE(bt_rc_active(&g_rc)); // the boot retry is not killed with the wait
}

TEST(play_while_a_connect_is_already_running_waits_for_it_and_makes_no_second_call) {
    go_bt_remembered_and_gone(true);
    const uint32_t c0 = hal_host_bt_connect_count();
    ASSERT_EQ(HAL_OK, hal_bt_connect(A_AIRPODS)); // somebody else's connect (Settings, the boot retry)
    ASSERT_EQ(HAL_BT_LINK_CONNECTING, hal_bt_link_state());
    g_pstate = PLAYER_PLAYING;
    tick(50);
    ASSERT_TRUE(audio_route_holding());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    ASSERT_EQ(c0 + 1u, hal_host_bt_connect_count()); // held back: no second call on top of it
    pump_stream(HAL_BT_STREAM_STREAMING, 8000);
    ASSERT_EQ(HAL_BT_STREAM_STREAMING, hal_bt_stream_state());
    ASSERT_EQ(c0 + 1u, hal_host_bt_connect_count());
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    ASSERT_EQ(0, g_pauses);
}

TEST(play_with_the_radio_off_pauses_at_once_with_not_connected) {
    go_bt_remembered_and_gone(true);
    ASSERT_EQ(HAL_OK, hal_bt_power(false));
    tick(100);
    ASSERT_EQ(HAL_BT_OFF, hal_bt_state());
    const uint32_t c0 = hal_host_bt_connect_count();
    const uint32_t req0 = audio_route_connect_request_seq();
    g_pstate = PLAYER_PLAYING;
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate); // no "Connecting..." and no 60 s stall for a radio that cannot answer
    ASSERT_FALSE(audio_route_holding());
    uint32_t seq;
    ASSERT_EQ(ROUTE_NOTICE_NOT_CONNECTED, audio_route_notice(&seq));
    ASSERT_EQ(req0, audio_route_connect_request_seq());
    ASSERT_EQ(c0, hal_host_bt_connect_count());
    ASSERT_EQ(HAL_OK, hal_bt_power(true)); // leave the simulator as the other tests expect it
    for (int i = 0; i < 40 && hal_bt_state() != HAL_BT_ON; i++) {
        tick(50);
    }
    ASSERT_EQ(HAL_BT_ON, hal_bt_state());
}

TEST(a_cancel_during_the_close_and_reopen_pauses_as_not_connected_not_as_a_drop) {
    go_bt();
    remember_headset();
    hal_host_bt_ignore_starts(1000); // the headset never starts the stream
    g_pstate = PLAYER_PLAYING;
    for (int i = 0; i < 200 && hal_bt_stream_state() != HAL_BT_STREAM_CLOSED; i++) {
        tick(100); // until the route closes the stream to open it afresh
    }
    ASSERT_EQ(HAL_BT_STREAM_CLOSED, hal_bt_stream_state());
    ASSERT_TRUE(audio_route_holding());
    audio_route_connect_cancel(); // Settings > Bluetooth > Connect / Pair during the restart
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    ASSERT_FALSE(audio_route_holding());
    uint32_t seq;
    ASSERT_EQ(ROUTE_NOTICE_NOT_CONNECTED, audio_route_notice(&seq)); // not "Headset disconnected"
    hal_host_bt_ignore_starts(0);
    settle(500);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    ASSERT_EQ(ROUTE_NOTICE_NOT_CONNECTED, audio_route_notice(&seq)); // and no second notice when the stream settles
}

// ---- the internal speaker (Y2): Settings > Speaker and Audio output = Speaker ----

static void speaker_done(void) { // leave the shared simulator the way the other tests expect it
    settings_set_u8(SETTING_LINE_OUT, 0);
    audio_route_set_speaker_enabled(false);
    audio_route_set_pref(AUDIO_ROUTE_AUTO);
    hal_host_set_wired_attached(true);
    hal_host_set_speaker_present(true);
    tick(100);
}

TEST(the_speaker_setting_defaults_to_off_is_saved_and_needs_a_speaker) {
    reset();
    uint8_t v;
    ASSERT_FALSE(settings_get_u8(SETTING_SPEAKER, &v)); // never written: Off
    ASSERT_FALSE(audio_route_speaker_enabled());
    audio_route_set_speaker_enabled(true);
    ASSERT_TRUE(settings_get_u8(SETTING_SPEAKER, &v) && v == 1);
    ASSERT_TRUE(audio_route_speaker_enabled());
    hal_host_set_speaker_present(false); // a device without one: the setting means nothing
    ASSERT_FALSE(audio_route_speaker_enabled());
    hal_host_set_speaker_present(true);
    audio_route_set_speaker_enabled(false);
    ASSERT_FALSE(audio_route_speaker_enabled());
    speaker_done();
}

TEST(the_speaker_output_setting_is_stored_as_3_and_reads_back) {
    reset();
    audio_route_set_pref(AUDIO_ROUTE_SPEAKER);
    uint8_t v;
    ASSERT_TRUE(settings_get_u8(SETTING_AUDIO_OUTPUT, &v) && v == 3);
    ASSERT_EQ(AUDIO_ROUTE_SPEAKER, audio_route_pref());
    settings_set_u8(SETTING_AUDIO_OUTPUT, 4); // a value from the future: Auto
    ASSERT_EQ(AUDIO_ROUTE_AUTO, audio_route_pref());
    settings_set_u8(SETTING_AUDIO_OUTPUT, 2); // the older values keep their meaning
    ASSERT_EQ(AUDIO_ROUTE_BLUETOOTH, audio_route_pref());
    speaker_done();
}

TEST(auto_allows_the_speaker_only_when_the_setting_is_on_and_no_headset_is_the_output) {
    reset();
    hal_host_set_wired_attached(false); // nothing in the jack
    tick(100);
    ASSERT_FALSE(hal_host_speaker_allowed()); // Speaker Off (the default): never
    audio_route_set_speaker_enabled(true);
    tick(100);
    ASSERT_TRUE(hal_host_speaker_allowed()); // Auto, wired, nothing in the jack, Speaker On
    audio_route_set_pref(AUDIO_ROUTE_WIRED);
    tick(100);
    ASSERT_FALSE(hal_host_speaker_allowed()); // Wired means the jack only
    audio_route_set_pref(AUDIO_ROUTE_AUTO);
    tick(100);
    ASSERT_TRUE(hal_host_speaker_allowed());
    connect_headset(); // a headset's stream takes over under Auto
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    ASSERT_FALSE(hal_host_speaker_allowed());
    audio_route_set_pref(AUDIO_ROUTE_BLUETOOTH);
    tick(100);
    ASSERT_FALSE(hal_host_speaker_allowed());
    speaker_done();
    reset();
}

TEST(the_speaker_setting_as_the_output_is_wired_even_with_a_headset_and_off_means_nothing_plays) {
    reset();
    hal_host_set_wired_attached(false);
    audio_route_set_speaker_enabled(true);
    audio_route_set_pref(AUDIO_ROUTE_SPEAKER);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    ASSERT_TRUE(hal_host_speaker_allowed());
    connect_headset();
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    for (int i = 0; i < 5; i++) tick(100);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output()); // never Bluetooth under Speaker
    ASSERT_TRUE(hal_host_speaker_allowed());
    audio_route_set_speaker_enabled(false); // Speaker Off wins over Audio output = Speaker
    tick(100);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    ASSERT_FALSE(hal_host_speaker_allowed());
    speaker_done();
    reset();
}

TEST(switching_from_bluetooth_to_the_speaker_setting_moves_the_output_and_allows_the_speaker) {
    go_bt();
    hal_host_set_wired_attached(false);
    audio_route_set_speaker_enabled(true);
    audio_route_set_pref(AUDIO_ROUTE_SPEAKER);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_WIRED, audio_route_output());
    ASSERT_TRUE(hal_host_speaker_allowed());
    speaker_done();
    reset();
}

TEST(turning_the_speaker_off_under_a_playing_track_pauses_with_no_audio_output) {
    reset();
    hal_host_set_wired_attached(false);
    audio_route_set_speaker_enabled(true);
    tick(100);
    uint32_t seq0, seq1;
    (void)audio_route_notice(&seq0);
    g_pstate = PLAYER_PLAYING;
    tick(100);
    ASSERT_EQ(PLAYER_PLAYING, g_pstate); // the speaker is the output: playing on
    ASSERT_EQ(0, g_pauses);
    audio_route_set_speaker_enabled(false);
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate);
    ASSERT_EQ(ROUTE_NOTICE_NO_OUTPUT, audio_route_notice(&seq1));
    ASSERT_TRUE(seq1 != seq0);
    speaker_done();
}

TEST(pulling_the_headphones_out_pauses_even_when_the_speaker_could_take_over_and_plugging_in_does_not) {
    reset();
    audio_route_set_speaker_enabled(true);
    hal_host_set_wired_attached(true); // headphones in
    tick(100);
    tick(100);
    uint32_t seq0, seq1;
    (void)audio_route_notice(&seq0);
    g_pstate = PLAYER_PLAYING;
    hal_host_set_wired_attached(false);
    tick(100);
    ASSERT_EQ(PLAYER_PAUSED, g_pstate); // not on the speaker unasked
    ASSERT_EQ(ROUTE_NOTICE_HEADPHONES_UNPLUGGED, audio_route_notice(&seq1));
    ASSERT_TRUE(seq1 != seq0);
    ASSERT_TRUE(hal_host_speaker_allowed()); // the next Play may use it
    g_pstate = PLAYER_PLAYING; // Play on the speaker
    tick(100);
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    hal_host_set_wired_attached(true); // plugging in while on the speaker: the sound moves to the jack, nothing pauses
    tick(100);
    ASSERT_EQ(PLAYER_PLAYING, g_pstate);
    ASSERT_EQ(1, g_pauses);
    speaker_done();
}

TEST(a_device_without_a_speaker_never_allows_it) {
    reset();
    hal_host_set_speaker_present(false);
    audio_route_set_speaker_enabled(true);
    audio_route_set_pref(AUDIO_ROUTE_SPEAKER);
    tick(100);
    ASSERT_FALSE(hal_host_speaker_allowed());
    speaker_done();
}
// ---- Line out ----

TEST(line_out_is_saved_off_by_default_and_only_turns_on_with_a_plug_in_the_jack) {
    reset();
    uint8_t v;
    ASSERT_FALSE(audio_route_line_out_enabled()); // absent or 0: Off
    hal_host_set_wired_attached(false);
    ASSERT_FALSE(audio_route_set_line_out_enabled(true)); // nothing in the jack
    ASSERT_FALSE(audio_route_line_out_enabled());
    hal_host_set_wired_attached(true);
    ASSERT_TRUE(audio_route_set_line_out_enabled(true));
    ASSERT_TRUE(settings_get_u8(SETTING_LINE_OUT, &v) && v == 1);
    tick(100);
    ASSERT_TRUE(audio_route_line_out_active());
    ASSERT_TRUE(hal_host_line_out_allowed());
    ASSERT_TRUE(player_volume_locked()); // software volume at 0 dB, wheel ignored
    ASSERT_TRUE(audio_route_set_line_out_enabled(false));
    tick(100);
    ASSERT_FALSE(hal_host_line_out_allowed());
    ASSERT_FALSE(player_volume_locked());
    speaker_done();
}

TEST(unplugging_the_jack_switches_line_out_off_saved_and_with_a_notice) {
    reset();
    hal_host_set_wired_attached(true);
    ASSERT_TRUE(audio_route_set_line_out_enabled(true));
    tick(100);
    ASSERT_TRUE(audio_route_line_out_active());
    uint32_t seq0, seq1;
    (void)audio_route_notice(&seq0);
    hal_host_set_wired_attached(false);
    tick(100);
    ASSERT_FALSE(audio_route_line_out_active());
    ASSERT_FALSE(hal_host_line_out_allowed());
    ASSERT_FALSE(player_volume_locked());
    uint8_t v = 9;
    ASSERT_TRUE(settings_get_u8(SETTING_LINE_OUT, &v) && v == 0); // saved Off: the next plug starts normally
    ASSERT_EQ(ROUTE_NOTICE_LINE_OUT_OFF, audio_route_notice(&seq1));
    ASSERT_TRUE(seq1 != seq0);
    hal_host_set_wired_attached(true);
    tick(100);
    ASSERT_FALSE(audio_route_line_out_active());
    speaker_done();
}

TEST(a_saved_line_out_is_switched_off_at_boot_when_the_jack_is_empty) {
    reset();
    settings_set_u8(SETTING_LINE_OUT, 1); // as if saved by the last run
    hal_host_set_wired_attached(false);
    tick(100);
    ASSERT_FALSE(audio_route_line_out_enabled());
    ASSERT_FALSE(audio_route_line_out_active());
    speaker_done();
}

TEST(a_saved_line_out_is_switched_off_on_a_settings_load_even_with_a_plug_in_the_jack) {
    reset();
    hal_host_set_wired_attached(true);
    settings_set_u8(SETTING_LINE_OUT, 1); // as if saved by the last run, with the amp in the jack
    settings_test_bump_generation();      // the boot-time load from the card
    tick(100);
    ASSERT_FALSE(audio_route_line_out_enabled());
    ASSERT_FALSE(audio_route_line_out_active());
    ASSERT_FALSE(hal_host_line_out_allowed());
    ASSERT_TRUE(audio_route_set_line_out_enabled(true)); // the user turns it on again for the amp, and it stays on
    tick(100);
    ASSERT_TRUE(audio_route_line_out_active());
    ASSERT_TRUE(audio_route_set_line_out_enabled(false));
    hal_host_set_wired_attached(false);
    tick(100);
    speaker_done();
}

TEST(line_out_is_ignored_on_bluetooth_and_comes_back_only_on_the_jack) {
    reset();
    hal_host_set_wired_attached(true);
    ASSERT_TRUE(audio_route_set_line_out_enabled(true));
    tick(100);
    ASSERT_TRUE(hal_host_line_out_allowed());
    connect_headset();
    pump_stream(HAL_BT_STREAM_OPEN, 3000);
    tick(100);
    ASSERT_EQ(AUDIO_OUT_BT, audio_route_output());
    ASSERT_FALSE(hal_host_line_out_allowed());
    ASSERT_FALSE(player_volume_locked()); // the headset's own volume
    speaker_done();
    reset();
}

TEST(a_device_without_the_hardware_has_no_line_out) {
    reset();
    hal_host_set_speaker_present(false);
    ASSERT_FALSE(audio_route_set_line_out_enabled(true));
    ASSERT_FALSE(audio_route_line_out_enabled());
    speaker_done();
}

int main(void) {
    RUN(wired_until_a_stream_is_open_then_bluetooth_with_a_quiet_change);
    RUN(a_track_playing_is_stopped_for_the_change_of_output);
    RUN(wired_setting_keeps_the_sound_on_the_wired_output);
    RUN(forcing_wired_while_on_bluetooth_switches_back_and_stops_the_stream);
    RUN(the_stream_starts_with_playback_and_suspends_after_silence);
    RUN(a_lost_link_pauses_at_once_keeps_bluetooth_and_raises_the_notice);
    RUN(play_with_no_headset_on_the_bluetooth_output_pauses_again_and_says_so);
    RUN(a_link_lost_while_paused_is_silent);
    RUN(reconnecting_after_a_drop_lets_playback_resume_without_another_change);
    RUN(letting_the_headset_go_on_purpose_returns_to_wired);
    RUN(a_stream_released_by_the_headset_is_reopened_a_few_times_then_left);
    RUN(headphones_pulled_out_while_playing_pause_with_a_notice_and_do_not_resume);
    RUN(the_bluetooth_setting_is_the_output_with_or_without_a_headset_and_survives_a_disconnect);
    RUN(leaving_the_bluetooth_setting_for_auto_with_no_headset_returns_to_wired);
    RUN(the_saved_output_setting_reads_back_and_an_unknown_value_is_auto);
    RUN(play_after_an_idle_hang_up_waits_connects_and_starts_the_stream);
    RUN(a_fresh_track_started_while_the_headset_is_away_waits_in_the_same_way);
    RUN(a_drop_mid_play_pauses_then_a_play_after_the_reconnect_starts_the_stream);
    RUN(play_during_the_reconnect_after_a_drop_waits_and_goes_on_when_the_headset_is_back);
    RUN(when_the_headset_cannot_be_reached_play_tries_twice_then_pauses_with_a_notice_and_stays_on_bluetooth);
    RUN(the_wait_is_bounded_even_if_nothing_answers);
    RUN(pausing_while_waiting_ends_the_wait_and_the_play_schedule);
    RUN(a_user_action_on_the_link_ends_the_wait_and_pauses_as_before);
    RUN(play_with_no_remembered_headset_still_says_not_connected);
    RUN(under_auto_on_the_wired_output_play_plays_wired_and_connects_nothing);
    RUN(with_the_bluetooth_setting_play_connects_to_the_remembered_headset);
    RUN(a_headset_that_ignores_the_start_is_asked_again_then_the_stream_is_closed_and_reopened_once);
    RUN(a_stream_the_headset_will_not_start_ends_in_a_pause_with_the_failure_notice);
    RUN(play_with_the_link_up_and_the_reopen_tries_used_up_tries_again_and_starts_the_stream);
    RUN(a_play_schedule_replaced_by_the_power_up_one_survives_the_end_of_the_wait);
    RUN(play_while_a_connect_is_already_running_waits_for_it_and_makes_no_second_call);
    RUN(play_with_the_radio_off_pauses_at_once_with_not_connected);
    RUN(a_cancel_during_the_close_and_reopen_pauses_as_not_connected_not_as_a_drop);
    RUN(the_speaker_setting_defaults_to_off_is_saved_and_needs_a_speaker);
    RUN(the_speaker_output_setting_is_stored_as_3_and_reads_back);
    RUN(auto_allows_the_speaker_only_when_the_setting_is_on_and_no_headset_is_the_output);
    RUN(the_speaker_setting_as_the_output_is_wired_even_with_a_headset_and_off_means_nothing_plays);
    RUN(switching_from_bluetooth_to_the_speaker_setting_moves_the_output_and_allows_the_speaker);
    RUN(turning_the_speaker_off_under_a_playing_track_pauses_with_no_audio_output);
    RUN(pulling_the_headphones_out_pauses_even_when_the_speaker_could_take_over_and_plugging_in_does_not);
    RUN(a_device_without_a_speaker_never_allows_it);
    RUN(line_out_is_saved_off_by_default_and_only_turns_on_with_a_plug_in_the_jack);
    RUN(unplugging_the_jack_switches_line_out_off_saved_and_with_a_notice);
    RUN(a_saved_line_out_is_switched_off_at_boot_when_the_jack_is_empty);
    RUN(line_out_is_ignored_on_bluetooth_and_comes_back_only_on_the_jack);
    RUN(a_device_without_the_hardware_has_no_line_out);
    return RESULT();
}
