// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <string.h>

#include "audio_dsp.h"
#include "audio_route.h"
#include "bt_control.h"
#include "os.h"
#include "runtime_config.h"
#include "../ui/ui_logic.h"  /* ui_seek_step_s: the Now Playing scrub step */

/* ---- the volume scale ---- */

uint8_t bt_vol_from_db(uint32_t att_db) {
    if (att_db >= AUDIO_VOL_OFF_DB) {
        return 0;
    }
    return (uint8_t)(((AUDIO_VOL_OFF_DB - att_db) * HAL_BT_VOLUME_MAX + AUDIO_VOL_OFF_DB / 2u) / AUDIO_VOL_OFF_DB);
}

uint32_t bt_vol_to_db(uint8_t volume) {
    if (volume >= HAL_BT_VOLUME_MAX) {
        return 0;
    }
    return ((HAL_BT_VOLUME_MAX - volume) * AUDIO_VOL_OFF_DB + HAL_BT_VOLUME_MAX / 2u) / HAL_BT_VOLUME_MAX;
}

uint8_t bt_vol_step(uint8_t volume, int32_t steps) {
    int64_t v = (int64_t)volume + (int64_t)steps * (int64_t)BT_VOL_STEP;
    return v < 0 ? 0 : v > (int64_t)HAL_BT_VOLUME_MAX ? (uint8_t)HAL_BT_VOLUME_MAX : (uint8_t)v;
}

unsigned bt_vol_percent(uint8_t volume) { return ((unsigned)volume * 100u + HAL_BT_VOLUME_MAX / 2u) / HAL_BT_VOLUME_MAX; }

/* ---- the headset's buttons ---- */

bt_key_action_t bt_key_action(hal_bt_key_t key, player_state_t st, bool queue) {
    const bool playing = st == PLAYER_PLAYING;
    switch (key) {
    case HAL_BT_KEY_PLAY: return playing ? BT_KEY_NONE : BT_KEY_TOGGLE;
    case HAL_BT_KEY_PAUSE:
    case HAL_BT_KEY_STOP: return playing ? BT_KEY_TOGGLE : BT_KEY_NONE;
    case HAL_BT_KEY_PLAY_PAUSE: return BT_KEY_TOGGLE;
    case HAL_BT_KEY_NEXT: return queue ? BT_KEY_NEXT : BT_KEY_NONE;
    case HAL_BT_KEY_PREV: return queue ? BT_KEY_PREV : BT_KEY_NONE;
    case HAL_BT_KEY_FF: return playing || st == PLAYER_PAUSED ? BT_KEY_FWD : BT_KEY_NONE;
    case HAL_BT_KEY_RW: return playing || st == PLAYER_PAUSED ? BT_KEY_BACK : BT_KEY_NONE;
    }
    return BT_KEY_NONE;
}

bool bt_queue_active(const player_info_t *pi) {
    return pi->src == PLAYER_SRC_FILE && pi->queue_len != 0 && (pi->state == PLAYER_PLAYING || pi->state == PLAYER_PAUSED || pi->state == PLAYER_OPENING);
}

uint32_t bt_scrub_step_s(uint64_t total_s) { return ui_seek_step_s(total_s); }

/* ---- track information ---- */

bool bt_meta_from_player(const player_info_t *pi, hal_bt_track_t *out) {
    if (pi->src != PLAYER_SRC_FILE) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->track_id = pi->track_id;
    hal_bt_meta_copy(out->title, sizeof out->title, pi->tags.title[0] != '\0' ? pi->tags.title : pi->name);
    hal_bt_meta_copy(out->artist, sizeof out->artist, pi->tags.artist);
    hal_bt_meta_copy(out->album, sizeof out->album, pi->tags.album);
    if (pi->rate != 0) {
        out->duration_ms = (uint32_t)(pi->total_frames * 1000u / pi->rate);
        out->position_ms = (uint32_t)(pi->pos_frames * 1000u / pi->rate);
    }
    out->status = pi->state == PLAYER_PLAYING || pi->state == PLAYER_OPENING ? HAL_BT_PLAY_PLAYING : pi->state == PLAYER_PAUSED ? HAL_BT_PLAY_PAUSED : HAL_BT_PLAY_STOPPED;
    return true;
}

bool bt_meta_changed(const hal_bt_track_t *sent, uint32_t age_ms, const hal_bt_track_t *now) {
    if (sent->track_id != now->track_id || sent->status != now->status || sent->duration_ms != now->duration_ms || strcmp(sent->title, now->title) != 0) {
        return true;
    }
    if (age_ms >= 10000u) {
        return true;
    }
    int64_t expected = (int64_t)sent->position_ms + (sent->status == HAL_BT_PLAY_PLAYING ? (int64_t)age_ms : 0);
    int64_t off = (int64_t)now->position_ms - expected;
    return off > 2000 || off < -2000;
}

/* ---- the app side ---- */

bool bt_volume_absolute(void) { return audio_route_output() == AUDIO_OUT_BT && hal_bt_volume_supported(); }

int bt_volume_get(void) { return bt_volume_absolute() ? hal_bt_volume_get() : -1; }

void bt_volume_step(int32_t steps) {
    int v = bt_volume_get();
    if (v >= 0) {
        (void)hal_bt_volume_set(bt_vol_step((uint8_t)v, steps));
    } else {
        (void)player_volume_step(steps);
    }
}

/* Headset buttons: one producer (the radio's event call), one consumer (the monitor task). A full ring drops the newest: a button pressed 8 times
 * in 100 ms is not a human. */
#define KEY_RING 8u
static volatile uint8_t g_ring[KEY_RING];
static volatile uint32_t g_head, g_tail; /* head: next write, tail: next read */

void bt_control_on_event(const hal_bt_event_t *ev) {
    if (ev->type != HAL_BT_EVT_KEY) {
        return; /* the volume is read from the HAL; its event is for listeners that want to react at once */
    }
    uint32_t h = __atomic_load_n(&g_head, __ATOMIC_RELAXED);
    if (h - __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE) >= KEY_RING) {
        return;
    }
    g_ring[h % KEY_RING] = (uint8_t)ev->key;
    __atomic_store_n(&g_head, h + 1u, __ATOMIC_RELEASE);
}

static player_info_t g_pi; /* monitor task only: the structure is large */

static void apply_key(hal_bt_key_t key) {
    player_get(&g_pi);
    switch (bt_key_action(key, g_pi.state, bt_queue_active(&g_pi))) {
    case BT_KEY_TOGGLE: player_pause_toggle(); break;
    case BT_KEY_NEXT: player_next(); break;
    case BT_KEY_PREV: player_prev(); break;
    case BT_KEY_FWD:
    case BT_KEY_BACK: {
        if (!g_pi.seekable || g_pi.rate == 0 || g_pi.total_frames == 0) {
            break;
        }
        uint64_t step = (uint64_t)bt_scrub_step_s(g_pi.total_frames / g_pi.rate) * g_pi.rate;
        uint64_t to = g_pi.pos_frames;
        if (key == HAL_BT_KEY_FF) {
            to = to + step > g_pi.total_frames ? g_pi.total_frames : to + step;
        } else {
            to = to > step ? to - step : 0;
        }
        (void)player_seek(to);
        break;
    }
    case BT_KEY_NONE: break;
    }
}

static hal_bt_track_t g_sent, g_now; /* monitor task only */
static bool g_sent_valid, g_none_sent;
static uint32_t g_sent_ms;
static bool g_hold;
static bool g_supported_seen; /* hal_bt_volume_supported() as of the last service: its rising edge is "the headset's first volume report on this connection" */

static void meta_service(uint32_t now) {
    if (hal_bt_link_state() != HAL_BT_LINK_CONNECTED) {
        g_sent_valid = g_none_sent = false; /* the next connection starts from nothing */
        return;
    }
    player_get(&g_pi);
    if (bt_meta_from_player(&g_pi, &g_now)) {
        if ((!g_sent_valid || bt_meta_changed(&g_sent, now - g_sent_ms, &g_now)) && hal_bt_track_info(&g_now)) { /* false: the stack was busy, the next tick asks again */
            g_sent = g_now;
            g_sent_valid = true;
            g_sent_ms = now;
            g_none_sent = false;
        }
    } else if ((g_sent_valid || !g_none_sent) && hal_bt_track_info(NULL)) {
        g_sent_valid = false;
        g_none_sent = true;
    }
}

void bt_control_service(void) {
    const uint32_t now = os_tick_ms();
    const bool supported = hal_bt_volume_supported();
    if (supported && !g_supported_seen) {
        /* The headset has just told us its volume (a phone may have left it at 100 %). Never louder than the cap when we take it over: before the software
         * volume is released to 0 dB below, so the first sound is not the loud one. Only down; what the user does afterwards is theirs. */
        int v = hal_bt_volume_get();
        if (v > (int)NEUPOD_BT_VOLUME_CAP_ON_CONNECT && hal_bt_volume_set((uint8_t)NEUPOD_BT_VOLUME_CAP_ON_CONNECT) == HAL_OK) {
            printf("bt: headset volume capped %u%% -> %u%%\n", bt_vol_percent((uint8_t)v), bt_vol_percent((uint8_t)NEUPOD_BT_VOLUME_CAP_ON_CONNECT));
        }
    }
    g_supported_seen = supported;
    const bool absolute = bt_volume_absolute();
    if (absolute != g_hold) { /* the headset's volume is the volume: nothing may be attenuated a second time. Back to the software volume when it is not. */
        g_hold = absolute;
        player_volume_hold_unity(absolute);
        printf("bt: volume is %s\n", absolute ? "the headset's (software 0 dB)" : "the software volume");
    }
    while (__atomic_load_n(&g_tail, __ATOMIC_RELAXED) != __atomic_load_n(&g_head, __ATOMIC_ACQUIRE)) {
        uint32_t t = __atomic_load_n(&g_tail, __ATOMIC_RELAXED);
        hal_bt_key_t k = (hal_bt_key_t)g_ring[t % KEY_RING];
        __atomic_store_n(&g_tail, t + 1u, __ATOMIC_RELEASE);
        apply_key(k);
    }
    meta_service(now);
}
