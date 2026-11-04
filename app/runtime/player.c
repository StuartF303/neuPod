// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <string.h>

#include "audio_engine.h"
#include "os.h"
#include "play_queue.h"
#include "player.h"
#include "settings_store.h"
#include "storage_service.h"

#define VOL_STEP_DB 2u
#define STARTUP_GRACE_MS 600u // the audio core takes a few ms to act on a command; do not read "stopped" too early

static os_mutex_t *g_mu;
static player_info_t g_info;
static uint32_t g_vol = AUDIO_VOL_DEFAULT_ATT_DB;
static uint32_t g_started_ms;
static uint32_t g_ended_base; // audio_pcm_ended_count() when the current file was requested
static uint32_t g_changes_seen; // audio_track_change_count() as last looked at: gapless handoffs are applied when it moves
static bool g_vol_touched;    // the volume was changed since boot (a late settings load must not override the user)
static bool g_error_stopped;  // storage_stop_request() already sent for the current error
static char g_path[PLAYER_PATH_MAX]; // the file last played or verified
static play_queue_t g_q;      // the folder being played through (count 0: a single file, a test tone or nothing)
static bool g_auto;           // the current track was started by the queue (not chosen by the user): a track that cannot be opened is skipped
static bool g_next_valid;     // the audio core was told to continue with the next queued track by itself; its details:
static player_file_t g_next_pf;
static uint16_t g_next_entry = PLAY_QUEUE_NO_ENTRY; // queue entry the audio core was told to continue with (the modes may change after)
static uint16_t g_offer_entry = PLAY_QUEUE_NO_ENTRY;
// An announcement is only accepted for the offer it answers: any restart of playback (reset_info) bumps g_gen, so the answer to
// an offer made before a skip, stop or new start is ignored instead of marking a successor that no longer exists.
static pq_snapshot_t g_snap; // the queue before the move being started (under the lock; g_gen rejects a stale rollback)
static uint16_t g_skips;     // consecutive unplayable tracks skipped under repeat-all (reset by any successful start); capped at the queue length
static uint32_t g_gen;
static uint32_t g_offer_gen;
static bool g_offered;

// What to do once the lock is released (storage requests may wait on the storage queue; never hold the player lock for that).
typedef struct {
    bool start;
    bool stop;
    bool snap;         // g_snap holds the queue as it was before a move that may have re-shuffled it (repeat-all wrap)
    bool rollback;     // the queue position was advanced for this start: put it back if the start cannot even be requested
    uint16_t prev_pos;
    uint32_t gen;
    char path[PLAYER_PATH_MAX];
} action_t;

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
    pq_clear(&g_q);
    (void)audio_set_volume(g_vol);
}

bool player_gapless_enabled(void) {
    uint8_t v;
    return !settings_get_u8(SETTING_GAPLESS, &v) || v != 0; // default On
}

void player_set_gapless(bool on) { settings_set_u8(SETTING_GAPLESS, on ? 1u : 0u); }

pq_shuffle_t player_shuffle(void) {
    uint8_t v;
    return settings_get_u8(SETTING_SHUFFLE, &v) && v == PQ_SHUFFLE_SONGS ? PQ_SHUFFLE_SONGS : PQ_SHUFFLE_OFF; // default Off
}

pq_repeat_t player_repeat(void) {
    uint8_t v;
    return settings_get_u8(SETTING_REPEAT, &v) && (v == PQ_REPEAT_ONE || v == PQ_REPEAT_ALL) ? (pq_repeat_t)v : PQ_REPEAT_OFF;
}

// The shuffle generator is seeded from the tick count at the moment of the user's action, mixed with its previous state.
static void reseed(void) { pq_seed(&g_q, os_tick_ms() * 2654435761u ^ g_q.rng); }

void player_set_shuffle(pq_shuffle_t mode) {
    settings_set_u8(SETTING_SHUFFLE, (uint8_t)mode);
    lock();
    reseed();
    pq_set_shuffle(&g_q, mode); // the track playing stays current; a handoff already announced to the audio core still happens
    unlock();
}

void player_set_repeat(pq_repeat_t mode) {
    settings_set_u8(SETTING_REPEAT, (uint8_t)mode);
    lock();
    pq_set_repeat(&g_q, mode);
    unlock();
}

const char *player_tone_name(tone_mode_t mode) {
    switch (mode) {
    case TONE_1K: return "1 kHz tone";
    case TONE_IDENT: return "L/R ident";
    case TONE_SCALE: return "Scale";
    default: return "";
    }
}

static void reset_info(player_src_t src, player_state_t state, const char *name) {
    memset(&g_info, 0, sizeof g_info);
    g_info.src = src;
    g_info.state = state;
    g_info.vol_att_db = g_vol;
    g_error_stopped = false;
    g_next_valid = false;
    g_offered = false;
    g_gen++;
    snprintf(g_info.name, sizeof g_info.name, "%s", name);
    g_started_ms = os_tick_ms();
}

static void go_idle(action_t *a) {
    reset_info(PLAYER_SRC_NONE, PLAYER_IDLE, "");
    pq_clear(&g_q);
    g_auto = false;
    a->stop = true;
}

// Remember the queue as it is, so that a start request that is refused puts it back exactly (a wrap re-shuffles the order).
static void prepare_rollback(action_t *a) {
    a->prev_pos = g_q.pos;
    a->rollback = true;
    if (g_q.shuffle) {
        pq_snapshot(&g_q, &g_snap);
        a->snap = true;
    }
}

// Start queue entry g_q.pos (hard start: the audio core ramps down, re-clocks if needed, ramps up).
static void begin_track(action_t *a, bool automatic, const char *why) {
    if (!pq_path(&g_q, g_q.pos, a->path, sizeof a->path)) {
        go_idle(a);
        return;
    }
    snprintf(g_path, sizeof g_path, "%s", a->path);
    reset_info(PLAYER_SRC_FILE, PLAYER_OPENING, pq_name(&g_q, g_q.pos));
    g_ended_base = audio_pcm_ended_count();
    g_changes_seen = audio_track_change_count();
    g_auto = automatic;
    if (!automatic) {
        g_skips = 0;
    }
    a->start = true;
    a->gen = g_gen;
    printf("player: track %u/%u '%s' (%s)\n", (unsigned)(g_q.pos + 1u), (unsigned)g_q.count, pq_name(&g_q, g_q.pos), why);
}

static void run_action(const action_t *a) {
    if (a->stop) {
        storage_stop_request();
        (void)audio_stop();
    }
    if (a->start && !storage_play_request(a->path)) {
        lock();
        if (a->rollback && g_gen == a->gen && g_q.count != 0) {
            if (a->snap) {
                pq_restore(&g_q, &g_snap); // a wrap re-shuffled the order: put the whole queue back
            } else {
                g_q.pos = a->prev_pos; // the next press must not skip the entry that never started
            }
        }
        unlock();
        player_file_failed("Storage busy, try again");
    }
}

static void apply_file(const player_file_t *f) {
    g_info.rate = f->rate;
    g_info.total_frames = f->total_frames;
    g_info.tags = f->tags;
    g_info.title_is_name = f->title_is_name;
    g_info.estimated = f->estimated;
    g_info.codec = f->codec;
    snprintf(g_info.format, sizeof g_info.format, "%s", f->format);
    g_started_ms = os_tick_ms();
}

// The audio core started the next track by itself, gaplessly: show it.
static void apply_next(void) {
    if (!g_next_valid) {
        return;
    }
    g_next_valid = false;
    pq_move_t mv = pq_commit(&g_q, g_next_entry);
    if (mv == PQ_END) {
        return;
    }
    snprintf(g_info.name, sizeof g_info.name, "%s", pq_name(&g_q, g_q.pos));
    (void)pq_path(&g_q, g_q.pos, g_path, sizeof g_path);
    g_info.state = PLAYER_PLAYING;
    g_info.msg[0] = '\0';
    g_info.elapsed_s = 0;
    apply_file(&g_next_pf);
    printf("player: track %u/%u '%s' (gapless%s)\n", (unsigned)(g_q.pos + 1u), (unsigned)g_q.count, g_info.name,
           mv == PQ_RESTART ? " repeat" : mv == PQ_WRAPPED ? " wrap" : "");
}

// Fold in what the audio core reports. Lock held.
static void service(action_t *a, bool have, const audio_stats_t *st) {
    uint32_t now = os_tick_ms();
    uint32_t chg = audio_track_change_count();
    if (chg != g_changes_seen) {
        g_changes_seen = chg;
        if (g_info.src == PLAYER_SRC_FILE && (g_info.state == PLAYER_PLAYING || g_info.state == PLAYER_PAUSED)) {
            apply_next();
        }
    }
    if (g_info.src == PLAYER_SRC_FILE && g_info.state == PLAYER_VERIFYING && have) {
        g_info.verify = st->verify_state;
        if (audio_pcm_ended_count() != g_ended_base && st->verify_state != AUDIO_VERIFY_RUNNING) {
            g_info.state = PLAYER_VERIFIED;
        }
        if (st->source == AUDIO_SRC_NONE && st->codec_error != 0 && now - g_started_ms > STARTUP_GRACE_MS) {
            g_info.state = PLAYER_ERROR;
            snprintf(g_info.msg, sizeof g_info.msg, "%s", codec_error_text(st->codec_error));
        }
    } else if (g_info.src == PLAYER_SRC_FILE && (g_info.state == PLAYER_PLAYING || g_info.state == PLAYER_PAUSED) &&
               audio_pcm_ended_count() != g_ended_base) {
        // The track played to its end and nothing followed in the audio core (gapless off, another sample rate, a fault-free end):
        // go on with the queue the ordinary way, or stop at its end.
        prepare_rollback(a);
        pq_move_t mv = g_q.count != 0 ? pq_auto_next(&g_q) : PQ_END;
        if (mv != PQ_END) {
            begin_track(a, true, mv == PQ_RESTART ? "repeat" : mv == PQ_WRAPPED ? "wrap" : "auto");
        } else {
            printf("player: queue end\n");
            go_idle(a);
        }
    } else if (g_info.src != PLAYER_SRC_NONE && (g_info.state == PLAYER_PLAYING || g_info.state == PLAYER_PAUSED) &&
               have && st->source == AUDIO_SRC_NONE && now - g_started_ms > STARTUP_GRACE_MS) {
        // The audio core dropped the source by itself: PCM data starved for too long (it muted the output).
        g_info.state = PLAYER_ERROR;
        snprintf(g_info.msg, sizeof g_info.msg, "%s",
                 st->codec_error != 0 ? codec_error_text(st->codec_error) : "Stopped: audio data ran dry");
    } else if (g_info.src == PLAYER_SRC_FILE && g_info.state == PLAYER_ERROR && g_auto && g_q.count != 0 &&
               (g_q.repeat == PQ_REPEAT_ALL ? g_skips < g_q.count : pq_has_next(&g_q))) {
        // A track the queue moved on to cannot be played: skip it rather than stopping the album. Under repeat-all this wraps at the end,
        // but never more than one lap of unplayable tracks in a row (an all-broken queue ends in the error).
        printf("player: skipping '%s' (%s)\n", g_info.name, g_info.msg);
        g_skips++;
        prepare_rollback(a);
        (void)pq_next(&g_q);
        begin_track(a, true, "skip");
        a->stop = true; // the failed track may still be feeding or have left the audio core half set up
    }
}

void player_service(void) {
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    action_t a = {0};
    lock();
    service(&a, have, &st);
    unlock();
    run_action(&a);
}

void player_get(player_info_t *out) {
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    action_t a = {0};
    lock();
    service(&a, have, &st);
    g_info.vol_att_db = g_vol;
    if (have) {
        uint32_t frames = g_info.state == PLAYER_VERIFYING || g_info.state == PLAYER_VERIFIED ? st.decoded_frames : st.pcm_frames;
        g_info.elapsed_s = g_info.rate != 0 && g_info.src == PLAYER_SRC_FILE ? frames / g_info.rate : 0;
        g_info.muted = st.muted != 0;
    }
    bool stop_feed = g_info.state == PLAYER_ERROR && g_info.src == PLAYER_SRC_FILE && !g_error_stopped;
    if (stop_feed) {
        g_error_stopped = true;
    }
    g_info.queue_len = g_info.src == PLAYER_SRC_FILE ? g_q.count : 0;
    g_info.queue_pos = g_info.queue_len != 0 ? (uint16_t)(g_q.pos + 1u) : 0;
    g_info.shuffle = g_info.queue_len != 0 ? g_q.shuffle : 0;
    g_info.repeat = g_info.queue_len != 0 ? g_q.repeat : 0;
    *out = g_info;
    unlock();
    run_action(&a);
    if (stop_feed && !a.start) {
        storage_stop_request(); // the audio core stopped the stream: stop reading the card for it too
    }
}

void player_tone_toggle(tone_mode_t mode) {
    lock();
    bool same = g_info.src == PLAYER_SRC_TONE && g_info.tone == mode && g_info.state == PLAYER_PLAYING;
    pq_clear(&g_q);
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
    pq_clear(&g_q);
    g_auto = false;
    snprintf(g_path, sizeof g_path, "%s", path);
    reset_info(PLAYER_SRC_FILE, PLAYER_OPENING, name);
    g_ended_base = audio_pcm_ended_count();
    g_changes_seen = audio_track_change_count();
    printf("player: track 1/1 '%s' (start)\n", name);
    unlock();
    if (!storage_play_request(path)) {
        player_file_failed("Storage busy, try again");
    }
}

void player_play_folder(const char *dir, const file_entry_t *entries, uint32_t count, uint32_t index) {
    action_t a = {0};
    lock();
    reseed();
    pq_set_repeat(&g_q, player_repeat()); // the saved modes apply to the queue being built (the settings may have loaded since boot)
    g_q.shuffle = (uint8_t)player_shuffle();
    if (!pq_build(&g_q, dir, entries, count, index)) {
        unlock();
        return;
    }
    begin_track(&a, false, "start");
    unlock();
    run_action(&a);
}

void player_next(void) {
    action_t a = {0};
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    lock();
    service(&a, have, &st); // a gapless handoff that just happened moves the queue first
    if (g_info.src == PLAYER_SRC_FILE && g_q.count != 0 && g_info.state != PLAYER_VERIFYING && g_info.state != PLAYER_VERIFIED) {
        prepare_rollback(&a);
        pq_move_t mv = pq_next(&g_q);
        if (mv != PQ_END) {
            begin_track(&a, false, mv == PQ_WRAPPED ? "wrap" : "next");
        } else {
            printf("player: queue end\n");
            go_idle(&a); // Next on the last track: stop, Now Playing goes idle
        }
    }
    unlock();
    run_action(&a);
}

void player_prev(void) {
    action_t a = {0};
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    lock();
    service(&a, have, &st);
    if (g_info.src == PLAYER_SRC_FILE && g_q.count != 0 && g_info.state != PLAYER_VERIFYING && g_info.state != PLAYER_VERIFIED) {
        uint32_t elapsed = have && g_info.rate != 0 ? st.pcm_frames / g_info.rate : g_info.elapsed_s;
        a.prev_pos = g_q.pos;
        a.rollback = true;
        pq_move_t mv = pq_prev(&g_q, elapsed); // PQ_MOVED: the one before; PQ_RESTART: this one again from the start
        begin_track(&a, false, mv == PQ_MOVED ? "prev" : "restart");
    }
    unlock();
    run_action(&a);
}

bool player_next_track(char *path, size_t n) {
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    action_t a = {0};
    lock();
    service(&a, have, &st); // a handoff the audio core just completed must have moved the queue before we look at it
    uint16_t entry;
    bool ok = player_gapless_enabled() && g_info.src == PLAYER_SRC_FILE &&
              (g_info.state == PLAYER_PLAYING || g_info.state == PLAYER_PAUSED) && !g_next_valid && pq_peek_auto(&g_q, &entry) &&
              pq_entry_path(&g_q, entry, path, n);
    if (ok) {
        g_offered = true;
        g_offer_gen = g_gen;
        g_offer_entry = entry;
    }
    unlock();
    run_action(&a);
    return ok;
}

void player_next_announced(const player_file_t *f) {
    lock();
    if (g_offered && g_offer_gen == g_gen && g_info.src == PLAYER_SRC_FILE && g_q.count != 0) {
        g_offered = false;
        g_next_entry = g_offer_entry;
        g_next_pf = *f;
        g_next_valid = true;
    }
    unlock();
}

void player_verify_file(const char *path, const char *name) {
    lock();
    pq_clear(&g_q);
    g_auto = false;
    snprintf(g_path, sizeof g_path, "%s", path);
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
    } else if (g_info.src == PLAYER_SRC_FILE && (g_info.state == PLAYER_ENDED || g_info.state == PLAYER_VERIFIED) && g_path[0] != '\0') {
        // A completed MD5 check: play the same file again from the start, with sound.
        char path[PLAYER_PATH_MAX], name[sizeof g_info.name];
        snprintf(path, sizeof path, "%s", g_path);
        snprintf(name, sizeof name, "%s", g_info.name);
        unlock();
        player_play_file(path, name);
        return;
    }
    unlock();
}

void player_stop(void) {
    action_t a = {0};
    lock();
    go_idle(&a);
    unlock();
    run_action(&a);
}

uint32_t player_volume_step(int32_t steps) {
    lock();
    int32_t att = (int32_t)g_vol - steps * (int32_t)VOL_STEP_DB; // louder = less attenuation
    if (att < 0) {
        att = 0;
    }
    g_vol = audio_vol_clamp((uint32_t)att);
    g_vol_touched = true;
    uint32_t v = g_vol;
    unlock();
    (void)audio_set_volume(v);
    settings_set_u8(SETTING_VOLUME_ATT_DB, (uint8_t)v); // persisted by the storage task a moment later
    return v;
}

void player_restore_volume(uint32_t att_db) {
    // Headphone safety (docs/audio.md): a restored volume is never LOUDER than the power-on default, whatever was saved.
    if (att_db < PLAYER_RESTORE_MIN_ATT_DB) {
        att_db = PLAYER_RESTORE_MIN_ATT_DB;
    }
    lock();
    bool apply = !g_vol_touched;
    if (apply) {
        g_vol = audio_vol_clamp(att_db);
        att_db = g_vol;
    }
    unlock();
    if (apply) {
        (void)audio_set_volume(att_db);
    }
}

void player_file_started(const player_file_t *f) {
    lock();
    if (g_info.src == PLAYER_SRC_FILE && g_info.state == PLAYER_OPENING) {
        g_info.state = f->verify ? PLAYER_VERIFYING : PLAYER_PLAYING;
        g_next_valid = false;
        g_skips = 0;
        // The audio core has flushed and restarted: handoffs counted until now (the old track's ramp-down could still have crossed a
        // boundary) are not ours.
        g_changes_seen = audio_track_change_count();
        apply_file(f);
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
