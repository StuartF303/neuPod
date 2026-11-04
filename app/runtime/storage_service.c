// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfs.h"
#include "art_service.h"
#include "audio_engine.h"
#include "os.h"
#include "play_queue.h"
#include "player.h"
#include "resume.h"
#include "settings_service.h"
#include "settings_store.h"
#include "storage_service.h"
#include "meta.h"
#include "track_feed.h"
#include "track_stream.h"
#include "wav.h"

typedef enum { REQ_LIST, REQ_SPACE, REQ_PREFETCH, REQ_PLAY, REQ_STOP, REQ_VERIFY, REQ_SETTINGS_LOAD } req_kind_t;

typedef struct {
    req_kind_t kind;
    uint32_t seq; /* list/space: the caller's sequence number. REQ_PLAY: the start frame (resume), 0 = from the beginning */
    char path[NFS_PATH_MAX];
} storage_req_t;

static os_queue_t *g_req_q;
static os_queue_t *g_res_q;
static file_entry_t g_entries[FILES_MAX_ENTRIES];

void storage_service_init(void) {
    g_req_q = os_queue_create(sizeof(storage_req_t), 3);
    g_res_q = os_queue_create(sizeof(storage_result_t), 4);
}

bool storage_service_ready(void) { return g_req_q != NULL && g_res_q != NULL; }

static bool post(req_kind_t kind, const char *path, uint32_t seq) {
    storage_req_t r = {.kind = kind, .seq = seq};
    if (path != NULL) {
        strncpy(r.path, path, sizeof r.path - 1);
    }
    // Transport requests may wait briefly for a slot (a directory listing can be queued ahead of them).
    uint32_t wait = (kind == REQ_PLAY || kind == REQ_STOP || kind == REQ_VERIFY || kind == REQ_SETTINGS_LOAD) ? 50u : OS_NO_WAIT;
    return g_req_q != NULL && os_queue_send(g_req_q, &r, wait);
}
bool storage_play_request(const char *path) { return path != NULL && post(REQ_PLAY, path, 0); }
bool storage_play_request_at(const char *path, uint32_t frame) { return path != NULL && post(REQ_PLAY, path, frame); }
bool storage_stop_request(void) { return post(REQ_STOP, NULL, 0); }
bool storage_settings_load_request(void) { return post(REQ_SETTINGS_LOAD, NULL, 0); }
bool storage_verify_request(const char *path) { return path != NULL && post(REQ_VERIFY, path, 0); }

bool storage_list_request(const char *path, uint32_t seq) { return post(REQ_LIST, path, seq); }
bool storage_space_request(uint32_t seq) { return post(REQ_SPACE, NULL, seq); }
bool storage_prefetch_request(const char *path) { return path != NULL && post(REQ_PREFETCH, path, 0); }

bool storage_result_poll(storage_result_t *out) {
    return g_res_q != NULL && os_queue_recv(g_res_q, out, OS_NO_WAIT);
}

const file_entry_t *storage_list_entries(void) { return g_entries; }

// Names: dirs first, then case-insensitive alphabetical.
static int cmp_entries(const void *pa, const void *pb) {
    const file_entry_t *a = pa, *b = pb;
    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }
    for (size_t i = 0; i < FILES_NAME_LEN; i++) {
        int ca = a->name[i], cb = b->name[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca += 'a' - 'A';
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb += 'a' - 'A';
        }
        if (ca != cb) {
            return ca - cb;
        }
        if (ca == 0) {
            break;
        }
    }
    return 0;
}

// Copy a name, keeping at most FILES_NAME_LEN-1 bytes and never cutting a UTF-8 character in half.
static void copy_name(char *dst, const char *src) {
    size_t n = strlen(src);
    if (n > FILES_NAME_LEN - 1) {
        n = FILES_NAME_LEN - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            n--; // src[n] is a continuation byte: drop the partial character
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool g_ui_listed; /* the UI has asked for a listing: g_entries may be on screen, the boot resume must not reuse it */

static void list_dir(const storage_req_t *rq, storage_result_t *res) {
    res->kind = STORAGE_RES_LIST;
    res->seq = rq->seq;
    uint32_t count = 0;
    bool truncated = false;
    nfs_err_t err = NFS_OK;
    // Two passes so that "dirs first" survives the entry cap: directories, then files.
    for (int pass = 0; pass < 2 && err == NFS_OK && !truncated; pass++) {
        nfs_dir_t *d;
        err = nfs_opendir(&d, rq->path);
        if (err != NFS_OK) {
            break;
        }
        uint32_t scanned = 0;
        for (;;) {
            nfs_dirent_t e;
            bool done = false;
            err = nfs_readdir(d, &e, &done);
            if (err != NFS_OK || done) {
                break;
            }
            if ((++scanned & 31u) == 0) {
                os_delay_ms(1); // let the UI and input run during a big directory
            }
            if (nfs_dirent_hidden(&e) || e.is_dir != (pass == 0)) {
                continue; // hidden entries (dot names, FAT hidden / system); and the other pass's kind
            }
            if (count >= FILES_MAX_ENTRIES) {
                truncated = true;
                break;
            }
            copy_name(g_entries[count].name, e.name);
            g_entries[count].size = e.size;
            g_entries[count].is_dir = e.is_dir;
            count++;
        }
        (void)nfs_closedir(d);
    }
    if (err == NFS_OK && count > 1) {
        qsort(g_entries, count, sizeof g_entries[0], cmp_entries);
    }
    res->err = (int)err;
    res->count = err == NFS_OK ? count : 0;
    res->truncated = truncated;
}

// ---- Streaming: card -> audio core's data ring ----------------------------------------------------------
//
// The stream controller (app/audio/track_stream.c, docs/audio.md) does the work; this is its glue to the card and the player.

static int32_t file_read_cb(void *ctx, uint64_t off, void *dst, uint32_t n) {
    size_t got = 0;
    if (nfs_seek((nfs_file_t *)ctx, off) != NFS_OK || nfs_read((nfs_file_t *)ctx, dst, n, &got) != NFS_OK) {
        return -1;
    }
    return (int32_t)got;
}

static const char *ts_open(const char *path, meta_read_fn *rd, void **rctx, uint64_t *size, char *msg, size_t msg_n) {
    nfs_file_t *f;
    nfs_err_t err = nfs_open(&f, path, NFS_O_READ);
    if (err == NFS_OK && (err = nfs_size(f, size)) != NFS_OK) {
        (void)nfs_close(f);
    }
    if (err != NFS_OK) {
        snprintf(msg, msg_n, "Cannot open file: %s", nfs_err_str(err));
        return msg;
    }
    *rd = file_read_cb;
    *rctx = f;
    return NULL;
}

static void ts_close(void *rctx) { (void)nfs_close((nfs_file_t *)rctx); }

static void to_player_file(const track_info_t *i, player_file_t *pf) {
    memset(pf, 0, sizeof *pf);
    snprintf(pf->format, sizeof pf->format, "%s", i->format);
    pf->rate = i->rate;
    pf->total_frames = i->total_frames;
    pf->tags = i->tags;
    pf->title_is_name = i->title_is_name;
    pf->estimated = i->estimated;
    pf->verify = i->verify;
    pf->codec = i->codec;
    memcpy(pf->md5, i->md5, 16);
    pf->file_size = i->file_size;
    pf->start_frame = i->start_frame;
}

static void ts_started(const char *path, const track_info_t *i) {
    player_file_t pf;
    to_player_file(i, &pf);
    player_file_started(&pf);
    if (!i->verify) {
        art_service_track(path, &pf.tags); // queues the album art; it is decoded later, between audio top-ups (art_yield)
    }
}

// Gapless handoff: the new track's art (the per-album cache makes a same-album change free).
static void ts_track_changed(const char *path, const track_info_t *i) { art_service_track(path, &i->tags); }

static void ts_next_announced(const track_info_t *i) {
    player_file_t pf;
    to_player_file(i, &pf);
    player_next_announced(&pf);
}

static uint32_t ts_now(void) { return os_tick_ms(); }

static const track_stream_ops_t k_stream_ops = {.open = ts_open, .close = ts_close, .next_track = player_next_track,
                                                .next_announced = ts_next_announced, .started = ts_started, .track_changed = ts_track_changed,
                                                .failed = player_file_failed, .now_ms = ts_now, .delay_ms = os_delay_ms};

// Boot resume (resume.h): runs on this task right after the settings load. Lists the saved folder (into g_entries, so only while the UI has
// not listed anything yet), checks the file against what was saved and hands the folder to the player, paused (or playing: setting "Resume playing on power-up" on and the record was playing).
static void resume_boot(void) {
    resume_state_t r;
    char dir[NFS_PATH_MAX];
    const char *name;
    if (!resume_load(&r)) {
        return;
    }
    player_info_t pi;
    player_get(&pi);
    if (pi.state != PLAYER_IDLE) {
        return; // something already plays: leave the record alone
    }
    if (!resume_split(r.path, dir, sizeof dir, &name)) {
        resume_clear();
        return;
    }
    if (g_ui_listed) {
        return; // the user is already browsing: leave the record for the next boot
    }
    storage_req_t rq = {.kind = REQ_LIST};
    snprintf(rq.path, sizeof rq.path, "%s", dir);
    storage_result_t res = {0};
    list_dir(&rq, &res);
    if (res.err != 0) {
        printf("resume: folder '%s' not readable (%s), discarded\n", dir, nfs_err_str((nfs_err_t)res.err));
        resume_clear();
        return;
    }
    uint32_t idx = 0, frame = 0;
    resume_plan_t plan = resume_plan(&r, name, g_entries, res.count, &idx, &frame);
    if (plan == RESUME_PLAN_CHANGED) {
        printf("resume: '%s' changed size, starting it from the beginning\n", name);
    } else if (plan == RESUME_PLAN_GONE) {
        printf("resume: '%s' is gone, offering '%s' from its beginning\n", name, g_entries[idx].name);
    }
    bool play = player_resume_autoplay() && r.playing;
    player_resume_t pr = plan == RESUME_PLAN_NOTHING ? PLAYER_RESUME_INVALID : player_resume_folder(dir, g_entries, res.count, idx, frame, play);
    if (pr == PLAYER_RESUME_BUSY) {
        return; // the user started something first: keep the record, it is not the user's loss
    }
    if (pr != PLAYER_RESUME_OK) {
        printf("resume: nothing playable to resume, discarded\n");
        resume_clear();
        return;
    }
    printf("resume: '%s' %s at frame %u\n", g_entries[idx].name, play ? "playing" : "paused", (unsigned)frame);
}

#define SETTINGS_POLL_MS 500u
// A settings save is SD writes (a FAT update and two data sectors: typically a few ms, but an SD card may stall far longer). It must
// never starve the audio ring, so it is NOT done while audio plays: changes wait for pause, stop, the end of the track or an idle
// moment, and are then written at once (the 1.5 s debounce still applies). The cost: a volume change made during a long track is
// lost if the power fails before the next pause/stop. While paused the ring is full and nothing is consuming it, so the write cannot hurt.
// Optional exception, OFF by default: write a pending save (the resume position) while playing. The write runs on this task, which also
// refills the audio data ring, so the refill stalls for the whole write (about 8-14 sector writes; an SD card can take 100-250 ms for one).
// Only with RESUME_WRITE_WHILE_PLAYING 1, and then only when the data ring plus the PCM ring hold RESUME_MIN_BUFFERED_MS of audio, judged
// pessimistically (resume_buffered_ms: 32-bit stereo for the data ring, the PCM ring's low-water mark). Every such write is timed; one that
// takes longer than RESUME_SLOW_WRITE_MS switches the exception off for the rest of the boot.
#define RESUME_SLOW_WRITE_MS 50u
static bool g_wwp_off;       /* a write while playing was slow: not again this boot */
static bool g_wwp_attempt;   /* the last quiet verdict relied on the exception */

static bool settings_quiet_now(void) {
    player_info_t pi;
    player_get(&pi);
    g_wwp_attempt = false;
    if (RESUME_WRITE_WHILE_PLAYING && !g_wwp_off && pi.state == PLAYER_PLAYING && pi.rate != 0) {
        audio_stats_t st;
        if (audio_stats_get(&st) &&
            resume_write_while_playing_ok(resume_buffered_ms(pi.rate, AUDIO_DATA_RING_BYTES - audio_data_space(), 8u, st.ring_min_frames))) {
            g_wwp_attempt = true;
            return true;
        }
    }
    return pi.state != PLAYER_PLAYING && pi.state != PLAYER_OPENING && pi.state != PLAYER_VERIFYING;
}

static void settings_poll(void) {
    static uint32_t last_ms;
    uint32_t now = os_tick_ms();
    if (now - last_ms < SETTINGS_POLL_MS / 2u) {
        return; // while streaming this runs every millisecond: look at most every 250 ms
    }
    last_ms = now;
    if (!settings_dirty()) {
        settings_service_poll(true); // keeps the service's bookkeeping current; nothing to write
        return;
    }
    bool quiet = settings_quiet_now();
    settings_status_t before, after;
    settings_get_status(&before);
    uint32_t t0 = os_tick_ms();
    settings_service_poll(quiet);
    settings_get_status(&after);
    if (g_wwp_attempt && after.saves != before.saves && os_tick_ms() - t0 > RESUME_SLOW_WRITE_MS) {
        g_wwp_off = true;
        printf("settings: a save during playback took %u ms, no more saves during playback this boot\n", (unsigned)(os_tick_ms() - t0));
    }
}

// Called by the art decoder many times per picture. Keeps the audio ring topped up, gives the other tasks a millisecond after
// every ART_SLICE_MS of work (this task outranks input and the UI), and stops the decode when a request is waiting.
#define ART_SLICE_MS 2u
static bool art_yield(void *ctx) {
    static uint32_t slice_start;
    (void)ctx;
    if (os_queue_pending(g_req_q)) {
        return false;
    }
    track_stream_pump();
    uint32_t now = os_tick_ms();
    if (now - slice_start >= ART_SLICE_MS) {
        os_delay_ms(1);
        settings_poll();
        slice_start = os_tick_ms();
    }
    return true;
}

void storage_task(void *arg) {
    (void)arg;
    track_stream_init(&k_stream_ops);
    for (;;) {
        storage_req_t rq;
        // While a file streams, wake every millisecond to keep the audio data ring full.
        // Idle: wake twice a second so a pending settings save is not forgotten (nothing else wakes this task).
        if (!os_queue_recv(g_req_q, &rq, track_stream_active() ? 1u : SETTINGS_POLL_MS)) {
            track_stream_pump();
            settings_poll();
            if (art_service_pending()) {
                art_service_run(art_yield, NULL); // only when idle; it gives way to any request that arrives
            }
            continue;
        }
        storage_result_t res = {0};
        switch (rq.kind) {
        case REQ_PLAY:
            track_stream_start_at(rq.path, false, rq.seq);
            continue;
        case REQ_VERIFY:
            track_stream_start(rq.path, true);
            continue;
        case REQ_STOP:
            track_stream_stop();
            continue;
        case REQ_SETTINGS_LOAD:
            settings_service_load_now();
            resume_boot();
            continue;
        case REQ_LIST:
            g_ui_listed = true;
            list_dir(&rq, &res);
            break;
        case REQ_SPACE:
            res.kind = STORAGE_RES_SPACE;
            res.seq = rq.seq;
            res.err = (int)nfs_space(&res.total_bytes, &res.free_bytes);
            break;
        case REQ_PREFETCH:
            printf("storage: prefetch '%s' (stub: nothing to do yet)\n", rq.path);
            continue;
        }
        (void)os_queue_send(g_res_q, &res, 100);
        track_stream_pump();
    }
}
