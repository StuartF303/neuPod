// SPDX-License-Identifier: MPL-2.0
#include "art_service.h"

#include <stdio.h>
#include <string.h>

#include <strings.h>

#include "meta.h"
#include "nfs.h"
#include "os.h"

#define ACK_WAIT_MS 300u // how long to wait for the UI to stop showing the old pixels

// ---- the thumbnails and their state (one writer: the storage task; one reader: the UI) ---------------------------------

typedef struct {
    uint32_t folder, album, emb_len;
    uint8_t emb_kind;
} art_key_t;

// ART_CACHE_N thumbnails (24 x 50 KB on the hosted builds, one 8 KB one on the Pico 2 W): the last albums decoded, least recently used replaced
// first. The track playing shows one of them (the "view"); the album being browsed (the library's album header) another. The view's slot, the one
// before it (the UI may still be drawing it for a frame) and the browse slot are never replaced while there is another to take.
typedef struct {
    art_key_t key;
    bool used;
    uint16_t w, h;
    uint32_t stamp;
    uint16_t px[ART_PIXELS];
} art_slot_t;
static art_slot_t g_slot[ART_CACHE_N];
static uint32_t g_clock;
static int g_cur = -1, g_prev = -1, g_browse = -1;

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
        out->px = g_cur >= 0 ? g_slot[g_cur].px : NULL;
    } while (s != g_seq);
    out->gen = s / 2u;
}

void art_view_ack(uint32_t gen) { g_ack = gen; }

static bool key_eq(const art_key_t *a, const art_key_t *b) { return memcmp(a, b, sizeof *a) == 0; }

static int slot_find(const art_key_t *k) {
    for (unsigned i = 0; i < ART_CACHE_N; i++) {
        if (g_slot[i].used && key_eq(&g_slot[i].key, k)) {
            return (int)i;
        }
    }
    return -1;
}

// The slot to decode into: an unused one, else the least recently used that is not on screen (when the cache has more than one slot).
static int slot_victim(void) {
    int best = -1;
    for (unsigned i = 0; i < ART_CACHE_N; i++) {
        if (!g_slot[i].used) {
            return (int)i;
        }
        bool pinned = ART_CACHE_N > 3u && ((int)i == g_cur || (int)i == g_prev || (int)i == g_browse);
        if (!pinned && (best < 0 || (int32_t)(g_slot[i].stamp - g_slot[best].stamp) < 0)) {
            best = (int)i;
        }
    }
    return best >= 0 ? best : 0;
}

static void slot_touch(int i) { g_slot[i].stamp = ++g_clock; }

static void show_slot(int i) { // the track playing now has its thumbnail: publish it
    g_prev = g_cur;
    g_cur = i;
    slot_touch(i);
    set_state(ART_VIEW_READY, g_slot[i].w, g_slot[i].h);
}

// ---- the album being browsed (the album header of the library) -------------------------------------------------------------

#define BROWSE_HANDLE_MAX 24u
static struct {
    char handle[BROWSE_HANDLE_MAX]; // library handle of the album's first song ("@lib/1/12"); "" = nothing wanted
    volatile bool pending;
    volatile art_view_state_t state;
    volatile uint32_t gen;
    uint16_t w, h;
    art_key_t key;
    bool failed_known;
} g_b;
static art_resolve_fn g_resolve;

void art_service_set_resolver(art_resolve_fn fn) { g_resolve = fn; }

void art_browse_want(const char *handle) {
#if ART_CACHE_N > 1u
    if (handle == NULL || handle[0] == '\0') {
        g_b.handle[0] = '\0';
        g_b.pending = false;
        g_b.state = ART_VIEW_NONE;
        g_b.gen++;
        g_browse = -1;
        return;
    }
    if (strncmp(g_b.handle, handle, sizeof g_b.handle) == 0 && g_b.state != ART_VIEW_NONE) {
        return;
    }
    snprintf(g_b.handle, sizeof g_b.handle, "%s", handle);
    g_b.state = ART_VIEW_LOADING;
    g_b.pending = true;
    g_b.gen++;
#else
    (void)handle; // no cache, no browse art
#endif
}

void art_browse_get(art_view_t *out) {
    out->state = g_b.state;
    out->gen = g_b.gen;
    out->w = g_b.w;
    out->h = g_b.h;
    out->px = g_browse >= 0 ? g_slot[g_browse].px : NULL;
    if (out->state != ART_VIEW_READY || g_browse < 0) {
        out->px = NULL;
        if (out->state == ART_VIEW_READY) {
            out->state = ART_VIEW_LOADING;
        }
    }
}

// ---- what is wanted -------------------------------------------------------------------------------------------------

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
    int hit = slot_find(&k);
    if (hit >= 0) { // an album played (or browsed) before: nothing to decode
        g_job.pending = false;
        show_slot(hit);
        return;
    }
    g_job.pending = true;
    g_job.wait_ack = g_state == ART_VIEW_READY;
    snprintf(g_job.path, sizeof g_job.path, "%s", path);
    g_job.emb = tags->art;
    set_state(ART_VIEW_LOADING, 0, 0);
}

bool art_service_pending(void) { return g_job.pending || g_b.pending; }

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

static art_err_t decode_file(const char *path, uint64_t off, uint32_t len, bool whole_file, uint16_t *dst, art_info_t *info, art_yield_fn yield, void *yc) {
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
    art_err_t e = art_decode(read_cb, f, off, len, dst, info, yield, yc);
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
    const volatile uint32_t *live_gen; // the generation of the job being run: it moves when the job is retargeted
} guard_t;
static bool guarded_yield(void *vctx) {
    guard_t *g = vctx;
    return g->yield(g->ctx) && *g->live_gen == g->gen;
}

// Find the picture of the file at @p job_path (the embedded one in @p emb, else a cover file in its folder) and decode it into @p dst.
// Returns an art_err_t (ART_E_ABORTED: stop, the job stays queued). What was tried goes to @p used / @p nocover / @p emb_err / @p n_src for the log line.
static art_err_t find_and_decode(const char *job_path, const art_ref_t *emb, uint16_t *dst, art_info_t *info, art_yield_fn yield, void *yc,
                                 const char **used, bool *nocover, art_err_t *emb_err, size_t *n_src) {
    art_src_t order[2];
    size_t n = art_sources(emb, true, order); // a cover file in the folder is only looked for when it is reached
    *n_src = n;
    art_err_t err = ART_E_FORMAT;
    *used = "none";
    *nocover = false;
    *emb_err = ART_OK;
    for (size_t i = 0; i < n; i++) {
        if (order[i] == ART_SRC_EMBEDDED) {
            err = decode_file(job_path, emb->offset, emb->length, false, dst, info, yield, yc);
            *used = "embedded";
            *emb_err = err;
        } else {
            char cover[NFS_PATH_MAX];
            bool aborted = false;
            if (find_folder_art(job_path, cover, sizeof cover, &aborted, yield, yc) == 0) {
                err = aborted ? ART_E_ABORTED : ART_E_FORMAT;
                *used = aborted ? "folder scan" : "no cover file";
                *nocover = !aborted;
            } else {
                err = decode_file(cover, 0, 0, true, dst, info, yield, yc);
                *used = "folder";
            }
        }
        if (err == ART_E_ABORTED || err == ART_OK) {
            break;
        }
    }
    return err;
}

static void run_current(art_yield_fn caller_yield, void *caller_ctx) {
    guard_t guard = {.yield = caller_yield, .ctx = caller_ctx, .gen = g_job.gen, .live_gen = &g_job.gen};
    art_yield_fn yield = guarded_yield;
    void *yc = &guard;
    char job_path[NFS_PATH_MAX]; // the job as it was when the run started (the shared one may be retargeted under us)
    snprintf(job_path, sizeof job_path, "%s", g_job.path);
    const art_ref_t job_emb = g_job.emb;
    const art_key_t job_key = g_job.key;
    if (g_job.wait_ack) { // let the UI take the old picture off the screen before its pixels are overwritten
        uint32_t t0 = os_tick_ms();
        while (g_ack != g_seq / 2u && os_tick_ms() - t0 < ACK_WAIT_MS) {
            if (!yield(yc)) return;
            os_delay_ms(1);
        }
        g_job.wait_ack = false;
    }
    uint32_t t0 = os_tick_ms();
    int dst = slot_victim();
    art_info_t info = {0};
    const char *used;
    bool nocover;
    art_err_t emb_err;
    size_t n;
    art_err_t err = find_and_decode(job_path, &job_emb, g_slot[dst].px, &info, yield, yc, &used, &nocover, &emb_err, &n);
    if (err == ART_E_ABORTED) {
        g_slot[dst].used = false; // half a picture: not a hit for anyone
        return; // a request is waiting (or the stream needs the task): the job stays queued and restarts from scratch
    }
    if (g_job.gen != guard.gen) {
        g_slot[dst].used = false;
        return; // retargeted after the last yield: this result belongs to the previous track; the new job restarts from scratch
    }
    g_job.pending = false;
    if (err == ART_OK) {
        g_slot[dst].key = job_key;
        g_slot[dst].used = true;
        g_slot[dst].w = info.w;
        g_slot[dst].h = info.h;
        show_slot(dst);
        printf("art: %s %ux%u -> %ux%u (1/%u) %u ms\n", used, (unsigned)info.src_w, (unsigned)info.src_h, (unsigned)info.w, (unsigned)info.h,
               1u << info.scale, (unsigned)(os_tick_ms() - t0));
    } else {
        g_slot[dst].used = false; // the decode may have scribbled on the slot
        if (dst == g_cur) {
            g_cur = -1;
        }
        set_state(ART_VIEW_FAILED, 0, 0);
        if (err == ART_E_READ || err == ART_E_MEMORY) {
            g_job.have_key = false; // maybe a passing fault: the next track tries again
        }
        printf("art: none (%s%s%s; embedded: %s) %u ms\n", used, nocover ? "" : ": ", nocover ? "" : art_err_str(err),
               n > 1 || emb_err != ART_OK ? art_err_str(emb_err) : "no JPEG", (unsigned)(os_tick_ms() - t0));
    }
}

#if ART_CACHE_N > 1u
// The tags of the file that carry its picture's position: the embedded picture is only found by reading the file's header.
static bool read_art_ref(const char *path, art_ref_t *out, char *album, size_t album_cap) {
    nfs_file_t *f;
    if (nfs_open(&f, path, NFS_O_READ) != NFS_OK) return false;
    uint64_t size = 0;
    bool ok = false;
    memset(out, 0, sizeof *out);
    album[0] = '\0';
    if (nfs_size(f, &size) == NFS_OK) {
        size_t n = strlen(path);
        static union {
            flac_info_t flac;
            mp3_info_t mp3;
            mp4_info_t mp4;
        } info; // storage task only: off its stack
        const audio_tags_t *tags = NULL;
        if (n >= 5 && strcasecmp(path + n - 5, ".flac") == 0) {
            ok = flac_parse(read_cb, f, size, &info.flac) == META_OK;
            tags = &info.flac.tags;
        } else if (n >= 4 && strcasecmp(path + n - 4, ".mp3") == 0) {
            ok = mp3_parse(read_cb, f, size, &info.mp3) == META_OK;
            tags = &info.mp3.tags;
        } else if (n >= 4 && (strcasecmp(path + n - 4, ".m4a") == 0 || strcasecmp(path + n - 4, ".mp4") == 0)) {
            ok = mp4_parse(read_cb, f, size, &info.mp4) == META_OK;
            tags = &info.mp4.tags;
        } else {
            ok = true; // a WAV or anything else: no embedded picture, a cover file in the folder still counts
        }
        if (ok && tags != NULL) {
            *out = tags->art;
            snprintf(album, album_cap, "%s", tags->album);
        }
    }
    (void)nfs_close(f);
    return ok;
}

static void run_browse(art_yield_fn caller_yield, void *caller_ctx) {
    char handle[BROWSE_HANDLE_MAX];
    snprintf(handle, sizeof handle, "%s", g_b.handle);
    uint32_t want_gen = g_b.gen;
    guard_t guard = {.yield = caller_yield, .ctx = caller_ctx, .gen = want_gen, .live_gen = &g_b.gen};
    char path[NFS_PATH_MAX];
    const char *why = "";
    if (handle[0] == '\0' || g_resolve == NULL || !g_resolve(handle, path, sizeof path, &why)) {
        if (g_b.gen == want_gen) {
            g_b.pending = false;
            g_b.state = ART_VIEW_FAILED;
        }
        return;
    }
    art_ref_t emb;
    char album[64];
    if (!read_art_ref(path, &emb, album, sizeof album)) {
        memset(&emb, 0, sizeof emb);
        album[0] = '\0';
    }
    // The same key the track playing makes (folder, album name, embedded picture): an album played earlier is a hit, and playing it later is too.
    art_key_t k = {.folder = fnv(path, dir_len(path)), .album = fnv(album, strlen(album)), .emb_len = emb.length, .emb_kind = emb.kind};
    int hit = slot_find(&k);
    if (hit >= 0) {
        g_browse = hit;
        slot_touch(hit);
        g_b.w = g_slot[hit].w;
        g_b.h = g_slot[hit].h;
        if (g_b.gen == want_gen) {
            g_b.pending = false;
            g_b.state = ART_VIEW_READY;
        }
        return;
    }
    int dst = slot_victim();
    art_info_t info = {0};
    const char *used;
    bool nocover;
    art_err_t emb_err;
    size_t n;
    uint32_t t0 = os_tick_ms();
    art_err_t err = find_and_decode(path, &emb, g_slot[dst].px, &info, guarded_yield, &guard, &used, &nocover, &emb_err, &n);
    if (err == ART_E_ABORTED || g_b.gen != want_gen) {
        g_slot[dst].used = false; // half a picture: not a hit for anyone
        return;                   // wanted something else meanwhile (or the stream needs the task): a pending request restarts from scratch
    }
    g_b.pending = false;
    if (err == ART_OK) {
        g_slot[dst].key = k;
        g_slot[dst].used = true;
        g_slot[dst].w = info.w;
        g_slot[dst].h = info.h;
        slot_touch(dst);
        g_browse = dst;
        g_b.w = info.w;
        g_b.h = info.h;
        g_b.state = ART_VIEW_READY;
        printf("art: album header %s %ux%u -> %ux%u %u ms\n", used, (unsigned)info.src_w, (unsigned)info.src_h, (unsigned)info.w, (unsigned)info.h,
               (unsigned)(os_tick_ms() - t0));
    } else {
        g_slot[dst].used = false;
        g_browse = -1;
        g_b.state = ART_VIEW_FAILED;
        printf("art: album header none (%s) %u ms\n", art_err_str(err), (unsigned)(os_tick_ms() - t0));
    }
}
#endif

void art_service_run(art_yield_fn caller_yield, void *caller_ctx) {
    if (g_job.pending) { // the track playing comes first
        run_current(caller_yield, caller_ctx);
        return;
    }
#if ART_CACHE_N > 1u
    if (g_b.pending) {
        run_browse(caller_yield, caller_ctx);
    }
#endif
}
