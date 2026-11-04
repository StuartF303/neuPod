// SPDX-License-Identifier: MPL-2.0
#include "art_service.h"

#include <stdio.h>
#include <string.h>

#include "nfs.h"
#include "os.h"

#define ACK_WAIT_MS 300u // how long to wait for the UI to stop showing the old pixels

// ---- the thumbnail and its state (one writer: the storage task; one reader: the UI) -------------------------------------

static uint16_t g_px[ART_PIXELS];
static volatile uint32_t g_seq; // odd while the state is being changed: the reader retries
static volatile art_view_state_t g_state;
static volatile uint16_t g_w, g_h;
static volatile uint32_t g_ack;

static void set_state(art_view_state_t st, uint16_t w, uint16_t h) {
    g_seq++;
    g_state = st;
    g_w = w;
    g_h = h;
    g_seq++;
}

void art_view_get(art_view_t *out) {
    uint32_t s;
    do {
        while (((s = g_seq) & 1u) != 0) {
            os_yield();
        }
        out->state = g_state;
        out->w = g_w;
        out->h = g_h;
    } while (s != g_seq);
    out->gen = s / 2u;
    out->px = g_px;
}

void art_view_ack(uint32_t gen) { g_ack = gen; }

// ---- what is wanted -------------------------------------------------------------------------------------------------

typedef struct {
    uint32_t folder, album, emb_len;
    uint8_t emb_kind;
} art_key_t;

static struct {
    bool pending;
    bool have_key;
    art_key_t key;
    uint32_t gen;  // bumped whenever the job is retargeted: a run in progress notices and abandons its (now stale) work
    bool wait_ack; // the previous picture was on screen when this job was queued
    char path[NFS_PATH_MAX];
    art_ref_t emb;
} g_job;

static uint32_t fnv(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ (uint8_t)s[i]) * 16777619u;
    }
    return h;
}

static size_t dir_len(const char *path) { // length of the folder part, without the final '/' (0 for the root)
    const char *s = strrchr(path, '/');
    return s != NULL ? (size_t)(s - path) : 0;
}

void art_service_track(const char *path, const audio_tags_t *tags) {
    art_key_t k = {.folder = fnv(path, dir_len(path)),
                .album = fnv(tags->album, strlen(tags->album)),
                .emb_len = tags->art.length,
                .emb_kind = tags->art.kind};
    if (g_job.have_key && memcmp(&g_job.key, &k, sizeof k) == 0) {
        return; // same album and picture: keep the thumbnail we have (or the job already queued for it)
    }
    g_job.gen++;
    g_job.key = k;
    g_job.have_key = true;
    g_job.pending = true;
    g_job.wait_ack = g_state == ART_VIEW_READY;
    snprintf(g_job.path, sizeof g_job.path, "%s", path);
    g_job.emb = tags->art;
    set_state(ART_VIEW_LOADING, 0, 0);
}

bool art_service_pending(void) { return g_job.pending; }

// ---- finding and decoding -------------------------------------------------------------------------------------------

static int32_t read_cb(void *ctx, uint64_t off, void *dst, uint32_t n) {
    size_t got = 0;
    if (nfs_seek((nfs_file_t *)ctx, off) != NFS_OK || nfs_read((nfs_file_t *)ctx, dst, n, &got) != NFS_OK) {
        return -1;
    }
    return (int32_t)got;
}

typedef struct {
    nfs_dir_t *dir;
    art_yield_fn yield;
    void *yield_ctx;
    uint32_t seen;
    bool aborted;
} scan_t;

static bool scan_next(void *vctx, char *name, size_t cap) {
    scan_t *s = vctx;
    for (;;) {
        nfs_dirent_t e;
        bool done = false;
        if (s->aborted || nfs_readdir(s->dir, &e, &done) != NFS_OK || done) {
            return false;
        }
        if ((++s->seen & 31u) == 0 && !s->yield(s->yield_ctx)) { // a big folder: stay responsive
            s->aborted = true;
            return false;
        }
        if (e.is_dir || nfs_dirent_hidden(&e)) {
            continue;
        }
        snprintf(name, cap, "%s", e.name);
        return true;
    }
}

static art_err_t decode_file(const char *path, uint64_t off, uint32_t len, bool whole_file, art_info_t *info, art_yield_fn yield, void *yc) {
    nfs_file_t *f;
    if (nfs_open(&f, path, NFS_O_READ) != NFS_OK) return ART_E_READ;
    if (whole_file) {
        uint64_t size = 0;
        if (nfs_size(f, &size) != NFS_OK) {
            (void)nfs_close(f);
            return ART_E_READ;
        }
        len = size > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)size;
    }
    art_err_t e = art_decode(read_cb, f, off, len, g_px, info, yield, yc);
    (void)nfs_close(f);
    return e;
}

// cover.jpg / folder.jpg / front.jpg in the folder of @p track. Returns the rank (0 = none) and the full path of the best one.
static int find_folder_art(const char *track, char *out, size_t cap, bool *aborted, art_yield_fn yield, void *yc) {
    char dir[NFS_PATH_MAX];
    size_t n = dir_len(track);
    snprintf(dir, sizeof dir, "%.*s", (int)(n == 0 ? 1 : n), n == 0 ? "/" : track);
    scan_t s = {.yield = yield, .yield_ctx = yc};
    if (nfs_opendir(&s.dir, dir) != NFS_OK) return 0;
    char best[NFS_NAME_MAX + 1];
    int rank = art_folder_pick(scan_next, &s, best, sizeof best);
    (void)nfs_closedir(s.dir);
    *aborted = s.aborted;
    if (rank == 0) return 0;
    size_t dl = n == 0 ? 0 : strlen(dir), bl = strlen(best);
    if (dl + 1 + bl + 1 > cap) {
        return 0; // path too long for NFS_PATH_MAX: treat as no cover file
    }
    memcpy(out, dir, dl);
    out[dl] = '/';
    memcpy(out + dl + 1, best, bl + 1);
    return rank;
}

// The caller's yield hook may feed the audio stream, which can start a new track and retarget the job (art_service_track) in the
// middle of a decode. Every yield is therefore followed by a check of the job generation: a run for a stale job stops at once and never
// publishes what it decoded; the retargeted job is picked up from scratch.
typedef struct {
    art_yield_fn yield;
    void *ctx;
    uint32_t gen;
} guard_t;
static bool guarded_yield(void *vctx) {
    guard_t *g = vctx;
    return g->yield(g->ctx) && g_job.gen == g->gen;
}

void art_service_run(art_yield_fn caller_yield, void *caller_ctx) {
    if (!g_job.pending) return;
    guard_t guard = {.yield = caller_yield, .ctx = caller_ctx, .gen = g_job.gen};
    art_yield_fn yield = guarded_yield;
    void *yc = &guard;
    char job_path[NFS_PATH_MAX]; // the job as it was when the run started (the shared one may be retargeted under us)
    snprintf(job_path, sizeof job_path, "%s", g_job.path);
    const art_ref_t job_emb = g_job.emb;
    if (g_job.wait_ack) { // let the UI take the old picture off the screen before its pixels are overwritten
        uint32_t t0 = os_tick_ms();
        while (g_ack != g_seq / 2u && os_tick_ms() - t0 < ACK_WAIT_MS) {
            if (!yield(yc)) return;
            os_delay_ms(1);
        }
        g_job.wait_ack = false;
    }
    uint32_t t0 = os_tick_ms();
    art_src_t order[2];
    size_t n = art_sources(&job_emb, true, order); // a cover file in the folder is only looked for when it is reached
    art_err_t err = ART_E_FORMAT;
    art_info_t info = {0};
    const char *used = "none";
    bool found = false, nocover = false;
    art_err_t emb_err = ART_OK;
    for (size_t i = 0; i < n && !found; i++) {
        if (order[i] == ART_SRC_EMBEDDED) {
            err = decode_file(job_path, job_emb.offset, job_emb.length, false, &info, yield, yc);
            used = "embedded";
            emb_err = err;
        } else {
            char cover[NFS_PATH_MAX];
            bool aborted = false;
            if (find_folder_art(job_path, cover, sizeof cover, &aborted, yield, yc) == 0) {
                err = aborted ? ART_E_ABORTED : ART_E_FORMAT;
                used = aborted ? "folder scan" : "no cover file";
                nocover = !aborted;
            } else {
                err = decode_file(cover, 0, 0, true, &info, yield, yc);
                used = "folder";
            }
        }
        if (err == ART_E_ABORTED) {
            return; // a request is waiting (or the stream needs the task): the job stays queued and restarts from scratch
        }
        found = err == ART_OK;
    }
    if (g_job.gen != guard.gen) {
        return; // retargeted after the last yield: this result belongs to the previous track; the new job restarts from scratch
    }
    g_job.pending = false;
    if (found) {
        set_state(ART_VIEW_READY, info.w, info.h);
        printf("art: %s %ux%u -> %ux%u (1/%u) %u ms\n", used, (unsigned)info.src_w, (unsigned)info.src_h, (unsigned)info.w, (unsigned)info.h,
               1u << info.scale, (unsigned)(os_tick_ms() - t0));
    } else {
        set_state(ART_VIEW_FAILED, 0, 0);
        if (err == ART_E_READ || err == ART_E_MEMORY) {
            g_job.have_key = false; // maybe a passing fault: the next track tries again
        }
        printf("art: none (%s%s%s; embedded: %s) %u ms\n", used, nocover ? "" : ": ", nocover ? "" : art_err_str(err),
               n > 1 || emb_err != ART_OK ? art_err_str(emb_err) : "no JPEG", (unsigned)(os_tick_ms() - t0));
    }
}
