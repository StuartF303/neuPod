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
static bool g_error_stopped;  // storage_stop_request() already sent for the current error

static const char *codec_error_text(uint32_t e) {
    switch (e) {
    case 1: return "Decoder could not open this file";
    case 2: return "Not enough memory to decode this file";
    case 3: return "File does not match its header";
    case 4: return "Decoding error (damaged data)";
    case 5: return "File is truncated or damaged";
    default: return "Decoder error";
    }
}

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
    bool stop_feed = false;
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    lock();
    uint32_t now = os_tick_ms();
    if (g_info.src == PLAYER_SRC_FILE && g_info.state == PLAYER_VERIFYING && have) {
        g_info.verify = st.verify_state;
        if (audio_pcm_ended_count() != g_ended_base && st.verify_state != AUDIO_VERIFY_RUNNING) {
            g_info.state = PLAYER_VERIFIED;
        }
        if (st.source == AUDIO_SRC_NONE && st.codec_error != 0 && now - g_started_ms > STARTUP_GRACE_MS) {
            g_info.state = PLAYER_ERROR;
            snprintf(g_info.msg, sizeof g_info.msg, "%s", codec_error_text(st.codec_error));
        }
    } else if (g_info.src == PLAYER_SRC_FILE && (g_info.state == PLAYER_PLAYING || g_info.state == PLAYER_PAUSED) &&
        audio_pcm_ended_count() != g_ended_base) {
        g_info.state = PLAYER_ENDED;
    } else if (g_info.src != PLAYER_SRC_NONE && (g_info.state == PLAYER_PLAYING || g_info.state == PLAYER_PAUSED) &&
               have && st.source == AUDIO_SRC_NONE && now - g_started_ms > STARTUP_GRACE_MS) {
        // The audio core dropped the source by itself: PCM data starved for too long (it muted the output).
        g_info.state = PLAYER_ERROR;
        snprintf(g_info.msg, sizeof g_info.msg, "%s",
                 st.codec_error != 0 ? codec_error_text(st.codec_error) : "Stopped: audio data ran dry");
    }
    g_info.vol_att_db = g_vol;
    if (have) {
        uint32_t frames = g_info.state == PLAYER_VERIFYING || g_info.state == PLAYER_VERIFIED ? st.decoded_frames : st.pcm_frames;
        g_info.elapsed_s = g_info.rate != 0 && g_info.src == PLAYER_SRC_FILE ? frames / g_info.rate : 0;
        g_info.muted = st.muted != 0;
    }
    stop_feed = g_info.state == PLAYER_ERROR && g_info.src == PLAYER_SRC_FILE && !g_error_stopped;
    if (stop_feed) {
        g_error_stopped = true;
    }
    *out = g_info;
    unlock();
    if (stop_feed) {
        storage_stop_request(); // the audio core stopped the stream: stop reading the card for it too
    }
}

static void reset_info(player_src_t src, player_state_t state, const char *name) {
    memset(&g_info, 0, sizeof g_info);
    g_info.src = src;
    g_info.state = state;
    g_info.vol_att_db = g_vol;
    g_error_stopped = false;
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

void player_verify_file(const char *path, const char *name) {
    lock();
    reset_info(PLAYER_SRC_FILE, PLAYER_OPENING, name);
    g_ended_base = audio_pcm_ended_count();
    unlock();
    if (!storage_verify_request(path)) {
        player_file_failed("Storage busy, try again");
    }
}

void player_log_end(void) {
    player_info_t i;
    player_get(&i);
    if (i.src == PLAYER_SRC_FILE && (i.state == PLAYER_VERIFIED || i.verify >= AUDIO_VERIFY_OK)) {
        static const char *const v[] = {"?", "running", "MD5 OK", "MD5 MISMATCH", "no MD5 stored in the file"};
        printf("verify: '%s' %s (%u frames decoded)\n", i.name, i.verify < 5 ? v[i.verify] : "?", (unsigned)(i.total_frames));
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

void player_file_started(const player_file_t *f) {
    lock();
    if (g_info.src == PLAYER_SRC_FILE && g_info.state == PLAYER_OPENING) {
        g_info.state = f->verify ? PLAYER_VERIFYING : PLAYER_PLAYING;
        g_info.rate = f->rate;
        g_info.total_frames = f->total_frames;
        g_info.tags = f->tags;
        g_info.title_is_name = f->title_is_name;
        g_info.estimated = f->estimated;
        g_info.codec = f->codec;
        snprintf(g_info.format, sizeof g_info.format, "%s", f->format);
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
