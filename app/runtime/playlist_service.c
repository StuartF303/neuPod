// SPDX-License-Identifier: MPL-2.0
// Music > Playlists on the storage task (playlist_service.h).
#include "playlist_service.h"

#include <stdio.h>
#include <string.h>

#include "capabilities.h"
#include "hal_storage.h"
#include "library_service.h"
#include "nfs.h"
#include "pl_m3u.h"
#include "play_queue.h"
#include "plays_store.h"
#include "ratings_store.h"
#include "settings_store.h"
#include "storage_service.h"
#include "track_stream.h"

const char *pls_status_text(pls_status_t s) {
    switch (s) {
    case PLS_OK: return "";
    case PLS_NO_CARD: return "No card";
    case PLS_NO_LIBRARY: return "No library yet: update it first";
    case PLS_ERROR: return "Card error";
    case PLS_READONLY: return "Card is write-protected";
    case PLS_FULL: return "On-The-Go is full";
    case PLS_DUPLICATE: return "Already in On-The-Go";
    case PLS_BAD: return "Can't add this";
    case PLS_BUSY: return "Card busy, try again";
    }
    return "?";
}

#if PLS_AVAILABLE

enum { OP_CATALOG = 1, OP_OPEN, OP_OTG_ADD, OP_OTG_REMOVE, OP_OTG_CLEAR };

typedef struct {
    uint8_t op, kind, file;
    uint32_t seq, index;
} req_t;
_Static_assert(sizeof(req_t) < 128, "a playlist request travels in the storage request's path buffer");

typedef struct {
    volatile uint32_t seq; /* written last, with release semantics */
    volatile int status;
} done_t;
static done_t g_done_view, g_done_otg;

/* the On-The-Go entry being added: written by the UI before it posts the request, read by the storage task, then released */
static struct {
    char path[PL_PATH_MAX], title[PL_TITLE_MAX], artist[PL_ARTIST_MAX];
    uint32_t secs;
} g_add;
static volatile bool g_add_busy;

/* the catalog: built in a staging copy and swapped in under a version count, so a screen that keeps showing it while it is refreshed never reads a half-built one */
static pls_item_t g_cat[PLS_SMART_N + PLS_FILES_MAX], g_stage[PLS_SMART_N + PLS_FILES_MAX];
static uint32_t g_cat_n;
static volatile uint32_t g_cat_ver; /* odd while the swap runs */
#define FILE_PATH_MAX 128u
static char g_fpath[PLS_FILES_MAX][FILE_PATH_MAX];
static uint8_t g_nfiles;

/* the open list: entries packed as path\0title\0sub\0 in one pool */
static char g_pool[PLS_POOL_BYTES];
static uint16_t g_off[PLS_ENTRIES_MAX];
static uint8_t g_state[PLS_ENTRIES_MAX];
static uint32_t g_n, g_used;
static bool g_trunc;
static volatile uint32_t g_ver; /* odd while the pool is being rewritten (an open request is being handled): the entries are not readable */

static pl_m3u_t g_parser; /* storage task only; big, so not on its stack */

static bool card_ready(void) {
#ifdef NEUPOD_LIB_POSIX
    return caps_available(CAP_STORAGE);
#else
    return hal_storage_is_mounted();
#endif
}

static void publish(done_t *d, uint32_t seq, pls_status_t s) {
    d->status = (int)s;
    __atomic_store_n(&d->seq, seq, __ATOMIC_RELEASE);
}

/* ---- UI side ---------------------------------------------------------------------------------------------------------------------------- */

static bool post(const req_t *r) { return storage_post_playlist(r, sizeof *r); }

bool pls_request_catalog(uint32_t seq) { return post(&(req_t){.op = OP_CATALOG, .seq = seq}); }

bool pls_request_open(uint8_t kind, uint8_t file, uint32_t seq) {
    __atomic_store_n(&g_ver, g_ver | 1u, __ATOMIC_RELEASE);
    bool ok = post(&(req_t){.op = OP_OPEN, .kind = kind, .file = file, .seq = seq});
    if (!ok) {
        __atomic_store_n(&g_ver, g_ver + 1u, __ATOMIC_RELEASE);
    }
    return ok;
}

bool pls_poll(uint32_t seq, pls_status_t *st) {
    if (__atomic_load_n(&g_done_view.seq, __ATOMIC_ACQUIRE) != seq) {
        return false;
    }
    *st = (pls_status_t)g_done_view.status;
    return true;
}

bool pls_otg_poll(uint32_t seq, pls_status_t *st) {
    if (__atomic_load_n(&g_done_otg.seq, __ATOMIC_ACQUIRE) != seq) {
        return false;
    }
    *st = (pls_status_t)g_done_otg.status;
    return true;
}

uint32_t pls_catalog_count(void) { return (__atomic_load_n(&g_cat_ver, __ATOMIC_ACQUIRE) & 1u) == 0 ? g_cat_n : 0; }

bool pls_catalog_item(uint32_t i, pls_item_t *out) {
    uint32_t v = __atomic_load_n(&g_cat_ver, __ATOMIC_ACQUIRE);
    if ((v & 1u) != 0 || i >= g_cat_n) {
        return false;
    }
    *out = g_cat[i];
    return __atomic_load_n(&g_cat_ver, __ATOMIC_ACQUIRE) == v; /* swapped while we copied: a miss, the screen looks again next pass */
}

uint32_t pls_entry_count(void) { return (__atomic_load_n(&g_ver, __ATOMIC_ACQUIRE) & 1u) == 0 ? g_n : 0; }
bool pls_entry_truncated(void) { return g_trunc; }

bool pls_entry(uint32_t i, pls_entry_view_t *out) {
    if ((__atomic_load_n(&g_ver, __ATOMIC_ACQUIRE) & 1u) != 0 || i >= g_n) {
        return false;
    }
    const char *p = g_pool + g_off[i];
    out->path = p;
    p += strlen(p) + 1u;
    out->title = p;
    p += strlen(p) + 1u;
    out->sub = p;
    out->state = g_state[i];
    return true;
}

bool pls_otg_add(const char *path, const char *title, const char *artist, uint32_t secs, uint32_t seq) {
    if (path == NULL || path[0] != '/' || strlen(path) >= sizeof g_add.path || g_add_busy) {
        return false;
    }
    g_add_busy = true;
    snprintf(g_add.path, sizeof g_add.path, "%s", path);
    snprintf(g_add.title, sizeof g_add.title, "%s", title != NULL ? title : "");
    snprintf(g_add.artist, sizeof g_add.artist, "%s", artist != NULL ? artist : "");
    g_add.secs = secs;
    if (!post(&(req_t){.op = OP_OTG_ADD, .seq = seq})) {
        g_add_busy = false;
        return false;
    }
    return true;
}

bool pls_otg_remove(uint32_t index, uint32_t seq) { return post(&(req_t){.op = OP_OTG_REMOVE, .index = index, .seq = seq}); }
bool pls_otg_clear(uint32_t seq) { return post(&(req_t){.op = OP_OTG_CLEAR, .seq = seq}); }

/* ---- the open list's pool ------------------------------------------------------------------------------------------------------------------ */

static void copy_cut(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1u;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            n--;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void pool_reset(void) {
    g_n = 0;
    g_used = 0;
    g_trunc = false;
}

static bool add_entry(const char *path, const char *title, const char *sub, uint8_t state) {
    char t[PL_TITLE_MAX], s[48];
    copy_cut(t, sizeof t, title);
    copy_cut(s, sizeof s, sub);
    size_t need = strlen(path) + 1u + strlen(t) + 1u + strlen(s) + 1u;
    if (g_n >= PLS_ENTRIES_MAX || g_used + need > sizeof g_pool) {
        g_trunc = true;
        return false;
    }
    char *p = g_pool + g_used;
    g_off[g_n] = (uint16_t)g_used;
    g_state[g_n] = state;
    memcpy(p, path, strlen(path) + 1u);
    p += strlen(path) + 1u;
    memcpy(p, t, strlen(t) + 1u);
    p += strlen(t) + 1u;
    memcpy(p, s, strlen(s) + 1u);
    g_used += (uint32_t)need;
    g_n++;
    return true;
}

// "/Music/A/01 Song.flac" -> "01 Song"; the folder name "A"
static void stem_of(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    const char *name = slash != NULL ? slash + 1 : path;
    size_t n = strlen(name);
    const char *dot = strrchr(name, '.');
    if (dot != NULL && dot != name && strlen(dot) <= 5) {
        n = (size_t)(dot - name);
    }
    char tmp[PL_PATH_MAX];
    if (n >= sizeof tmp) {
        n = sizeof tmp - 1u;
    }
    memcpy(tmp, name, n);
    tmp[n] = '\0';
    copy_cut(out, cap, tmp);
}

static void folder_of(const char *path, char *out, size_t cap) {
    out[0] = '\0';
    const char *slash = strrchr(path, '/');
    if (slash == NULL || slash == path) {
        return;
    }
    const char *end = slash;
    const char *start = end;
    while (start > path && start[-1] != '/') {
        start--;
    }
    char tmp[PL_PATH_MAX];
    size_t n = (size_t)(end - start);
    if (n >= sizeof tmp) {
        n = sizeof tmp - 1u;
    }
    memcpy(tmp, start, n);
    tmp[n] = '\0';
    copy_cut(out, cap, tmp);
}

static uint8_t classify(const char *path) {
    if (path[0] == '\0') {
        return PLS_ENT_UNRESOLVABLE;
    }
    if (!pq_is_audio(path)) {
        return PLS_ENT_CANT_PLAY;
    }
    nfs_stat_t st;
    return nfs_stat(path, &st) == NFS_OK && !st.is_dir ? PLS_ENT_OK : PLS_ENT_MISSING;
}

static void add_classified(const char *path, const char *title_in, const char *sub_in) {
    uint8_t state = classify(path);
    char title[PL_TITLE_MAX], sub[48];
    if (title_in != NULL && title_in[0] != '\0') {
        copy_cut(title, sizeof title, title_in);
    } else {
        stem_of(path, title, sizeof title);
    }
    switch (state) {
    case PLS_ENT_UNRESOLVABLE: copy_cut(sub, sizeof sub, "Not a path on the card"); break;
    case PLS_ENT_CANT_PLAY: copy_cut(sub, sizeof sub, "Not a music file"); break;
    case PLS_ENT_MISSING: copy_cut(sub, sizeof sub, "Not on the card"); break;
    default:
        if (sub_in != NULL && sub_in[0] != '\0') {
            copy_cut(sub, sizeof sub, sub_in);
        } else {
            folder_of(path, sub, sizeof sub);
        }
    }
    (void)add_entry(path, title, sub, state);
}

/* ---- reading a playlist file ----------------------------------------------------------------------------------------------------------------- */

#define PARSE_MAX_BYTES (256u * 1024u)

// Stream @p path through the parser; fn gets every entry. NFS_ERR_NOT_FOUND when there is no such file. *cut: the file was longer than PARSE_MAX_BYTES.
static nfs_err_t parse_file(const char *path, pl_entry_fn fn, void *user, bool *cut) {
    char dir[PL_PATH_MAX];
    const char *slash = strrchr(path, '/');
    size_t dl = slash != NULL && slash != path ? (size_t)(slash - path) : 0;
    if (dl == 0) {
        snprintf(dir, sizeof dir, "/");
    } else {
        memcpy(dir, path, dl);
        dir[dl] = '\0';
    }
    nfs_file_t *f;
    nfs_err_t e = nfs_open(&f, path, NFS_O_READ);
    if (e != NFS_OK) {
        return e;
    }
    pl_m3u_init(&g_parser, dir);
    uint8_t buf[512];
    uint32_t total = 0;
    if (cut != NULL) {
        *cut = false;
    }
    for (;;) {
        size_t got = 0;
        e = nfs_read(f, buf, sizeof buf, &got);
        if (e != NFS_OK || got == 0) {
            break;
        }
        pl_m3u_feed(&g_parser, buf, got, fn, user);
        total += (uint32_t)got;
        if ((total & 4095u) == 0) {
            track_stream_pump(); /* keep the audio ring topped up between pieces */
        }
        if (total >= PARSE_MAX_BYTES) {
            if (cut != NULL) {
                *cut = true;
            }
            (void)nfs_close(f);
            return NFS_OK;
        }
    }
    (void)nfs_close(f);
    if (e == NFS_OK) {
        pl_m3u_end(&g_parser, fn, user);
    }
    return e;
}

static void count_cb(void *user, const pl_entry_t *e) {
    (void)e;
    (*(uint32_t *)user)++;
}

static void open_cb(void *user, const pl_entry_t *e) {
    uint32_t *seen = user;
    if ((++*seen & 7u) == 0) {
        track_stream_pump();
    }
    if (e->resolvable) {
        add_classified(e->path, e->title, e->artist);
    } else {
        add_classified("", e->title, "");
    }
    /* an entry with an artist: the sub line is the artist (add_classified used it unless the file is missing) */
}

// The On-The-Go file as it is for reading: after a cut between the unlink and the rename of a save, the complete temp file is the newer list.
static const char *otg_read_path(void) {
    nfs_stat_t st;
    if (nfs_stat(PLS_OTG_FILE, &st) == NFS_OK) {
        return PLS_OTG_FILE;
    }
    nfs_file_t *f;
    if (nfs_open(&f, PLS_OTG_TMP, NFS_O_READ) != NFS_OK) {
        return PLS_OTG_FILE;
    }
    uint64_t size = 0;
    char tail[8] = {0};
    size_t got = 0;
    bool whole = nfs_size(f, &size) == NFS_OK && size >= sizeof tail - 1u && nfs_seek(f, size - 6u) == NFS_OK && nfs_read(f, tail, 6, &got) == NFS_OK &&
                 got == 6 && memcmp(tail, "# end\n", 6) == 0;
    (void)nfs_close(f);
    return whole ? PLS_OTG_TMP : PLS_OTG_FILE; /* a partial temp file (cut mid-write, no old file) is not a list */
}

/* ---- the list of lists -------------------------------------------------------------------------------------------------------------------------- */

static int cmp_nocase(const char *a, const char *b) {
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') {
            ca += 32;
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb += 32;
        }
        if (ca != cb) {
            return ca - cb;
        }
        if (ca == 0) {
            return 0;
        }
    }
}

static char g_fname[PLS_FILES_MAX][PLS_NAME_MAX];

static void add_file(const char *dir, const char *name) {
    char path[FILE_PATH_MAX];
    int w = strcmp(dir, "/") == 0 ? snprintf(path, sizeof path, "/%s", name) : snprintf(path, sizeof path, "%s/%s", dir, name);
    if (w <= 0 || (size_t)w >= sizeof path) {
        return; /* too long to keep */
    }
    char shown[PLS_NAME_MAX];
    copy_cut(shown, sizeof shown, name);
    for (uint8_t i = 0; i < g_nfiles; i++) {
        if (cmp_nocase(g_fpath[i], path) == 0) {
            return; /* the same file found twice (FAT is not case sensitive) */
        }
    }
    // insert in order of the shown name (then the path)
    uint8_t at = 0;
    while (at < g_nfiles && (cmp_nocase(g_fname[at], shown) < 0 || (cmp_nocase(g_fname[at], shown) == 0 && cmp_nocase(g_fpath[at], path) < 0))) {
        at++;
    }
    if (at >= PLS_FILES_MAX) {
        return;
    }
    uint8_t last = g_nfiles < PLS_FILES_MAX ? g_nfiles : (uint8_t)(PLS_FILES_MAX - 1u);
    for (uint8_t i = last; i > at; i--) {
        memcpy(g_fname[i], g_fname[i - 1], sizeof g_fname[i]);
        memcpy(g_fpath[i], g_fpath[i - 1], sizeof g_fpath[i]);
    }
    memcpy(g_fname[at], shown, sizeof shown);
    memcpy(g_fpath[at], path, strlen(path) + 1u);
    if (g_nfiles < PLS_FILES_MAX) {
        g_nfiles++;
    }
}

static void scan_dir(const char *dir) {
    nfs_dir_t *d;
    if (nfs_opendir(&d, dir) != NFS_OK) {
        return;
    }
    static nfs_dirent_t e; /* storage task only: 270 bytes off its stack */
    bool done = false;
    uint32_t seen = 0;
    while (nfs_readdir(d, &e, &done) == NFS_OK && !done) {
        if ((++seen & 15u) == 0) {
            track_stream_pump();
        }
        if (!e.is_dir && !nfs_dirent_hidden(&e) && pl_is_playlist_name(e.name)) {
            add_file(dir, e.name);
        }
    }
    (void)nfs_closedir(d);
}

static void set_item(uint32_t i, uint8_t kind, uint8_t file, const char *name, bool known, uint32_t count, bool capped) {
    pls_item_t *it = &g_stage[i];
    memset(it, 0, sizeof *it);
    it->kind = kind;
    it->file = file;
    it->count_known = known;
    it->count = count;
    it->count_capped = capped;
    copy_cut(it->name, sizeof it->name, name);
}

static uint32_t count_rated_top(void) {
    uint32_t n = 0;
    plays_entry_t e;
    for (uint32_t i = 0; i < plays_slots(); i++) {
        if (plays_at(i, &e) && ratings_get(e.key) >= 4) {
            n++;
        }
    }
    return n;
}

static uint32_t count_played(void) {
    uint32_t n = 0;
    plays_entry_t e;
    for (uint32_t i = 0; i < plays_slots(); i++) {
        if (plays_at(i, &e) && e.plays > 0) {
            n++;
        }
    }
    return n;
}

static uint32_t g_stage_n;

static pls_status_t do_catalog(void) {
    g_stage_n = 0;
    g_nfiles = 0;
    if (!card_ready()) {
        return PLS_NO_CARD;
    }
    uint32_t otg = 0;
    bool cut = false;
    nfs_err_t e = parse_file(otg_read_path(), count_cb, &otg, &cut);
    bool otg_known = e == NFS_OK || e == NFS_ERR_NOT_FOUND;
    libsvc_status_t why = LIBSVC_OK;
    lib_reader_t *r = library_service_reader(&why);
    uint32_t tracks = r != NULL ? lib_manifest(r)->tracks : 0;
    uint32_t played = count_played();
    uint32_t unplayed = tracks > played ? tracks - played : 0;
    uint32_t most = played < PLS_MOST_N ? played : PLS_MOST_N;
    uint32_t i = 0;
    set_item(i++, PLS_OTG, 0, "On-The-Go", otg_known, e == NFS_OK ? otg : 0, false);
    set_item(i++, PLS_RECENT, 0, "Recently added", r != NULL, tracks < PLS_RECENT_N ? tracks : PLS_RECENT_N, false);
    set_item(i++, PLS_MOST, 0, "Most played", true, most, played > PLS_MOST_N);
    set_item(i++, PLS_TOP, 0, "Top rated", true, count_rated_top(), false);
    set_item(i++, PLS_UNPLAYED, 0, "Unplayed", r != NULL, unplayed < PLS_ENTRIES_MAX ? unplayed : PLS_ENTRIES_MAX, unplayed > PLS_ENTRIES_MAX);
    static const char *const k_dirs[] = {"/", "/Playlists", "/playlists", "/Music"};
    for (unsigned d = 0; d < sizeof k_dirs / sizeof k_dirs[0]; d++) {
        scan_dir(k_dirs[d]);
    }
    for (uint8_t f = 0; f < g_nfiles; f++) {
        uint32_t n = 0;
        bool big = false;
        nfs_err_t pe = parse_file(g_fpath[f], count_cb, &n, &big);
        set_item(i++, PLS_FILE, f, g_fname[f], pe == NFS_OK, n, big);
    }
    g_stage_n = i;
    __atomic_store_n(&g_cat_ver, g_cat_ver | 1u, __ATOMIC_RELEASE);
    memcpy(g_cat, g_stage, sizeof g_stage);
    g_cat_n = g_stage_n;
    __atomic_store_n(&g_cat_ver, g_cat_ver + 1u, __ATOMIC_RELEASE);
    return PLS_OK;
}

/* ---- opening a list ------------------------------------------------------------------------------------------------------------------------------ */

static void sub_with(char *out, size_t cap, const char *artist, const char *tail) {
    char a[PLAYS_ARTIST_MAX];
    copy_cut(a, 22u < sizeof a ? 22u : sizeof a, artist); /* leave room for the tail in the 47 characters of a row's second line */
    snprintf(out, cap, "%s%s%s", a, a[0] != '\0' ? " \xC2\xB7 " : "", tail);
}

static pls_status_t open_plays(uint8_t kind) {
    // pick the best entries by repeated scans of the (small) table: Most played = plays, then the more recent; Top rated = stars, then the more recent
    uint32_t cap = kind == PLS_MOST ? PLS_MOST_N : PLS_ENTRIES_MAX;
    static uint8_t taken[PLAYS_MAX > 0 ? PLAYS_MAX : 1];
    memset(taken, 0, sizeof taken);
    plays_entry_t best, cur;
    for (uint32_t n = 0; n < cap; n++) {
        int bi = -1;
        uint32_t bscore = 0, bstamp = 0;
        for (uint32_t i = 0; i < plays_slots(); i++) {
            if (taken[i] || !plays_at(i, &cur)) {
                continue;
            }
            uint32_t score;
            if (kind == PLS_MOST) {
                score = cur.plays;
            } else {
                score = ratings_get(cur.key);
                if (score < 4) {
                    continue;
                }
            }
            if (kind == PLS_MOST && score == 0) {
                continue;
            }
            uint32_t stamp = kind == PLS_MOST ? cur.last : cur.stamp;
            if (bi < 0 || score > bscore || (score == bscore && (int32_t)(stamp - bstamp) > 0)) {
                bi = (int)i;
                bscore = score;
                bstamp = stamp;
            }
        }
        if (bi < 0) {
            break;
        }
        taken[bi] = 1;
        (void)plays_at((uint32_t)bi, &best);
        char tail[24];
        if (kind == PLS_MOST) {
            snprintf(tail, sizeof tail, "%u %s", (unsigned)best.plays, best.plays == 1 ? "play" : "plays");
        } else {
            snprintf(tail, sizeof tail, "%u stars", (unsigned)ratings_get(best.key));
        }
        char sub[48];
        sub_with(sub, sizeof sub, best.artist, tail);
        add_classified(best.path, best.title, sub); /* an untagged track has no title: the file name stands in */
        track_stream_pump();
    }
    return PLS_OK;
}

static pls_status_t open_recent(void) {
    libsvc_status_t why = LIBSVC_OK;
    lib_reader_t *r = library_service_reader(&why);
    if (r == NULL) {
        return why == LIBSVC_NO_CARD ? PLS_NO_CARD : (why == LIBSVC_NONE || why == LIBSVC_DAMAGED) ? PLS_NO_LIBRARY : PLS_ERROR;
    }
    uint32_t tracks = lib_manifest(r)->tracks;
    static uint32_t ids[PLS_RECENT_N], stamps[PLS_RECENT_N];
    uint32_t n = 0;
    for (uint32_t id = 0; id < tracks; id++) {
        uint32_t mtime = 0, size = 0;
        uint8_t play = 0;
        if (lib_track_stamp(r, id, &mtime, &size, &play) != LIB_OK) {
            return PLS_ERROR;
        }
        if (mtime != 0 && (n < PLS_RECENT_N || mtime > stamps[n - 1])) {
            uint32_t at = n < PLS_RECENT_N ? n : PLS_RECENT_N - 1u;
            while (at > 0 && stamps[at - 1] < mtime) {
                stamps[at] = stamps[at - 1];
                ids[at] = ids[at - 1];
                at--;
            }
            stamps[at] = mtime;
            ids[at] = id;
            if (n < PLS_RECENT_N) {
                n++;
            }
        }
        if ((id & 63u) == 63u) {
            track_stream_pump();
            if (storage_request_waiting()) {
                return PLS_BUSY; /* a Play press or another list wants the card: give way (the screen offers to try again) */
            }
        }
    }
    for (uint32_t k = 0; k < n; k++) {
        lib_track_t t;
        static char path[PL_PATH_MAX];
        if (lib_track(r, ids[k], &t) != LIB_OK || lib_track_path(r, ids[k], path, sizeof path) != LIB_OK) {
            return PLS_ERROR;
        }
        if (t.play != LIB_PLAY_OK) {
            (void)add_entry("", t.title, lib_cant_play_str(t.play), PLS_ENT_CANT_PLAY);
        } else {
            (void)add_entry(path, t.title, t.artist_name, PLS_ENT_OK);
        }
        track_stream_pump();
    }
    return PLS_OK;
}

static pls_status_t open_unplayed(void) {
    libsvc_status_t why = LIBSVC_OK;
    lib_reader_t *r = library_service_reader(&why);
    if (r == NULL) {
        return why == LIBSVC_NO_CARD ? PLS_NO_CARD : (why == LIBSVC_NONE || why == LIBSVC_DAMAGED) ? PLS_NO_LIBRARY : PLS_ERROR;
    }
    uint32_t tracks = lib_list_count(r, LIB_LIST_SONGS);
    /* The plays table holds at most PLAYS_MAX tracks, so among the first PLAYS_MAX + PLS_ENTRIES_MAX songs (title order) there are always enough unplayed ones. */
    uint32_t scan = PLAYS_MAX + PLS_ENTRIES_MAX + 8u;
    for (uint32_t i = 0; i < tracks && i < scan && g_n < PLS_ENTRIES_MAX; i++) {
        uint32_t id;
        lib_track_t t;
        static char path[PL_PATH_MAX];
        if (lib_list_id(r, LIB_LIST_SONGS, i, &id) != LIB_OK || lib_track(r, id, &t) != LIB_OK || lib_track_path(r, id, path, sizeof path) != LIB_OK) {
            return PLS_ERROR;
        }
        if (t.play != LIB_PLAY_OK || plays_get(ratings_key(path, t.size)) != 0) {
            continue;
        }
        (void)add_entry(path, t.title, t.artist_name, PLS_ENT_OK);
        track_stream_pump();
    }
    return PLS_OK;
}

static pls_status_t do_open(const req_t *rq) {
    pool_reset();
    if (!card_ready()) {
        return PLS_NO_CARD;
    }
    uint32_t seen = 0;
    nfs_err_t e;
    bool cut = false;
    switch (rq->kind) {
    case PLS_OTG:
        e = parse_file(otg_read_path(), open_cb, &seen, &cut);
        if (e != NFS_OK && e != NFS_ERR_NOT_FOUND) {
            return PLS_ERROR;
        }
        g_trunc = g_trunc || cut;
        return PLS_OK;
    case PLS_FILE:
        if (rq->file >= g_nfiles) {
            return PLS_BAD;
        }
        e = parse_file(g_fpath[rq->file], open_cb, &seen, &cut);
        g_trunc = g_trunc || cut;
        return e == NFS_OK ? PLS_OK : PLS_ERROR;
    case PLS_MOST:
    case PLS_TOP: return open_plays(rq->kind);
    case PLS_RECENT: return open_recent();
    case PLS_UNPLAYED: return open_unplayed();
    default: return PLS_BAD;
    }
}

/* ---- On-The-Go: rewrite the file ------------------------------------------------------------------------------------------------------------------ */

typedef struct {
    nfs_file_t *out;
    nfs_err_t err;
    uint32_t seen, kept;
    uint32_t remove_idx;
    bool clear, dup;
    const char *add_path;
} rw_t;

static void rw_write(rw_t *c, const char *text, size_t n) {
    if (c->err != NFS_OK) {
        return;
    }
    size_t put = 0;
    c->err = nfs_write(c->out, text, n, &put);
    if (c->err == NFS_OK && put != n) {
        c->err = NFS_ERR_IO;
    }
}

static void rw_entry(rw_t *c, const char *path, const char *title, const char *artist, uint32_t secs) {
    char line[PL_PATH_MAX + PL_TITLE_MAX + PL_ARTIST_MAX + 40];
    size_t n = pl_format_entry(line, sizeof line, path, title, artist, secs);
    if (n == 0) {
        c->err = NFS_ERR_INVALID;
        return;
    }
    rw_write(c, line, n);
    c->kept++;
}

static void rw_cb(void *user, const pl_entry_t *e) {
    rw_t *c = user;
    uint32_t idx = c->seen++;
    if (c->err != NFS_OK || !e->resolvable || c->clear || idx == c->remove_idx) {
        return;
    }
    if (c->add_path != NULL && cmp_nocase(e->path, c->add_path) == 0) {
        c->dup = true;
    }
    rw_entry(c, e->path, e->title, e->artist, e->secs);
}

static pls_status_t map_write(nfs_err_t e) {
    if (e == NFS_OK) {
        return PLS_OK;
    }
    if (e == NFS_ERR_BUSY) {
        return PLS_BUSY;
    }
    hal_card_info_t info;
    bool wp = hal_storage_get_info(&info) == HAL_STORAGE_OK && info.write_protected;
    return wp || e == NFS_ERR_IO ? (wp ? PLS_READONLY : PLS_ERROR) : PLS_ERROR;
}

// The whole rewrite inside the write scope: temp file written and synced, then the old file replaced (the card does not rename over a file: unlink first).
static pls_status_t otg_save(const char *add_path, uint32_t remove_idx, bool clear) {
    nfs_stat_t st;
    nfs_err_t e = nfs_stat(SETTINGS_DIR, &st);
    if (e == NFS_ERR_NOT_FOUND) {
        e = nfs_mkdir(SETTINGS_DIR);
    }
    if (e != NFS_OK) {
        return map_write(e);
    }
    nfs_file_t *f;
    e = nfs_open(&f, PLS_OTG_TMP, NFS_O_WRITE | NFS_O_CREATE | NFS_O_TRUNC);
    if (e != NFS_OK) {
        return map_write(e);
    }
    rw_t c = {.out = f, .remove_idx = remove_idx, .clear = clear, .add_path = add_path};
    rw_write(&c, pl_m3u_header, strlen(pl_m3u_header));
    nfs_err_t pe = parse_file(otg_read_path(), rw_cb, &c, NULL);
    pls_status_t res = PLS_OK;
    if (pe != NFS_OK && pe != NFS_ERR_NOT_FOUND) {
        res = PLS_ERROR;
    } else if (add_path != NULL) {
        if (c.dup) {
            res = PLS_DUPLICATE;
        } else if (c.kept >= PLS_OTG_MAX) {
            res = PLS_FULL;
        } else {
            rw_entry(&c, add_path, g_add.title, g_add.artist, g_add.secs);
        }
    } else if (!clear && remove_idx >= c.seen) {
        res = PLS_BAD;
    }
    rw_write(&c, "# end\n", 6); /* marks a complete file: a temp file without it is never taken for the list */
    if (res == PLS_OK && c.err == NFS_OK) {
        c.err = nfs_sync(f);
    }
    nfs_err_t ce = nfs_close(f);
    if (res == PLS_OK && c.err == NFS_OK) {
        c.err = ce;
    }
    if (res != PLS_OK || c.err != NFS_OK) {
        (void)nfs_unlink(PLS_OTG_TMP); /* the old file is untouched */
        return res != PLS_OK ? res : map_write(c.err);
    }
    if (nfs_stat(PLS_OTG_FILE, &st) == NFS_OK) {
        e = nfs_unlink(PLS_OTG_FILE);
        if (e != NFS_OK) {
            return map_write(e); /* the complete temp file stays and is read as the list */
        }
    }
    return map_write(nfs_rename(PLS_OTG_TMP, PLS_OTG_FILE));
}

static pls_status_t do_otg(uint8_t op, uint32_t index) {
    if (!card_ready()) {
        return PLS_NO_CARD;
    }
    hal_card_info_t info;
    if (hal_storage_get_info(&info) == HAL_STORAGE_OK && info.write_protected) {
        return PLS_READONLY;
    }
    nfs_err_t e = nfs_write_scope_enter(SETTINGS_DIR);
    if (e != NFS_OK) {
        return map_write(e);
    }
    bool was = hal_storage_get_write_enabled();
    hal_storage_set_write_enabled(true);
    pls_status_t s;
    if (op == OP_OTG_ADD) {
        s = !pq_is_audio(g_add.path) ? PLS_BAD : otg_save(g_add.path, UINT32_MAX, false);
    } else if (op == OP_OTG_REMOVE) {
        s = otg_save(NULL, index, false);
    } else {
        s = otg_save(NULL, UINT32_MAX, true);
    }
    hal_storage_set_write_enabled(was);
    nfs_write_scope_leave();
    return s;
}

/* ---- the storage task's entry --------------------------------------------------------------------------------------------------------------------- */

void pls_service_handle(const void *blob, size_t n) {
    req_t rq;
    if (n < sizeof rq) {
        return;
    }
    memcpy(&rq, blob, sizeof rq);
    switch (rq.op) {
    case OP_CATALOG: publish(&g_done_view, rq.seq, do_catalog()); break;
    case OP_OPEN: {
        __atomic_store_n(&g_ver, g_ver | 1u, __ATOMIC_RELEASE);
        pls_status_t s = do_open(&rq);
        __atomic_store_n(&g_ver, g_ver + 1u, __ATOMIC_RELEASE); /* even again, a new version */
        publish(&g_done_view, rq.seq, s);
        break;
    }
    case OP_OTG_ADD:
    case OP_OTG_REMOVE:
    case OP_OTG_CLEAR: {
        pls_status_t s = do_otg(rq.op, rq.index);
        if (rq.op == OP_OTG_ADD) {
            g_add_busy = false;
        }
        publish(&g_done_otg, rq.seq, s);
        break;
    }
    default: break;
    }
}

void pls_service_reset(void) {
    g_cat_n = 0;
    g_cat_ver = 0;
    g_nfiles = 0;
    pool_reset();
    g_ver = 0;
    g_add_busy = false;
    g_done_view.seq = g_done_otg.seq = 0;
}

#else /* the Pico 2 W (frozen): nothing, every request is refused */

bool pls_request_catalog(uint32_t seq) { (void)seq; return false; }
bool pls_request_open(uint8_t kind, uint8_t file, uint32_t seq) { (void)kind, (void)file, (void)seq; return false; }
bool pls_poll(uint32_t seq, pls_status_t *st) { (void)seq, (void)st; return false; }
uint32_t pls_catalog_count(void) { return 0; }
bool pls_catalog_item(uint32_t i, pls_item_t *out) { (void)i, (void)out; return false; }
uint32_t pls_entry_count(void) { return 0; }
bool pls_entry_truncated(void) { return false; }
bool pls_entry(uint32_t i, pls_entry_view_t *out) { (void)i, (void)out; return false; }
bool pls_otg_add(const char *path, const char *title, const char *artist, uint32_t secs, uint32_t seq) {
    (void)path, (void)title, (void)artist, (void)secs, (void)seq;
    return false;
}
bool pls_otg_remove(uint32_t index, uint32_t seq) { (void)index, (void)seq; return false; }
bool pls_otg_clear(uint32_t seq) { (void)seq; return false; }
bool pls_otg_poll(uint32_t seq, pls_status_t *st) { (void)seq, (void)st; return false; }
void pls_service_handle(const void *blob, size_t n) { (void)blob, (void)n; }
void pls_service_reset(void) {}

#endif /* PLS_AVAILABLE */
