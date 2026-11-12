// SPDX-License-Identifier: MPL-2.0
// AVRCP in the app (app/runtime/bt_control.c): the volume scale, the volume policy (the headset's volume is the volume while it has absolute volume and
// the output is Bluetooth; the software volume is held at 0 dB then and is the volume otherwise), the headset buttons, the track information for the
// headset. Over the host Bluetooth simulator, with fakes for the player and the routing.
#include <string.h>

#include "audio_dsp.h"
#include "audio_route.h"
#include "bt_control.h"
#include "hal_host.h"
#include "hal_radio.h"
#include "hal_system.h"
#include "player.h"
#include "unit.h"

// ---- fakes ----
static uint32_t g_now = 1000;
uint32_t os_tick_ms(void) { return g_now; }

static audio_output_t g_output = AUDIO_OUT_BT;
audio_output_t audio_route_output(void) { return g_output; }

static player_info_t g_pi;
static int g_toggles, g_nexts, g_prevs, g_sw_steps, g_holds;
static int32_t g_sw_last_steps;
static bool g_hold;
static uint64_t g_seek_to;
static int g_seeks;
void player_get(player_info_t *o) { *o = g_pi; }
void player_pause_toggle(void) { g_toggles++; }
void player_next(void) { g_nexts++; }
void player_prev(void) { g_prevs++; }
bool player_seek(uint64_t f) { g_seek_to = f; g_seeks++; return true; }
uint32_t player_volume_step(int32_t steps) { g_sw_steps++; g_sw_last_steps = steps; return 0; }
void player_volume_hold_unity(bool hold) { g_hold = hold; g_holds++; }

// ---- helpers ----
static const uint8_t A_AIRPODS[6] = {0x28, 0xCD, 0xC1, 0x01, 0x02, 0x03};
static const uint8_t A_FLIP[6] = {0x70, 0x99, 0x1C, 0x0B, 0x55, 0x66};

static void on_event(const hal_bt_event_t *ev, void *ctx) {
    (void)ctx;
    bt_control_on_event(ev);
}

static void pump(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 20) {
        g_now += 20;
        hal_bt_poll();
        hal_delay_ms(20);
        bt_control_service();
    }
}

static void connect(const uint8_t a[6], const char *name) {
    hal_bt_device_t d;
    memset(&d, 0, sizeof d);
    memcpy(d.addr, a, 6);
    d.audio_sink = true;
    strncpy(d.name, name, HAL_BT_NAME_MAX);
    hal_bt_disconnect();
    for (int i = 0; i < 100 && hal_bt_pair(&d) != HAL_OK; i++) {
        pump(20);
    }
    for (int i = 0; i < 200 && hal_bt_link_state() != HAL_BT_LINK_CONNECTED; i++) {
        pump(20);
    }
    pump(400); // the stream opens, AVRCP reports the volume
}

// ---- the scale ----

TEST(test_scale_ends_and_order) {
    ASSERT_EQ(127, bt_vol_from_db(0));
    ASSERT_EQ(0, bt_vol_from_db(AUDIO_VOL_OFF_DB));
    ASSERT_EQ(0, bt_vol_from_db(500));
    ASSERT_EQ(0u, bt_vol_to_db(127));
    ASSERT_EQ(AUDIO_VOL_OFF_DB, bt_vol_to_db(0));
    ASSERT_EQ(0u, bt_vol_to_db(200)); // clamped like the headset's own range
    // the default power-on level (-30 dB) is about 62 %
    ASSERT_EQ(79, bt_vol_from_db(AUDIO_VOL_DEFAULT_ATT_DB));
    // monotonic both ways, and a round trip through the other scale moves at most 1 dB / 1 unit
    for (uint32_t db = 1; db <= AUDIO_VOL_OFF_DB; db++) {
        ASSERT_TRUE(bt_vol_from_db(db) <= bt_vol_from_db(db - 1));
    }
    for (int v = 1; v <= 127; v++) {
        ASSERT_TRUE(bt_vol_to_db((uint8_t)v) <= bt_vol_to_db((uint8_t)(v - 1)));
    }
    for (uint32_t db = 0; db <= AUDIO_VOL_OFF_DB; db++) {
        uint32_t back = bt_vol_to_db(bt_vol_from_db(db));
        ASSERT_TRUE(back + 1u >= db && back <= db + 1u);
    }
    for (int v = 0; v <= 127; v++) {
        int back = bt_vol_from_db(bt_vol_to_db((uint8_t)v));
        ASSERT_TRUE(back - v <= 2 && v - back <= 2);
    }
}

TEST(test_wheel_steps_and_percent) {
    ASSERT_EQ(4, bt_vol_step(0, 1));
    ASSERT_EQ(0, bt_vol_step(2, -1)); // clamped at silent
    ASSERT_EQ(127, bt_vol_step(125, 1));
    ASSERT_EQ(127, bt_vol_step(127, 5));
    ASSERT_EQ(60, bt_vol_step(80, -5));
    ASSERT_EQ(0, bt_vol_step(10, -1000000));
    ASSERT_EQ(127, bt_vol_step(10, 1000000));
    // a wheel step is about 2 dB on the shared scale, so 32 steps cover the range
    ASSERT_TRUE(bt_vol_to_db(0) / 2 <= 40u && 127u / BT_VOL_STEP + 1u >= 32u);
    ASSERT_EQ(0u, bt_vol_percent(0));
    ASSERT_EQ(100u, bt_vol_percent(127));
    ASSERT_EQ(50u, bt_vol_percent(64));
    ASSERT_EQ(63u, bt_vol_percent(80));
}

// ---- the buttons ----

TEST(test_key_mapping_table) {
    // Play only acts when not playing, Pause / Stop only when playing, Play/Pause always toggles
    ASSERT_EQ(BT_KEY_TOGGLE, bt_key_action(HAL_BT_KEY_PLAY, PLAYER_PAUSED, true));
    ASSERT_EQ(BT_KEY_NONE, bt_key_action(HAL_BT_KEY_PLAY, PLAYER_PLAYING, true));
    ASSERT_EQ(BT_KEY_TOGGLE, bt_key_action(HAL_BT_KEY_PLAY, PLAYER_ENDED, false)); // plays the finished track again
    ASSERT_EQ(BT_KEY_TOGGLE, bt_key_action(HAL_BT_KEY_PAUSE, PLAYER_PLAYING, true));
    ASSERT_EQ(BT_KEY_NONE, bt_key_action(HAL_BT_KEY_PAUSE, PLAYER_PAUSED, true));
    ASSERT_EQ(BT_KEY_TOGGLE, bt_key_action(HAL_BT_KEY_STOP, PLAYER_PLAYING, true)); // Stop pauses: the place is kept
    ASSERT_EQ(BT_KEY_NONE, bt_key_action(HAL_BT_KEY_STOP, PLAYER_IDLE, true));
    ASSERT_EQ(BT_KEY_TOGGLE, bt_key_action(HAL_BT_KEY_PLAY_PAUSE, PLAYER_PLAYING, false));
    ASSERT_EQ(BT_KEY_TOGGLE, bt_key_action(HAL_BT_KEY_PLAY_PAUSE, PLAYER_PAUSED, false));
    // next / previous need a queue, as Left / Right do
    ASSERT_EQ(BT_KEY_NEXT, bt_key_action(HAL_BT_KEY_NEXT, PLAYER_PLAYING, true));
    ASSERT_EQ(BT_KEY_PREV, bt_key_action(HAL_BT_KEY_PREV, PLAYER_PAUSED, true));
    ASSERT_EQ(BT_KEY_NONE, bt_key_action(HAL_BT_KEY_NEXT, PLAYER_PLAYING, false));
    ASSERT_EQ(BT_KEY_NONE, bt_key_action(HAL_BT_KEY_PREV, PLAYER_PLAYING, false));
    // FF / RW scrub a track that plays or is paused
    ASSERT_EQ(BT_KEY_FWD, bt_key_action(HAL_BT_KEY_FF, PLAYER_PLAYING, false));
    ASSERT_EQ(BT_KEY_BACK, bt_key_action(HAL_BT_KEY_RW, PLAYER_PAUSED, true));
    ASSERT_EQ(BT_KEY_NONE, bt_key_action(HAL_BT_KEY_FF, PLAYER_IDLE, false));
    ASSERT_EQ(BT_KEY_NONE, bt_key_action((hal_bt_key_t)99, PLAYER_PLAYING, true));
}

TEST(test_queue_active_matches_the_global_keys_test) {
    player_info_t p;
    memset(&p, 0, sizeof p);
    p.src = PLAYER_SRC_FILE;
    p.queue_len = 3;
    p.state = PLAYER_PLAYING;
    ASSERT_TRUE(bt_queue_active(&p));
    p.state = PLAYER_OPENING;
    ASSERT_TRUE(bt_queue_active(&p));
    p.state = PLAYER_ENDED;
    ASSERT_FALSE(bt_queue_active(&p));
    p.state = PLAYER_PAUSED;
    p.queue_len = 0;
    ASSERT_FALSE(bt_queue_active(&p));
    p.queue_len = 2;
    p.src = PLAYER_SRC_TONE;
    ASSERT_FALSE(bt_queue_active(&p));
}

// ---- track information ----

static void fill_playing(player_info_t *p) {
    memset(p, 0, sizeof *p);
    p->src = PLAYER_SRC_FILE;
    p->state = PLAYER_PLAYING;
    snprintf(p->name, sizeof p->name, "01.wav");
    snprintf(p->tags.title, sizeof p->tags.title, "Hoppipolla");
    snprintf(p->tags.artist, sizeof p->tags.artist, "Sigur Ros");
    snprintf(p->tags.album, sizeof p->tags.album, "Takk");
    p->rate = 44100;
    p->total_frames = 44100ull * 270u;
    p->pos_frames = 44100ull * 12u;
    p->track_id = 5;
}

TEST(test_metadata_from_the_player) {
    player_info_t p;
    hal_bt_track_t t;
    fill_playing(&p);
    ASSERT_TRUE(bt_meta_from_player(&p, &t));
    ASSERT_EQ(5, t.track_id);
    ASSERT_TRUE(strcmp(t.title, "Hoppipolla") == 0 && strcmp(t.artist, "Sigur Ros") == 0 && strcmp(t.album, "Takk") == 0);
    ASSERT_EQ(270000, t.duration_ms);
    ASSERT_EQ(12000, t.position_ms);
    ASSERT_EQ(HAL_BT_PLAY_PLAYING, t.status);
    p.state = PLAYER_PAUSED;
    ASSERT_TRUE(bt_meta_from_player(&p, &t));
    ASSERT_EQ(HAL_BT_PLAY_PAUSED, t.status);
    p.state = PLAYER_ENDED;
    ASSERT_TRUE(bt_meta_from_player(&p, &t));
    ASSERT_EQ(HAL_BT_PLAY_STOPPED, t.status);
    p.tags.title[0] = '\0'; // no title tag: the file name
    ASSERT_TRUE(bt_meta_from_player(&p, &t));
    ASSERT_TRUE(strcmp(t.title, "01.wav") == 0);
    // a tone, or nothing: nothing to tell
    p.src = PLAYER_SRC_TONE;
    ASSERT_FALSE(bt_meta_from_player(&p, &t));
    p.src = PLAYER_SRC_NONE;
    ASSERT_FALSE(bt_meta_from_player(&p, &t));
}

TEST(test_when_the_headset_is_told_again) {
    player_info_t p;
    hal_bt_track_t sent, now;
    fill_playing(&p);
    ASSERT_TRUE(bt_meta_from_player(&p, &sent));
    now = sent;
    ASSERT_FALSE(bt_meta_changed(&sent, 500, &now));
    now.position_ms += 500; // time passing while playing is what the stack extrapolates
    ASSERT_FALSE(bt_meta_changed(&sent, 500, &now));
    now.position_ms = sent.position_ms + 500 + 2500; // a jump of 2.5 s: a seek
    ASSERT_TRUE(bt_meta_changed(&sent, 500, &now));
    now.position_ms = 0; // back to the start
    ASSERT_TRUE(bt_meta_changed(&sent, 500, &now));
    now = sent;
    now.track_id = 6;
    ASSERT_TRUE(bt_meta_changed(&sent, 10, &now));
    now = sent;
    now.status = HAL_BT_PLAY_PAUSED;
    ASSERT_TRUE(bt_meta_changed(&sent, 10, &now));
    now = sent;
    snprintf(now.title, sizeof now.title, "Another");
    ASSERT_TRUE(bt_meta_changed(&sent, 10, &now));
    now = sent;
    now.position_ms += 10000;
    ASSERT_TRUE(bt_meta_changed(&sent, 10000, &now)); // 10 s: refreshed anyway
    // paused: the position does not run on
    sent.status = now.status = HAL_BT_PLAY_PAUSED;
    now.position_ms = sent.position_ms + 3000;
    ASSERT_TRUE(bt_meta_changed(&sent, 3000, &now));
    now.position_ms = sent.position_ms;
    ASSERT_FALSE(bt_meta_changed(&sent, 3000, &now));
}

// ---- the policy over the simulator ----

TEST(test_absolute_volume_policy) {
    ASSERT_EQ(HAL_OK, hal_bt_init(on_event, NULL, NULL));
    pump(600);
    g_output = AUDIO_OUT_BT;
    ASSERT_FALSE(bt_volume_absolute());
    ASSERT_EQ(-1, bt_volume_get());
    // no headset: the wheel is the software volume
    bt_volume_step(1);
    ASSERT_EQ(1, g_sw_steps);
    ASSERT_EQ(1, g_sw_last_steps);
    connect(A_AIRPODS, "AirPods Pro 3");
    ASSERT_TRUE(bt_volume_absolute());
    ASSERT_EQ(60, bt_volume_get());
    ASSERT_TRUE(g_hold); // the software volume went to 0 dB
    // the wheel moves the headset's volume, not the software one
    uint32_t sets = hal_host_bt_volume_sets();
    bt_volume_step(2);
    ASSERT_EQ(68, bt_volume_get());
    bt_volume_step(-1);
    ASSERT_EQ(64, bt_volume_get());
    ASSERT_EQ(sets + 2, hal_host_bt_volume_sets());
    ASSERT_EQ(1, g_sw_steps); // unchanged
    // the headset's own volume moves the bar
    hal_host_bt_headset_volume(30);
    ASSERT_EQ(30, bt_volume_get());
    // the wheel does not pass the ends
    for (int i = 0; i < 20; i++) {
        bt_volume_step(-1);
    }
    ASSERT_EQ(0, bt_volume_get());
    for (int i = 0; i < 50; i++) {
        bt_volume_step(1);
    }
    ASSERT_EQ(127, bt_volume_get());
    // the wired output: the headset's volume is no longer the player's
    g_output = AUDIO_OUT_WIRED;
    pump(100);
    ASSERT_FALSE(bt_volume_absolute());
    ASSERT_FALSE(g_hold); // the user's software volume is back
    int sw = g_sw_steps;
    bt_volume_step(-3);
    ASSERT_EQ(sw + 1, g_sw_steps);
    ASSERT_EQ(-3, g_sw_last_steps);
    g_output = AUDIO_OUT_BT;
    pump(100);
    ASSERT_TRUE(g_hold);
    // the headset goes: software volume again
    hal_host_bt_drop_link();
    pump(100);
    ASSERT_FALSE(bt_volume_absolute());
    ASSERT_FALSE(g_hold);
}

TEST(test_no_absolute_volume_keeps_the_software_volume) {
    int holds = g_holds;
    connect(A_FLIP, "Flip 6");
    ASSERT_EQ(HAL_BT_LINK_CONNECTED, hal_bt_link_state());
    ASSERT_FALSE(bt_volume_absolute());
    ASSERT_FALSE(g_hold);
    ASSERT_EQ(holds, g_holds); // never held
    int sw = g_sw_steps;
    bt_volume_step(1);
    ASSERT_EQ(sw + 1, g_sw_steps);
    hal_bt_disconnect();
}

TEST(test_a_headset_at_zero_percent_is_reported_as_zero) {
    static const uint8_t A_XM5[6] = {0x00, 0x1B, 0x7C, 0x4A, 0x10, 0x22};
    uint32_t sets = hal_host_bt_volume_sets();
    connect(A_XM5, "WH-1000XM5");
    ASSERT_TRUE(bt_volume_absolute());
    ASSERT_EQ(0, bt_volume_get()); // the UI shows the bar at 0 and says so
    ASSERT_EQ(sets, hal_host_bt_volume_sets()); // and nothing of ours is pushed to the headset
    bt_volume_step(1);
    ASSERT_EQ(4, bt_volume_get());
    hal_bt_disconnect();
}

TEST(test_connect_cap_turns_a_loud_headset_down_once) {
    // 100 %: set to 50 % (64), before the software volume is released
    hal_host_bt_set_initial_volume(127);
    uint32_t sets = hal_host_bt_volume_sets();
    connect(A_AIRPODS, "AirPods Pro 3");
    ASSERT_TRUE(bt_volume_absolute());
    ASSERT_EQ(64, bt_volume_get());
    ASSERT_EQ(sets + 1, hal_host_bt_volume_sets());
    // a swipe above the cap afterwards is the user's: respected, not capped again
    hal_host_bt_headset_volume(110);
    pump(200);
    ASSERT_EQ(110, bt_volume_get());
    ASSERT_EQ(sets + 1, hal_host_bt_volume_sets());
    hal_bt_disconnect();
    // exactly at the cap and below it: untouched
    hal_host_bt_set_initial_volume(64);
    sets = hal_host_bt_volume_sets();
    connect(A_AIRPODS, "AirPods Pro 3");
    ASSERT_EQ(64, bt_volume_get());
    ASSERT_EQ(sets, hal_host_bt_volume_sets());
    hal_bt_disconnect();
    hal_host_bt_set_initial_volume(40 * 127 / 100); // 40 %
    connect(A_AIRPODS, "AirPods Pro 3");
    ASSERT_EQ(50, bt_volume_get());
    ASSERT_EQ(sets, hal_host_bt_volume_sets());
    hal_bt_disconnect();
    // a new connection is a new first report: capped again
    hal_host_bt_set_initial_volume(120);
    connect(A_AIRPODS, "AirPods Pro 3");
    ASSERT_EQ(64, bt_volume_get());
    ASSERT_EQ(sets + 1, hal_host_bt_volume_sets());
    hal_bt_disconnect();
    hal_host_bt_set_initial_volume(-1);
}

TEST(test_headset_play_pause_keys_are_robust) {
    // the matrix of the three keys over every player state: PLAY never pauses, PAUSE never plays, PLAY_PAUSE always toggles
    const player_state_t all[] = {PLAYER_IDLE, PLAYER_OPENING, PLAYER_PLAYING, PLAYER_PAUSED, PLAYER_ENDED};
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) {
        bool playing = all[i] == PLAYER_PLAYING;
        ASSERT_EQ(playing ? BT_KEY_NONE : BT_KEY_TOGGLE, bt_key_action(HAL_BT_KEY_PLAY, all[i], false));
        ASSERT_EQ(playing ? BT_KEY_TOGGLE : BT_KEY_NONE, bt_key_action(HAL_BT_KEY_PAUSE, all[i], false));
        ASSERT_EQ(BT_KEY_TOGGLE, bt_key_action(HAL_BT_KEY_PLAY_PAUSE, all[i], false));
    }
    // the headset's own START of a suspended stream reaches the app as PLAY: it resumes a paused player, and does nothing to a playing one
    connect(A_AIRPODS, "AirPods Pro 3");
    fill_playing(&g_pi);
    g_pi.state = PLAYER_PAUSED;
    int t0 = g_toggles;
    hal_host_bt_headset_key(HAL_BT_KEY_PLAY);
    pump(100);
    ASSERT_EQ(t0 + 1, g_toggles);
    g_pi.state = PLAYER_PLAYING;
    hal_host_bt_headset_key(HAL_BT_KEY_PLAY);
    pump(100);
    ASSERT_EQ(t0 + 1, g_toggles);
    hal_bt_disconnect();
}

TEST(test_headset_buttons_do_what_the_device_buttons_do) {
    connect(A_AIRPODS, "AirPods Pro 3");
    fill_playing(&g_pi);
    g_pi.queue_len = 3;
    g_pi.seekable = true;
    int t0 = g_toggles, n0 = g_nexts, p0 = g_prevs, s0 = g_seeks;
    hal_host_bt_headset_key(HAL_BT_KEY_PLAY_PAUSE);
    pump(100);
    ASSERT_EQ(t0 + 1, g_toggles);
    hal_host_bt_headset_key(HAL_BT_KEY_PLAY); // already playing
    pump(100);
    ASSERT_EQ(t0 + 1, g_toggles);
    hal_host_bt_headset_key(HAL_BT_KEY_PAUSE);
    pump(100);
    ASSERT_EQ(t0 + 2, g_toggles);
    hal_host_bt_headset_key(HAL_BT_KEY_NEXT);
    hal_host_bt_headset_key(HAL_BT_KEY_PREV);
    hal_host_bt_headset_key(HAL_BT_KEY_NEXT);
    pump(100);
    ASSERT_EQ(n0 + 2, g_nexts);
    ASSERT_EQ(p0 + 1, g_prevs);
    // fast forward: one scrub step (270 s -> 4 s) from 12 s
    hal_host_bt_headset_key(HAL_BT_KEY_FF);
    pump(100);
    ASSERT_EQ(s0 + 1, g_seeks);
    ASSERT_EQ(44100ull * 16u, g_seek_to);
    hal_host_bt_headset_key(HAL_BT_KEY_RW);
    pump(100);
    ASSERT_EQ(44100ull * 8u, g_seek_to);
    // no queue: next / previous do nothing
    g_pi.queue_len = 0;
    hal_host_bt_headset_key(HAL_BT_KEY_NEXT);
    pump(100);
    ASSERT_EQ(n0 + 2, g_nexts);
    // a flood is bounded, not a crash: more presses than the queue holds in one tick
    g_pi.queue_len = 3;
    for (int i = 0; i < 40; i++) {
        hal_host_bt_headset_key(HAL_BT_KEY_NEXT);
    }
    pump(100);
    ASSERT_TRUE(g_nexts > n0 + 2 && g_nexts <= n0 + 2 + 8);
    hal_bt_disconnect();
}

TEST(test_the_headset_is_told_what_plays) {
    memset(&g_pi, 0, sizeof g_pi);
    hal_bt_track_t got;
    connect(A_AIRPODS, "AirPods Pro 3");
    pump(100);
    ASSERT_FALSE(hal_host_bt_track_info(&got, NULL)); // nothing plays
    fill_playing(&g_pi);
    pump(100);
    ASSERT_TRUE(hal_host_bt_track_info(&got, NULL));
    ASSERT_TRUE(strcmp(got.title, "Hoppipolla") == 0);
    ASSERT_EQ(HAL_BT_PLAY_PLAYING, got.status);
    uint32_t calls;
    (void)hal_host_bt_track_info(NULL, &calls);
    pump(1000); // nothing changed (the stack extrapolates the position): no new call
    uint32_t calls2;
    (void)hal_host_bt_track_info(NULL, &calls2);
    ASSERT_EQ(calls, calls2);
    g_pi.state = PLAYER_PAUSED;
    pump(100);
    ASSERT_TRUE(hal_host_bt_track_info(&got, &calls2));
    ASSERT_EQ(HAL_BT_PLAY_PAUSED, got.status);
    ASSERT_TRUE(calls2 > calls);
    g_pi.track_id = 6;
    snprintf(g_pi.tags.title, sizeof g_pi.tags.title, "Saeglopur");
    pump(100);
    ASSERT_TRUE(hal_host_bt_track_info(&got, NULL));
    ASSERT_EQ(6, got.track_id);
    ASSERT_TRUE(strcmp(got.title, "Saeglopur") == 0);
    g_pi.pos_frames += 44100ull * 60u; // a seek
    pump(100);
    ASSERT_TRUE(hal_host_bt_track_info(&got, NULL));
    ASSERT_EQ(72000, got.position_ms);
    memset(&g_pi, 0, sizeof g_pi); // stopped, nothing loaded
    pump(100);
    ASSERT_FALSE(hal_host_bt_track_info(&got, NULL));
    hal_bt_disconnect();
}

int main(void) {
    RUN(test_scale_ends_and_order);
    RUN(test_wheel_steps_and_percent);
    RUN(test_key_mapping_table);
    RUN(test_queue_active_matches_the_global_keys_test);
    RUN(test_metadata_from_the_player);
    RUN(test_when_the_headset_is_told_again);
    RUN(test_absolute_volume_policy);
    RUN(test_no_absolute_volume_keeps_the_software_volume);
    RUN(test_a_headset_at_zero_percent_is_reported_as_zero);
    RUN(test_connect_cap_turns_a_loud_headset_down_once);
    RUN(test_headset_play_pause_keys_are_robust);
    RUN(test_headset_buttons_do_what_the_device_buttons_do);
    RUN(test_the_headset_is_told_what_plays);
    return RESULT();
}
