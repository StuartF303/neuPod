// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "audio_route.h"
#include "hal_audio.h"
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
static bool g_wired_was_attached; /* the wired output had something to listen to at the last tick (a platform that cannot tell always says yes; the speaker, when allowed, counts) */
static bool g_line_active;        /* Line out is in force: the HAL was told and the software volume is locked at 0 dB */
static bool g_hp_was_in;          /* headphones were in the jack at the last tick */
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

static void set_line_active(bool on) {
    if (on == g_line_active) {
        return;
    }
    if (on) {
        player_volume_set_locked(true); /* the software volume is at 0 dB before the DAC goes to full level */
        hal_audio_set_line_out(true);
    } else {
        hal_audio_set_line_out(false);  /* the DAC drops back (muted restart) first, then the user's volume returns */
        player_volume_set_locked(false);
    }
    g_line_active = on;
    printf("route: Line out %s\n", on ? "on" : "off");
}

static void notice(audio_route_notice_t n);

/* Every tick on the wired output. With the jack empty the setting is switched Off (and saved Off), so in-ear headphones plugged in later start normally. */
static void line_out_service(bool hp) {
    if (audio_route_line_out_enabled() && !hp) {
        settings_set_u8(SETTING_LINE_OUT, 0u);
        if (g_line_active) {
            notice(ROUTE_NOTICE_LINE_OUT_OFF);
        }
    }
    set_line_active(audio_route_line_out_enabled() && hp);
}

static void notice(audio_route_notice_t n) {
    g_notice = n;
    g_notice_seq++;
}

void audio_route_init(void) { hal_bt_set_pcm_source(bt_pcm_source()); }

audio_route_pref_t audio_route_pref(void) {
    uint8_t v;
    if (!settings_get_u8(SETTING_AUDIO_OUTPUT, &v)) {
        return AUDIO_ROUTE_AUTO; /* default Auto */
    }
    return v == 1 ? AUDIO_ROUTE_WIRED : v == 2 ? AUDIO_ROUTE_BLUETOOTH : v == 3 ? AUDIO_ROUTE_SPEAKER : AUDIO_ROUTE_AUTO; /* 0 and anything unknown: Auto (a card written by a newer firmware) */
}

bool audio_route_speaker_enabled(void) {
    uint8_t v;
    return hal_audio_speaker_available() && settings_get_u8(SETTING_SPEAKER, &v) && v == 1; /* absent = Off */
}

void audio_route_set_speaker_enabled(bool on) { settings_set_u8(SETTING_SPEAKER, on ? 1u : 0u); }

bool audio_route_line_out_enabled(void) {
    uint8_t v;
    return hal_audio_line_out_available() && settings_get_u8(SETTING_LINE_OUT, &v) && v == 1; /* absent = Off */
}

bool audio_route_line_out_active(void) { return g_line_active; }

bool audio_route_set_line_out_enabled(bool on) {
    if (on && (!hal_audio_line_out_available() || !hal_audio_headphones_in())) {
        return false; /* nothing in the jack: Line out would only be switched off again */
    }
    settings_set_u8(SETTING_LINE_OUT, on ? 1u : 0u);
    return true;
}

/* The speaker may sound only on the wired output, when the setting is On and the output setting is Auto or Speaker. Wired means the jack only. */
static bool speaker_allowed(void) { return g_out == AUDIO_OUT_WIRED && (g_pref == AUDIO_ROUTE_AUTO || g_pref == AUDIO_ROUTE_SPEAKER) && audio_route_speaker_enabled(); }

void audio_route_set_pref(audio_route_pref_t pref) {
    if (pref == AUDIO_ROUTE_AUTO && audio_route_pref() == AUDIO_ROUTE_BLUETOOTH) {
        audio_route_user_released(); /* Bluetooth -> Auto with no headset there: the user's own choice, so the output may return to wired (else it would sit on Bluetooth for ever) */
    }
    settings_set_u8(SETTING_AUDIO_OUTPUT, pref == AUDIO_ROUTE_WIRED ? 1u : pref == AUDIO_ROUTE_BLUETOOTH ? 2u : pref == AUDIO_ROUTE_SPEAKER ? 3u : 0u);
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
    if (out == AUDIO_OUT_BT) {
        hal_audio_set_speaker(false); /* the amplifier goes off before anything else moves */
        set_line_active(false);       /* and the full level: the headset has its own volume */
    }
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
    g_was_up = out == AUDIO_OUT_BT && stream_usable(); /* the Bluetooth setting can make Bluetooth the output with no headset: Play then says "not connected", not "dropped" */
    hal_audio_set_speaker(speaker_allowed());
    g_wired_was_attached = hal_audio_wired_attached();
    g_hp_was_in = hal_audio_headphones_in();
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
        if (g_pref == AUDIO_ROUTE_BLUETOOTH || (g_pref == AUDIO_ROUTE_AUTO && up)) {
            apply_output(AUDIO_OUT_BT);
            return;
        }
        /* Headphones pulled out while playing: pause (position kept, like a lost link) and say so, even when the speaker is allowed (the sound must
           not jump to it unasked). Plugging back in does not resume. The speaker turned off under a playing track pauses too. */
        const bool allow = speaker_allowed();
        hal_audio_set_speaker(allow); /* every tick: cheap, and the setting can change at any time */
        const bool hp = hal_audio_headphones_in();
        line_out_service(hp);
        const bool attached = hal_audio_wired_attached();
        const bool unplugged = g_hp_was_in && !hp;
        if (unplugged || (g_wired_was_attached && !attached)) {
            player_info_t pi;
            player_get(&pi);
            if (pi.state == PLAYER_PLAYING) {
                player_pause();
                notice(unplugged ? ROUTE_NOTICE_HEADPHONES_UNPLUGGED : ROUTE_NOTICE_NO_OUTPUT);
                printf("route: %s, paused\n", unplugged ? "headphones unplugged" : "no audio output");
            }
        }
        g_hp_was_in = hp;
        g_wired_was_attached = attached;
        return;
    }

    /* ---- the output is Bluetooth ---- */
    if (g_release_armed && (int32_t)(now - g_release_until) >= 0) {
        g_release_armed = false; /* the headset did not go away after all */
    }
    if (g_pref == AUDIO_ROUTE_WIRED || g_pref == AUDIO_ROUTE_SPEAKER || (g_pref != AUDIO_ROUTE_BLUETOOTH && g_release_armed && !up && hal_bt_link_state() != HAL_BT_LINK_CONNECTED)) {
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
        printf("route: not playing for %u ms (player state %d), suspending the stream\n", (unsigned)(now - g_quiet_since), (int)pi.state);
        hal_bt_stream_suspend();
    }
}
