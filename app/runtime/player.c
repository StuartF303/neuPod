// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <string.h>

#include "audio_engine.h"
#include "os.h"
#include "play_queue.h"
#include "player.h"
#include "resume.h"
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
// Resume (resume.h). g_base_frames: the stream started at this frame of the track, so the position is g_base_frames + the audio core's count.
static uint32_t g_base_frames;
static uint64_t g_file_size;
static bool g_resume_boot;        // the track being opened is a boot resume (a failed open is dropped quietly)
static bool g_resume_play;        // ... and it is to play (setting "Resume playing on power-up" and the record was playing), not stay paused
static resume_state_t g_rs;       // a record waiting to be stored once the player lock is released (settings_put takes its own lock)
static bool g_rs_has, g_rs_clear;
static resume_why_t g_rs_why;
#if RESUME_WRITE_WHILE_PLAYING
static uint32_t g_rs_periodic_ms; // time of the last periodic attempt
#endif
// Seek (player_seek): the same file is restarted at a frame. The state stays OPENING until the storage task reports the start; the UI is
// handed the state from before the seek and the target as the position meanwhile.
static bool g_seeking;
// Start requests posted to the storage task that have not been answered yet (player_file_started() or player_file_failed()). The storage task
// answers every REQ_PLAY / REQ_VERIFY exactly once and in order, so the answer that belongs to the LAST request is the one that leaves this at 0:
// the earlier ones (a seek followed by another seek, Next, or a seek to the end) describe a stream that has already been replaced and are dropped.
// (Matching on path and frame was the alternative: it cannot tell two starts of the same file apart when the frame is the same, and a resume
// start of an MP3 lands on a different frame than the one asked for.)
static uint32_t g_starts_out;
// Identifies the track being played: moves on a new track (any start, a gapless handoff), not on a seek within it. The UI ties a scrub to it.
static uint32_t g_track_id;
static bool g_seek_paused;        // the seek was made while paused: the restarted track stays paused
static uint64_t g_seek_to;
static resume_state_t g_rs_tmp;    // scratch for the record written when a seek's restart is up
static output_path_t g_out_path;  // see player.h
static bool g_hp_at_boot;

// What to do once the lock is released (storage requests may wait on the storage queue; never hold the player lock for that).
typedef struct {
    bool start;
    bool stop;
    bool snap;         // g_snap holds the queue as it was before a move that may have re-shuffled it (repeat-all wrap)
    uint64_t seek_prev_to;    // ... and the target of the seek that was under way
    player_state_t seek_prev; // a seek: the state to give back if the start cannot even be requested (PLAYER_IDLE = not a seek)
    bool rollback;     // the queue position was advanced for this start: put it back if the start cannot even be requested
    uint16_t prev_pos;
    uint32_t gen;
    uint32_t frame;    // start position (resume), 0 = the beginning
    char path[PLAYER_PATH_MAX];
} action_t;

static const char *codec_error_text(uint32_t e) {
    switch (e) {
    case 1: return "Decoder could not open this file";
    case 2: return "Not enough memory to decode this file";
    case 3: return "File does not match its header";
    case 4: return "Decoding error (damaged data)";
    case 5: return "File is truncated or damaged";
    case AUDIO_ERR_RATE_NOT_ON_BT: return "Not supported on Bluetooth (44.1 kHz only)";
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

bool player_resume_autoplay(void) {
    uint8_t v;
    return settings_get_u8(SETTING_RESUME_PLAY, &v) && v == 1; // default Off
}

void player_set_resume_autoplay(bool on) { settings_set_u8(SETTING_RESUME_PLAY, on ? 1u : 0u); }

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
    g_base_frames = 0;
    g_file_size = 0;
    g_resume_boot = false;
    g_resume_play = false;
    g_seeking = false;
    g_track_id++;
    g_gen++;
    snprintf(g_info.name, sizeof g_info.name, "%s", name);
    g_started_ms = os_tick_ms();
}

// ---- Resume bookkeeping: records are queued under the player lock and stored after it is released ----

// Describe what plays now. Lock held. Needs the audio core's count for the position.
static bool rs_snapshot(resume_state_t *r, bool have, const audio_stats_t *st) {
    if (!have || g_info.src != PLAYER_SRC_FILE || g_q.count == 0 || (g_info.state != PLAYER_PLAYING && g_info.state != PLAYER_PAUSED) ||
        !pq_path(&g_q, g_q.pos, r->path, sizeof r->path)) {
        return false;
    }
    r->index = pq_entry(&g_q, g_q.pos); // the folder entry, not the slot: under shuffle the slot order is different at the next boot
    r->size = g_file_size;
    r->frame = g_base_frames + st->pcm_frames;
    r->playing = g_info.state == PLAYER_PLAYING;
    return true;
}

static void rs_queue(const resume_state_t *r, resume_why_t why) {
    g_rs = *r;
    g_rs_why = why;
    g_rs_has = true;
    g_rs_clear = false;
}

// A new track of the queue starts from its beginning. Lock held.
static void rs_queue_track_start(void) {
    resume_state_t r;
    memset(&r, 0, sizeof r);
    if (g_q.count != 0 && pq_path(&g_q, g_q.pos, r.path, sizeof r.path)) {
        r.index = pq_entry(&g_q, g_q.pos);
        r.size = g_file_size;
        r.playing = true;
        rs_queue(&r, RESUME_EVENT);
    }
}

static void rs_queue_clear(void) {
    g_rs_clear = true;
    g_rs_has = false;
}

static void rs_flush(void) {
    lock();
    resume_state_t r = g_rs;
    resume_why_t why = g_rs_why;
    bool has = g_rs_has, clr = g_rs_clear;
    g_rs_has = g_rs_clear = false;
    unlock();
    if (has) {
        (void)resume_save(&r, why, os_tick_ms());
    } else if (clr) {
        resume_clear();
    }
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
    a->frame = 0;
    rs_queue_track_start();
    printf("player: track %u/%u '%s' (%s)\n", (unsigned)(g_q.pos + 1u), (unsigned)g_q.count, pq_name(&g_q, g_q.pos), why);
}

static void run_action(const action_t *a) {
    rs_flush();
    if (a->stop) {
        storage_stop_request();
        (void)audio_stop();
    }
    if (a->start) {
        lock();
        g_starts_out++; // before the request is posted: the answer can come at once
        unlock();
    }
    if (a->start && !(a->frame != 0 ? storage_play_request_at(a->path, a->frame) : storage_play_request(a->path))) {
        lock();
        if (a->seek_prev != PLAYER_IDLE && g_seeking && g_gen == a->gen) {
            if (g_starts_out > 0) {
                g_starts_out--;
            }
            if (g_starts_out == 0) {
                g_seeking = false; // the old stream is untouched and still plays: nothing to report
                g_info.state = a->seek_prev;
            } else {
                g_seek_to = a->seek_prev_to; // an earlier seek's start is still on its way: its answer finishes the job, at its target
            }
            unlock();
            return;
        }
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
    g_info.seekable = f->seekable && !f->verify;
    g_base_frames = (uint32_t)f->start_frame;
    g_file_size = f->file_size;
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
    g_track_id++;
    snprintf(g_info.name, sizeof g_info.name, "%s", pq_name(&g_q, g_q.pos));
    (void)pq_path(&g_q, g_q.pos, g_path, sizeof g_path);
    g_info.state = PLAYER_PLAYING;
    g_info.msg[0] = '\0';
    g_info.elapsed_s = 0;
    apply_file(&g_next_pf);
    rs_queue_track_start();
    printf("player: track %u/%u '%s' (gapless%s)\n", (unsigned)(g_q.pos + 1u), (unsigned)g_q.count, g_info.name,
           mv == PQ_RESTART ? " repeat" : mv == PQ_WRAPPED ? " wrap" : "");
}

// The track is over (it played out, or a seek went to its end): go on with the queue the ordinary way, or stop at its end. Lock held.
static void track_over(action_t *a) {
    prepare_rollback(a);
    pq_move_t mv = g_q.count != 0 ? pq_auto_next(&g_q) : PQ_END;
    if (mv != PQ_END) {
        begin_track(a, true, mv == PQ_RESTART ? "repeat" : mv == PQ_WRAPPED ? "wrap" : "auto");
    } else {
        printf("player: queue end\n");
        if (g_q.count != 0) {
            rs_queue_clear(); // the album finished: nothing to resume
        }
        go_idle(a);
    }
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
        track_over(a);
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
#if RESUME_WRITE_WHILE_PLAYING
    uint32_t now = os_tick_ms();
    if (g_info.state == PLAYER_PLAYING && now - g_rs_periodic_ms >= RESUME_PERIOD_MS && resume_periodic_due(now)) {
        g_rs_periodic_ms = now;
        resume_state_t r;
        if (rs_snapshot(&r, have, &st)) {
            rs_queue(&r, RESUME_PERIODIC);
        }
    }
#endif
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
        g_info.elapsed_s = g_info.rate != 0 && g_info.src == PLAYER_SRC_FILE ? (g_base_frames + frames) / g_info.rate : 0;
        g_info.muted = st.muted != 0;
    }
    bool stop_feed = g_info.state == PLAYER_ERROR && g_info.src == PLAYER_SRC_FILE && !g_error_stopped;
    if (stop_feed) {
        g_error_stopped = true;
    }
    g_info.track_id = g_track_id;
    g_info.pos_frames = have && g_info.rate != 0 && g_info.src == PLAYER_SRC_FILE ? g_base_frames + (uint64_t)st.pcm_frames : 0;
    g_info.queue_len = g_info.src == PLAYER_SRC_FILE ? g_q.count : 0;
    g_info.queue_pos = g_info.queue_len != 0 ? (uint16_t)(g_q.pos + 1u) : 0;
    g_info.shuffle = g_info.queue_len != 0 ? g_q.shuffle : 0;
    g_info.repeat = g_info.queue_len != 0 ? g_q.repeat : 0;
    *out = g_info;
    if (g_seeking && g_info.state == PLAYER_OPENING) { // the restart at the target is under way: look as before, at the target
        out->state = g_seek_paused ? PLAYER_PAUSED : PLAYER_PLAYING;
        out->pos_frames = g_seek_to;
        out->elapsed_s = g_info.rate != 0 ? (uint32_t)(g_seek_to / g_info.rate) : 0;
    }
    out->seekable = g_info.seekable && (out->state == PLAYER_PLAYING || out->state == PLAYER_PAUSED);
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
    g_starts_out++;
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

player_resume_t player_resume_folder(const char *dir, const file_entry_t *entries, uint32_t count, uint32_t index, uint32_t frame, bool play) {
    action_t a = {0};
    lock();
    if (g_info.state != PLAYER_IDLE || g_info.src != PLAYER_SRC_NONE) {
        unlock();
        return PLAYER_RESUME_BUSY;
    }
    reseed();
    pq_set_repeat(&g_q, player_repeat()); // the saved modes, as for a track chosen in Files
    g_q.shuffle = (uint8_t)player_shuffle();
    if (!pq_build(&g_q, dir, entries, count, index)) {
        unlock();
        return PLAYER_RESUME_INVALID;
    }
    begin_track(&a, false, "resume");
    g_rs_has = false; // the saved position stays as it is until the user does something
    g_resume_boot = true;
    g_resume_play = play; // play: the track just starts (the settings load restored the volume, capped by the path policy, just before)
    a.frame = frame;
    unlock();
    run_action(&a);
    return PLAYER_RESUME_OK;
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
            rs_queue_clear();
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
        uint32_t elapsed = have && g_info.rate != 0 ? (g_base_frames + st.pcm_frames) / g_info.rate : g_info.elapsed_s;
        a.prev_pos = g_q.pos;
        a.rollback = true;
        pq_move_t mv = pq_prev(&g_q, elapsed); // PQ_MOVED: the one before; PQ_RESTART: this one again from the start
        begin_track(&a, false, mv == PQ_MOVED ? "prev" : "restart");
    }
    unlock();
    run_action(&a);
}

bool player_seek(uint64_t frame) {
    action_t a = {0};
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    lock();
    service(&a, have, &st); // a track that just ended (or a handoff) is settled first: the seek must not land on the wrong track
    bool ok = !a.start && !a.stop && g_info.src == PLAYER_SRC_FILE && g_info.seekable && g_info.rate != 0 && g_info.total_frames != 0 &&
              ((g_info.state == PLAYER_PLAYING || g_info.state == PLAYER_PAUSED) || (g_seeking && g_info.state == PLAYER_OPENING)) &&
              g_path[0] != '\0';
    if (ok && frame >= g_info.total_frames) {
        track_over(&a); // the end of the track, as if it had played out
    } else if (ok) {
        player_state_t prev = g_info.state;
        if (!g_seeking) {
            g_seek_paused = prev == PLAYER_PAUSED;
        } else {
            prev = g_seek_paused ? PLAYER_PAUSED : PLAYER_PLAYING; // a second seek before the first one started: the state to go back to is the original
        }
        // Nothing is reset here on purpose. service() above has just taken in the ended / handoff counts; player_file_started() re-bases both
        // when the restart is up (the old stream's ramp-down can still end or cross a boundary in between) and drops an announced successor. If the
        // start cannot even be requested the old stream still plays with its successor announced, which must keep working
        // (a_refused_seek_keeps_the_announced_successor).
        a.seek_prev_to = g_seek_to;
        g_seeking = true;
        g_seek_to = frame;
        g_info.state = PLAYER_OPENING;
        snprintf(a.path, sizeof a.path, "%s", g_path);
        a.start = true;
        a.gen = g_gen;
        a.frame = (uint32_t)frame;
        a.seek_prev = prev;
        printf("player: seek to frame %u (%u s)\n", (unsigned)frame, (unsigned)(frame / g_info.rate));
    }
    unlock();
    run_action(&a);
    return ok;
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
    g_starts_out++;
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
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    lock();
    if (g_seeking && g_info.state == PLAYER_OPENING) {
        g_seek_paused = !g_seek_paused; // Play / Pause during the restart of a seek: applied when the restarted track is up
        unlock();
        return;
    }
    if (g_info.state == PLAYER_PLAYING || g_info.state == PLAYER_PAUSED) {
        bool pause = g_info.state == PLAYER_PLAYING;
        g_info.state = pause ? PLAYER_PAUSED : PLAYER_PLAYING;
        (void)audio_pause(pause);
        resume_state_t r;
        if (rs_snapshot(&r, have, &st)) {
            rs_queue(&r, RESUME_EVENT);
        }
        unlock();
        rs_flush();
        return;
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

void player_pause(void) {
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    lock();
    if (g_info.state == PLAYER_PLAYING) {
        g_info.state = PLAYER_PAUSED;
        (void)audio_pause(true);
        resume_state_t r;
        if (rs_snapshot(&r, have, &st)) {
            rs_queue(&r, RESUME_EVENT);
        }
        unlock();
        rs_flush();
        return;
    }
    unlock();
}

void player_stop(void) {
    action_t a = {0};
    audio_stats_t st;
    bool have = audio_stats_get(&st);
    lock();
    resume_state_t r;
    if (rs_snapshot(&r, have, &st)) {
        r.playing = false; // Stop keeps the place: the next boot offers this track paused here
        rs_queue(&r, RESUME_EVENT);
    }
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
    settings_set_u8(SETTING_VOLUME_PATH, (uint8_t)g_out_path); // ... together with the path it was set for
    return v;
}

void player_set_output_path(output_path_t path) {
    lock();
    g_out_path = path;
    unlock();
}

void player_set_headphones_at_boot(bool inserted) {
    lock();
    g_hp_at_boot = inserted;
    unlock();
}

uint32_t player_volume_restore_att(uint32_t saved_att, output_path_t saved_path, output_path_t cur_path, bool headphones_at_boot) {
    // Headphone safety (docs/bluetooth.md "Volume restore policy"): honour the saved level on the same known path; otherwise never louder than the default.
    bool cap = saved_path == OUT_PATH_UNKNOWN || cur_path == OUT_PATH_UNKNOWN || saved_path != cur_path ||
               (headphones_at_boot && saved_path == OUT_PATH_LINE);
    return cap && saved_att < PLAYER_RESTORE_MIN_ATT_DB ? PLAYER_RESTORE_MIN_ATT_DB : saved_att;
}

void player_restore_volume(uint32_t att_db) {
    uint8_t saved_path = OUT_PATH_UNKNOWN; // a volume saved before the path was recorded: unknown, so capped
    if (!settings_get_u8(SETTING_VOLUME_PATH, &saved_path) || saved_path > OUT_PATH_BT) {
        saved_path = OUT_PATH_UNKNOWN;
    }
    lock();
    att_db = player_volume_restore_att(att_db, (output_path_t)saved_path, g_out_path, g_hp_at_boot);
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
    bool save = false;
    lock();
    if (g_starts_out > 0 && --g_starts_out > 0) {
        unlock(); // the answer to a start that a later one has replaced
        return;
    }
    if (g_info.src == PLAYER_SRC_FILE && g_info.state == PLAYER_OPENING) {
        g_info.state = f->verify ? PLAYER_VERIFYING : PLAYER_PLAYING;
        if (g_seeking) { // the restart at the target: put the transport back as it was before
            g_info.state = g_seek_paused ? PLAYER_PAUSED : PLAYER_PLAYING;
        }
        g_next_valid = false;
        g_skips = 0;
        // The audio core has flushed and restarted: handoffs counted until now (the old track's ramp-down could still have crossed a
        // boundary) are not ours.
        g_changes_seen = audio_track_change_count();
        g_ended_base = audio_pcm_ended_count(); // ... and so is an end the old stream reported while it was being replaced
        apply_file(f);
        if (g_resume_boot && !g_resume_play && g_info.state == PLAYER_PLAYING) {
            // Boot resume: nothing has been fed yet, so pausing now keeps the track silent until the user presses play.
            g_info.state = PLAYER_PAUSED;
            (void)audio_pause(true);
        }
        if (g_seeking && g_info.state == PLAYER_PAUSED) {
            (void)audio_pause(true); // nothing has been fed yet: the restarted track stays silent
        }
        if (g_seeking) { // the seek is done: the new position is what a power cut should resume at (once per seek, not per scrub step)
            audio_stats_t none;
            memset(&none, 0, sizeof none); // the restarted stream has played nothing yet: the position is its start frame
            save = rs_snapshot(&g_rs_tmp, true, &none);
            if (save) {
                rs_queue(&g_rs_tmp, RESUME_EVENT);
            }
        }
        g_seeking = false;
        g_resume_boot = false; // the boot resume is open (paused or playing): later failures are ordinary ones
    }
    unlock();
    if (save) {
        rs_flush();
    }
}

// Show a failure. Lock held on entry, released here.
static void fail_current(const char *msg) {
    g_seeking = false;
    if (g_resume_boot && g_info.src == PLAYER_SRC_FILE) {
        // The boot resume could not start (file unreadable): drop it quietly instead of greeting the user with an error.
        reset_info(PLAYER_SRC_NONE, PLAYER_IDLE, "");
        pq_clear(&g_q);
        g_auto = false;
        rs_queue_clear();
        unlock();
        rs_flush();
        printf("player: resume discarded (%s)\n", msg);
        return;
    }
    if (g_info.src == PLAYER_SRC_FILE) {
        g_info.state = PLAYER_ERROR;
        snprintf(g_info.msg, sizeof g_info.msg, "%s", msg);
    }
    unlock();
    printf("player: %s\n", msg);
}

void player_file_failed(const char *msg) {
    lock();
    if (g_starts_out > 0 && --g_starts_out > 0) {
        unlock(); // a start that a later one has replaced failed: the later one decides
        printf("player: %s (superseded start)\n", msg);
        return;
    }
    fail_current(msg);
}

void player_stream_error(const char *msg) {
    lock();
    if (g_starts_out > 0) {
        unlock(); // a start is on its way and replaces the broken stream; it is not this error's to answer, and the count is not ours
        printf("player: %s (replaced by a new start)\n", msg);
        return;
    }
    fail_current(msg);
}

uint32_t player_starts_outstanding(void) {
    lock();
    uint32_t n = g_starts_out;
    unlock();
    return n;
}

uint32_t player_file_generation(void) { return g_ended_base; }
