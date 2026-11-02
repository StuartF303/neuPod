// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <string.h>

#include "audio_engine.h"
#include "os.h"
#include "player.h"
#include "storage_service.h"

#define VOL_STEP_DB 2u
#define STARTUP_GRACE_MS 600u // the audio core takes a few ms to act on a command; do not read "stopped" too early

static os_mutex_t *g_mu;
static player_info_t g_info;
static uint32_t g_vol = AUDIO_VOL_DEFAULT_ATT_DB;
static uint32_t g_started_ms;
static uint32_t g_ended_base; // audio_pcm_ended_count() when the current file was requested

static void lock(void) { (void)os_mutex_lock(g_mu, OS_WAIT_FOREVER); }
static void unlock(void) { os_mutex_unlock(g_mu); }

void player_init(void) {
    g_mu = os_mutex_create();
    g_vol = audio_vol_clamp(AUDIO_VOL_DEFAULT_ATT_DB);
    g_info.vol_att_db = g_vol;
    (void)audio_set_volume(g_vol);
}

const char *player_tone_name(tone_mode_t mode) {
    switch (mode) {
    case TONE_1K: return "1 kHz tone";
    case TONE_IDENT: return "L/R ident";
    case TONE_SCALE: return "Scale";
    default: return "";
    }
}

void player_get(player_info_t *out) {
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    lock();
    uint32_t now = os_tick_ms();
    if (g_info.src == PLAYER_SRC_FILE && (g_info.state == PLAYER_PLAYING || g_info.state == PLAYER_PAUSED) &&
        audio_pcm_ended_count() != g_ended_base) {
        g_info.state = PLAYER_ENDED;
    } else if (g_info.src != PLAYER_SRC_NONE && (g_info.state == PLAYER_PLAYING || g_info.state == PLAYER_PAUSED) &&
               have && st.source == AUDIO_SRC_NONE && now - g_started_ms > STARTUP_GRACE_MS) {
        // The audio core dropped the source by itself: PCM data starved for too long (it muted the output).
        g_info.state = PLAYER_ERROR;
        snprintf(g_info.msg, sizeof g_info.msg, "Stopped: audio data ran dry");
    }
    g_info.vol_att_db = g_vol;
    if (have) {
        g_info.elapsed_s = g_info.rate != 0 && g_info.src == PLAYER_SRC_FILE ? st.pcm_frames / g_info.rate : 0;
        g_info.muted = st.muted != 0;
    }
    *out = g_info;
    unlock();
}

static void reset_info(player_src_t src, player_state_t state, const char *name) {
    memset(&g_info, 0, sizeof g_info);
    g_info.src = src;
    g_info.state = state;
    g_info.vol_att_db = g_vol;
    snprintf(g_info.name, sizeof g_info.name, "%s", name);
    g_started_ms = os_tick_ms();
}

void player_tone_toggle(tone_mode_t mode) {
    lock();
    bool same = g_info.src == PLAYER_SRC_TONE && g_info.tone == mode && g_info.state == PLAYER_PLAYING;
    if (same || mode == TONE_OFF) {
        reset_info(PLAYER_SRC_NONE, PLAYER_IDLE, "");
        unlock();
        storage_stop_request(); // in case a file stream is still being fed
        (void)audio_tone(TONE_OFF);
        return;
    }
    reset_info(PLAYER_SRC_TONE, PLAYER_PLAYING, player_tone_name(mode));
    g_info.tone = mode;
    unlock();
    storage_stop_request();
    (void)audio_tone((uint32_t)mode);
}

void player_play_file(const char *path, const char *name) {
    lock();
    reset_info(PLAYER_SRC_FILE, PLAYER_OPENING, name);
    g_ended_base = audio_pcm_ended_count();
    unlock();
    if (!storage_play_request(path)) {
        player_file_failed("Storage busy, try again");
    }
}

void player_pause_toggle(void) {
    lock();
    if (g_info.state == PLAYER_PLAYING) {
        g_info.state = PLAYER_PAUSED;
        (void)audio_pause(true);
    } else if (g_info.state == PLAYER_PAUSED) {
        g_info.state = PLAYER_PLAYING;
        (void)audio_pause(false);
    }
    unlock();
}

void player_stop(void) {
    lock();
    reset_info(PLAYER_SRC_NONE, PLAYER_IDLE, "");
    unlock();
    storage_stop_request();
    (void)audio_stop();
}

uint32_t player_volume_step(int32_t steps) {
    lock();
    int32_t att = (int32_t)g_vol - steps * (int32_t)VOL_STEP_DB; // louder = less attenuation
    if (att < 0) {
        att = 0;
    }
    g_vol = audio_vol_clamp((uint32_t)att);
    uint32_t v = g_vol;
    unlock();
    (void)audio_set_volume(v);
    return v;
}

void player_file_started(const wav_info_t *w) {
    lock();
    if (g_info.src == PLAYER_SRC_FILE && g_info.state == PLAYER_OPENING) {
        g_info.state = PLAYER_PLAYING;
        g_info.rate = w->rate;
        g_info.total_frames = w->frames;
        unsigned khz10 = w->rate / 100u; // 441 -> 44.1
        if (khz10 % 10u == 0) {
            snprintf(g_info.format, sizeof g_info.format, "WAV %u/%u", (unsigned)w->bits, khz10 / 10u);
        } else {
            snprintf(g_info.format, sizeof g_info.format, "WAV %u/%u.%u", (unsigned)w->bits, khz10 / 10u, khz10 % 10u);
        }
        if (w->channels == 1) {
            size_t n = strlen(g_info.format);
            snprintf(g_info.format + n, sizeof g_info.format - n, " mono");
        }
        g_started_ms = os_tick_ms();
    }
    unlock();
}

void player_file_failed(const char *msg) {
    lock();
    if (g_info.src == PLAYER_SRC_FILE) {
        g_info.state = PLAYER_ERROR;
        snprintf(g_info.msg, sizeof g_info.msg, "%s", msg);
    }
    unlock();
    printf("player: %s\n", msg);
}

uint32_t player_file_generation(void) { return g_ended_base; }
