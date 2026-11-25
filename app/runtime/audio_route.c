// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "audio_route.h"
#include "bt_saved.h"
#include "hal_audio.h"
#include "bt_pcm.h"
#include "hal_radio.h"
#include "os.h"
#include "player.h"
#include "settings_store.h"

#define RELEASE_WINDOW_MS 5000u  /* how long after Disconnect / Forget a link that is still up is waited for */
#define START_RETRY_MS 2000u     /* between two Start requests while the stream has not begun (a headset refuses a second START while one is still pending, so not faster) */
#define REOPEN_RETRY_MS 3000u    /* between two attempts to reopen a stream the headset released */
#define REOPEN_TRIES 3u
#define START_TRIES 3u           /* Start requests the headset may leave unanswered before the stream is closed and opened once more */

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
/* Reconnect-on-play (audio_route.h): Play with the headset away waits (the player stays PLAYING, nothing is consumed) instead of pausing. */
/* g_hold and g_connect_req are written on the monitor task and read on the UI task, g_cancel and g_gaveup the other way round: __atomic release / acquire
   (the hold is set before the request counter is bumped, so a reader that sees the request sees the hold). */
static bool g_hold;                  /* waiting; read by bt_service, which keeps its play schedule going only while it is true */
static bool g_cancel;                /* the user took over the link (any task): end the wait at the next tick */
static uint32_t g_connect_req, g_gaveup;
static uint32_t g_hold_since, g_gaveup_base;
static uint8_t g_start_tries;        /* Start requests since the stream last streamed */
static bool g_restarted;             /* the stream has been closed and reopened once for this Play */
static bool g_restarting;            /* ... and the close is on its way */

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
    __atomic_store_n(&g_cancel, true, __ATOMIC_RELEASE);
}

void audio_route_connect_cancel(void) { __atomic_store_n(&g_cancel, true, __ATOMIC_RELEASE); }
uint32_t audio_route_connect_request_seq(void) { return __atomic_load_n(&g_connect_req, __ATOMIC_ACQUIRE); }
bool audio_route_holding(void) { return __atomic_load_n(&g_hold, __ATOMIC_ACQUIRE); }
void audio_route_connect_gave_up(void) { (void)__atomic_add_fetch(&g_gaveup, 1u, __ATOMIC_RELEASE); }
static void hold_set(bool on) { __atomic_store_n(&g_hold, on, __ATOMIC_RELEASE); }

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
    hold_set(false); /* a Play's wait belongs to the Bluetooth output (the player was stopped above) */
    g_start_tries = 0;
    g_restarted = g_restarting = false;
    printf("route: output %s\n", out == AUDIO_OUT_BT ? "Bluetooth" : "wired");
}

/* Play with no usable stream and a headset remembered: wait for it instead of pausing. With the link down, bt_service is asked (g_connect_req) to connect;
   with it up there is nothing to ask, the stream is opened by the reopen below. g_hold is set first: bt_service looks at it when it sees the request. */
static bool hold_begin(uint32_t now, bool link) {
    const hal_bt_state_t rs = hal_bt_state();
    if (!bt_saved_get(NULL) || rs == HAL_BT_OFF || rs == HAL_BT_FAILED) {
        return false; /* nothing to wait for, or the radio is off / broken and would never answer: the plain "Headset not connected" pause (STARTING and SCANNING do wait) */
    }
    g_gaveup_base = __atomic_load_n(&g_gaveup, __ATOMIC_ACQUIRE);
    g_hold_since = now;
    hold_set(true);
    if (!link) {
        __atomic_store_n(&g_connect_req, g_connect_req + 1u, __ATOMIC_RELEASE);
    } else {
        g_reopen_tries = 0; /* a fresh Play gets its own reopen tries, whatever earlier ones were used up */
        g_last_reopen = now - REOPEN_RETRY_MS;
    }
    notice(ROUTE_NOTICE_CONNECTING);
    printf("route: playing with no headset stream, waiting (%s)\n", link ? "link up" : "connect asked for");
    return true;
}

/* The wait is over without a stream: pause, place kept, ring emptied (as on a drop); the output stays Bluetooth. */
static void hold_fail(const char *why) {
    hold_set(false);
    g_restarted = g_restarting = false;
    g_start_tries = 0;
    player_pause();
    bt_pcm_request_flush();
    notice(ROUTE_NOTICE_CONNECT_FAILED);
    printf("route: %s, paused\n", why);
}

void audio_route_service(void) {
    const uint32_t now = os_tick_ms();
    uint32_t gen = settings_generation();
    if (gen != g_gen_seen) { /* the settings were (re)loaded from the card */
        g_gen_seen = gen;
        g_pref = audio_route_pref();
        /* Line out never survives a load (boot, card reinsert): whatever is in the jack now may be earphones that replaced the amp
           while the unit was off, and they must not start at full level. The user turns it on again for the amp. */
        if (audio_route_line_out_enabled()) {
            settings_set_u8(SETTING_LINE_OUT, 0u);
            printf("route: Line out reset to Off on settings load\n");
        }
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
    player_info_t pi;
    player_get(&pi);
    bool playing = pi.state == PLAYER_PLAYING;

    bool cancelled = false; /* the user took over the link: the wait ends, and this tick pauses as a Play with no headset does */
    if (__atomic_exchange_n(&g_cancel, false, __ATOMIC_ACQ_REL)) {
        cancelled = g_hold;
        hold_set(false);
        if (cancelled) {
            g_restarting = false; /* a close / reopen in progress is abandoned (the stream may still be OPEN: it is simply left to the idle suspend) */
            if (up && playing) {
                player_pause();
                bt_pcm_request_flush();
                notice(ROUTE_NOTICE_NOT_CONNECTED);
                printf("route: wait cancelled, paused\n");
                playing = false;
            }
        }
    }
    if (g_hold) {
        if (!playing && pi.state != PLAYER_OPENING) { /* paused (or stopped) while waiting: nothing more to do; an attempt already in flight finishes by itself, no further one starts */
            hold_set(false);
        } else if (up && !g_restarting) {
            hold_set(false); /* the stream is usable: the playing branch below starts it */
        } else if ((int32_t)(now - g_hold_since - AUDIO_ROUTE_HOLD_MS) >= 0) {
            hold_fail("headset did not come in time");
            playing = false;
        } else if ((int32_t)(__atomic_load_n(&g_gaveup, __ATOMIC_ACQUIRE) - g_gaveup_base) != 0) {
            hold_fail("could not connect");
            playing = false;
        } else if (!up && link && st != HAL_BT_STREAM_OPENING && g_reopen_tries >= REOPEN_TRIES && now - g_last_reopen >= REOPEN_RETRY_MS) {
            hold_fail("headset linked but no audio stream");
            playing = false;
        }
    }
    if (link && st == HAL_BT_STREAM_OPENING) {
        return; /* being set up: wait */
    }

    if (!up) {
        if (playing && !g_hold && (cancelled || g_was_up || !hold_begin(now, link))) { /* the headset is gone (or never was): pause at once, position kept, nothing plays from elsewhere */
            player_pause();
            bt_pcm_request_flush();
            const bool dropped = g_was_up && !cancelled; /* a cancelled wait is the user's doing, not a drop */
            notice(dropped ? ROUTE_NOTICE_HEADSET_DROPPED : ROUTE_NOTICE_NOT_CONNECTED);
            printf("route: %s, paused\n", dropped ? "headset disconnected" : "no headset");
        } else if (!playing && g_was_up) {
            bt_pcm_request_flush();
        }
        if (!playing) {
            g_start_tries = 0;
            g_restarted = false;
        }
        g_was_up = false;
        g_restarting = false;
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
        if (st == HAL_BT_STREAM_STREAMING) {
            g_start_tries = 0;
            g_restarted = false;
        } else if (!g_restarting && now - g_last_start >= START_RETRY_MS) { /* OPEN or SUSPENDED: ask for the start, a few times */
            g_last_start = now;
            if (g_start_tries < START_TRIES) {
                g_start_tries++;
                printf("route: asking the headset to start the stream (%u)\n", (unsigned)g_start_tries);
                (void)hal_bt_stream_start();
            } else if (!g_restarted && hold_begin(now, true)) {
                /* The headset answered none of them: close the stream and open it afresh once. The wait covers the gap (the player stays PLAYING). */
                g_restarted = true;
                g_restarting = true;
                g_start_tries = 0;
                g_reopen_tries = 0;
                g_last_reopen = now - REOPEN_RETRY_MS;
                printf("route: no stream start after %u requests, closing and reopening it\n", (unsigned)START_TRIES);
                hal_bt_stream_close();
            } else {
                hold_fail("the headset did not start the stream");
            }
        }
    } else {
        g_start_tries = 0;
        g_restarted = g_restarting = false;
        if (st == HAL_BT_STREAM_STREAMING && now - g_quiet_since >= AUDIO_ROUTE_SUSPEND_MS) {
            printf("route: not playing for %u ms (player state %d), suspending the stream\n", (unsigned)(now - g_quiet_since), (int)pi.state);
            hal_bt_stream_suspend();
        }
    }
}
