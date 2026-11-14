// SPDX-License-Identifier: MPL-2.0
// Audio routing (app/runtime/audio_route.c) over the host Bluetooth simulator, the real settings store and the real Bluetooth ring, with fakes
// for the audio core commands and the player: Auto / Wired, the quiet change of output, pause on link loss with the output staying on
// Bluetooth, start / suspend of the stream, and the notices for the UI.
#include <string.h>

#include "audio_route.h"
#include "bt_pcm.h"
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

// ---- helpers ----
static void tick(uint32_t ms) {
    g_now += ms;
    hal_delay_ms(ms > 50 ? 50 : ms); // the simulator runs on the real clock
    hal_bt_poll();
    audio_route_service();
}

static void pump_stream(hal_bt_stream_state_t want, uint32_t max_ms) {
    for (uint32_t t = 0; t < max_ms && hal_bt_stream_state() != want; t += 50) {
        tick(50);
    }
}

static const uint8_t A_AIRPODS[6] = {0x28, 0xCD, 0xC1, 0x01, 0x02, 0x03};
static bool g_inited;

static void connect_headset(void) {
    if (!g_inited) {
        g_inited = true;
        settings_init(NULL);
        ASSERT_EQ(HAL_OK, hal_bt_init(NULL, NULL, NULL));
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
    return RESULT();
}
