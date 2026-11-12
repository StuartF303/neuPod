// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "audio_route.h"
#include "bt_pcm.h"
#include "hal_radio.h"
#include "os.h"
#include "player.h"
#include "settings_store.h"

#define RELEASE_WINDOW_MS 5000u  /* how long after Disconnect / Forget a link that is still up is waited for */
#define START_RETRY_MS 1000u     /* between two Start requests while the stream has not begun */
#define REOPEN_RETRY_MS 3000u    /* between two attempts to reopen a stream the headset released */
#define REOPEN_TRIES 3u

static audio_output_t g_out = AUDIO_OUT_WIRED;
static audio_route_pref_t g_pref = AUDIO_ROUTE_AUTO;
static uint32_t g_gen_seen = UINT32_MAX;
static bool g_was_up;           /* the stream was usable at the last tick while the output was Bluetooth */
static uint32_t g_release_until;
static bool g_release_armed;
static uint32_t g_quiet_since;  /* the last tick something was playing on Bluetooth */
static uint32_t g_last_start;
static uint32_t g_last_reopen;
static uint32_t g_reopen_tries;
static audio_route_notice_t g_notice;
static uint32_t g_notice_seq;

static bool stream_usable(void) {
    hal_bt_stream_state_t st = hal_bt_stream_state();
    return st == HAL_BT_STREAM_OPEN || st == HAL_BT_STREAM_STREAMING || st == HAL_BT_STREAM_SUSPENDED;
}

static void notice(audio_route_notice_t n) {
    g_notice = n;
    g_notice_seq++;
}

void audio_route_init(void) { hal_bt_set_pcm_source(bt_pcm_source()); }

audio_route_pref_t audio_route_pref(void) {
    uint8_t v;
    return settings_get_u8(SETTING_AUDIO_OUTPUT, &v) && v == 1 ? AUDIO_ROUTE_WIRED : AUDIO_ROUTE_AUTO; /* default Auto */
}

void audio_route_set_pref(audio_route_pref_t pref) {
    settings_set_u8(SETTING_AUDIO_OUTPUT, pref == AUDIO_ROUTE_WIRED ? 1u : 0u);
    g_pref = pref; /* applied by the next service tick */
}

audio_output_t audio_route_output(void) { return g_out; }

void audio_route_user_released(void) {
    g_release_until = os_tick_ms() + RELEASE_WINDOW_MS;
    g_release_armed = true;
}

audio_route_notice_t audio_route_notice(uint32_t *seq) {
    *seq = g_notice_seq;
    return g_notice;
}

// Change the output. The audio core must be quiet: whatever plays is stopped (the place is kept, like Stop), the stream is closed, the layout of
// the rings follows the output, the stream is opened again. The commands are processed in this order by the core.
static void apply_output(audio_output_t out) {
    player_info_t pi;
    player_get(&pi);
    if (pi.state != PLAYER_IDLE) {
        player_stop();
    }
    if (out == AUDIO_OUT_WIRED) {
        hal_bt_stream_suspend(); /* the encoder must not read the ring while its storage changes hands */
    }
    bool ok = audio_close() && audio_set_output(out, AUDIO_SAMPLE_RATE) && audio_open(AUDIO_SAMPLE_RATE, 2, 16);
    if (!ok) {
        printf("route: the audio core did not take the output change\n");
    }
    bt_pcm_request_flush();
    g_out = out;
    player_set_output_path(out == AUDIO_OUT_BT ? OUT_PATH_BT : OUT_PATH_WIRED);
    g_was_up = out == AUDIO_OUT_BT;
    g_quiet_since = os_tick_ms();
    g_reopen_tries = 0;
    printf("route: output %s\n", out == AUDIO_OUT_BT ? "Bluetooth" : "wired");
}

void audio_route_service(void) {
    const uint32_t now = os_tick_ms();
    uint32_t gen = settings_generation();
    if (gen != g_gen_seen) { /* the settings were (re)loaded from the card */
        g_gen_seen = gen;
        g_pref = audio_route_pref();
    }
    const bool up = stream_usable();

    if (g_out == AUDIO_OUT_WIRED) {
        if (g_pref == AUDIO_ROUTE_AUTO && up) {
            apply_output(AUDIO_OUT_BT);
        }
        return;
    }

    /* ---- the output is Bluetooth ---- */
    if (g_release_armed && (int32_t)(now - g_release_until) >= 0) {
        g_release_armed = false; /* the headset did not go away after all */
    }
    if (g_pref == AUDIO_ROUTE_WIRED || (g_release_armed && !up && hal_bt_link_state() != HAL_BT_LINK_CONNECTED)) {
        g_release_armed = false;
        apply_output(AUDIO_OUT_WIRED);
        return;
    }
    const hal_bt_stream_state_t st = hal_bt_stream_state();
    const bool link = hal_bt_link_state() == HAL_BT_LINK_CONNECTED;
    if (link && st == HAL_BT_STREAM_OPENING) {
        return; /* being set up: wait */
    }
    player_info_t pi;
    player_get(&pi);
    const bool playing = pi.state == PLAYER_PLAYING;

    if (!up) {
        if (playing) { /* the headset is gone (or never was): pause at once, position kept, nothing plays from elsewhere */
            player_pause();
            bt_pcm_request_flush();
            notice(g_was_up ? ROUTE_NOTICE_HEADSET_DROPPED : ROUTE_NOTICE_NOT_CONNECTED);
            printf("route: %s, paused\n", g_was_up ? "headset disconnected" : "no headset");
        } else if (g_was_up) {
            bt_pcm_request_flush();
        }
        g_was_up = false;
        if (!link) {
            g_reopen_tries = 0;
        } else if (g_reopen_tries < REOPEN_TRIES && now - g_last_reopen >= REOPEN_RETRY_MS) { /* the link is up but the headset released its stream */
            g_last_reopen = now;
            g_reopen_tries++;
            (void)hal_bt_stream_open();
        }
        return;
    }

    g_was_up = true;
    g_reopen_tries = 0;
    if (playing) {
        g_quiet_since = now;
        if ((st == HAL_BT_STREAM_OPEN || st == HAL_BT_STREAM_SUSPENDED) && now - g_last_start >= START_RETRY_MS) {
            g_last_start = now;
            (void)hal_bt_stream_start();
        }
    } else if (st == HAL_BT_STREAM_STREAMING && now - g_quiet_since >= AUDIO_ROUTE_SUSPEND_MS) {
        hal_bt_stream_suspend();
    }
}
