// SPDX-License-Identifier: MPL-2.0
// The music library on the storage task (library_service.h).
#include "library_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_engine.h"
#include "capabilities.h"
#include "codec.h"
#include "hal_storage.h"
#include "os.h"
#include "play_queue.h"
#include "player.h"
#include "storage_service.h"
#include "track_stream.h"

/* ---- the shared buffer ------------------------------------------------------------------------------------------------------------ */

typedef struct {
    lib_reader_t r;
    lvw_row_t rows[LIBSVC_ROWS];
} lib_mem_t;

// The folder listing (storage_service.h: FILES_MAX_ENTRIES entries) and the library's reader + row window never need to be live together:
// the Files screen and the Music screens are not on screen at the same time. One static buffer serves both.
static union {
    file_entry_t entries[FILES_MAX_ENTRIES];
    lib_mem_t lib;
} g_mem;
// The reader plus the row window are a little larger than the folder listing since the two-line rows (a second text per row) and the fifth page of the cache
// (a song row reads five table pages): the shared buffer grows to the larger of the two, which costs under 1 KB over the listing alone.
_Static_assert(sizeof(lib_mem_t) <= sizeof(file_entry_t[FILES_MAX_ENTRIES]) + 1024u, "reader + row window must stay close to the Files listing buffer");

// Is there a card the library can use? A mounted FatFs volume on the device; the simulator's card is a folder, which the storage check has vouched for.
static bool card_ready(void) {
#ifdef NEUPOD_LIB_POSIX
    return caps_available(CAP_STORAGE);
#else
    return hal_storage_is_mounted();
#endif
}

const char *lib_cant_play_str(uint8_t play) {
    switch (play) {
    case LIB_PLAY_CODEC: return "Can't play: unsupported codec (AAC)";
    case LIB_PLAY_CHANNELS: return "Can't play: more than 2 channels";
    case LIB_PLAY_BITS: return "Can't play: unsupported bit depth";
    case LIB_PLAY_DAMAGED: return "Can't play: damaged file";
    case LIB_PLAY_RATE: return "Can't play: unsupported sample rate";
    case LIB_PLAY_BLOCK: return "Can't play: FLAC block size too large";
    default: return "Can't play: unsupported file";
    }
}

typedef enum { OWN_NONE, OWN_LIST, OWN_LIB } owner_t;
static owner_t g_owner;
static bool g_reader_open;
static bool g_recover_tried;
static bool g_ready_seen;

/* ---- answers the UI polls --------------------------------------------------------------------------------------------------------- */

typedef struct {
    volatile uint32_t seq; /* written last, with release semantics: the data for it is valid once seq matches */
    volatile int status;
} done_t;
static done_t g_done[LIBSVC_REQS];

static lvw_info_t g_cur_info; /* the last INFO request's answer: what library_info() returns and what the rows requests work from (UI and storage task never use it at once: the UI asks for rows only after it has read the info) */
static lvw_view_t g_cur_view = {LVW_KINDS, 0};
static uint32_t g_gen;             /* manifest generation g_cur_info was computed for */

/* the row window: filled by the storage task, copied by the UI under a version count (odd = being written) */
static volatile uint32_t g_rows_ver;
static lvw_view_t g_rows_view;
static uint32_t g_rows_first, g_rows_n;

/* build progress */
static volatile uint32_t g_prog_files, g_prog_total; /* files read so far; audio files the first quick pass found (0 = not counted yet) */
static volatile uint32_t g_prog_dir_ver;               /* odd while g_prog_dir is being written */
static char g_prog_dir[LIBSVC_FOLDER_MAX];             /* the folder the scan is in (its last path component) */
static volatile bool g_prog_bg;                        /* the screen was left with the build running: it carries on in the background */
static volatile bool g_prog_active, g_cancel;
static volatile uint32_t g_last[4]; /* tracks, artists, albums, genres of the last finished build */

/* the two paths handles were resolved to last (current and next track), for the resume record */
#define PATH_CACHE 2
static struct {
    char handle[24]; /* "@lib/2/4294967295" is 17 characters */
    char path[PLAYER_PATH_MAX];
    uint32_t album, artist; /* the track's album and album artist ids (LIB_NONE: a compilation): Now Playing's "Go to album / artist" */
} g_paths[PATH_CACHE];
static unsigned g_path_next;
static os_mutex_t *g_path_mu;

typedef struct {
    uint8_t op;
    lvw_view_t view;
    uint32_t first, n, seq;
} libsvc_req_t;
_Static_assert(sizeof(libsvc_req_t) < 128, "a library request travels in the storage request's path buffer");

static void publish(libsvc_kind_t k, uint32_t seq, libsvc_status_t s) {
    g_done[k].status = (int)s;
    __atomic_store_n(&g_done[k].seq, seq, __ATOMIC_RELEASE);
}

const char *library_status_text(libsvc_status_t s) {
    switch (s) {
    case LIBSVC_OK: return "";
    case LIBSVC_NO_CARD: return "No card";
    case LIBSVC_NONE: return "No library yet";
    case LIBSVC_DAMAGED: return "Library needs rebuilding";
    case LIBSVC_ERROR: return "Card read error";
    case LIBSVC_PLAYING: return "Stop playback to update the library";
    case LIBSVC_NEEDS_WRITABLE: return "Library needs a writable card";
    case LIBSVC_FULL: return "Card is full";
    case LIBSVC_CANCELLED: return "Cancelled";
    case LIBSVC_BUSY: return "Card busy, try again";
    }
    return "?";
}

/* ---- UI side ----------------------------------------------------------------------------------------------------------------------- */

static bool post(const libsvc_req_t *rq) { return storage_post_library(rq, sizeof *rq); }

bool library_request_info(lvw_view_t v, uint32_t seq) { return post(&(libsvc_req_t){.op = LIBSVC_REQ_INFO, .view = v, .seq = seq}); }

bool library_request_rows(lvw_view_t v, uint32_t first, uint32_t n, uint32_t seq) {
    return post(&(libsvc_req_t){.op = LIBSVC_REQ_ROWS, .view = v, .first = first, .n = n > LIBSVC_ROWS ? LIBSVC_ROWS : n, .seq = seq});
}

bool library_request_build(uint32_t seq) {
    g_cancel = false;
    // A paused track (the usual state after boot: resume brings the last track back paused) is stopped here, on the caller's task,
    // keeping its place for resume, so the build is not refused for it. Something actually playing still refuses (LIBSVC_PLAYING).
    // player_stop() queues its storage request before the build's, so the storage task handles it first.
    player_info_t pi;
    player_get(&pi);
    if (pi.state == PLAYER_PAUSED) {
        player_stop();
    }
    return post(&(libsvc_req_t){.op = LIBSVC_REQ_BUILD, .seq = seq});
}

void library_cancel_build(void) { g_cancel = true; }
void library_build_background(bool on) { g_prog_bg = on; }

bool library_poll(libsvc_kind_t k, uint32_t seq, libsvc_status_t *status) {
    if (__atomic_load_n(&g_done[k].seq, __ATOMIC_ACQUIRE) != seq) {
        return false;
    }
    *status = (libsvc_status_t)g_done[k].status;
    return true;
}

void library_info(lvw_info_t *out) { *out = g_cur_info; }

bool library_row(lvw_view_t v, uint32_t idx, lvw_row_t *out) {
    uint32_t ver = __atomic_load_n(&g_rows_ver, __ATOMIC_ACQUIRE);
    if ((ver & 1u) != 0 || g_owner != OWN_LIB || g_rows_view.kind != v.kind || g_rows_view.id != v.id || idx < g_rows_first ||
        idx >= g_rows_first + g_rows_n) {
        return false;
    }
    *out = g_mem.lib.rows[idx - g_rows_first];
    return __atomic_load_n(&g_rows_ver, __ATOMIC_ACQUIRE) == ver; /* refilled while we copied: a miss, the caller asks again */
}

void library_progress(library_progress_t *out) {
    out->active = g_prog_active;
    out->background = g_prog_bg;
    out->files = g_prog_files;
    out->total = g_prog_total;
    for (int tries = 0; tries < 3; tries++) { /* a torn copy of the folder name is retried; after that, the last try's text is fine */
        uint32_t v = __atomic_load_n(&g_prog_dir_ver, __ATOMIC_ACQUIRE);
        memcpy(out->folder, g_prog_dir, sizeof out->folder);
        if ((v & 1u) == 0 && __atomic_load_n(&g_prog_dir_ver, __ATOMIC_ACQUIRE) == v) {
            break;
        }
    }
    out->folder[sizeof out->folder - 1] = '\0';
    out->tracks = g_last[0];
    out->artists = g_last[1];
    out->albums = g_last[2];
    out->genres = g_last[3];
}

bool library_known_ready(void) { return g_ready_seen; }

/* ---- storage task side ------------------------------------------------------------------------------------------------------------- */

void library_service_init(void) { g_path_mu = os_mutex_create(); }

static void rows_invalidate(void) {
    __atomic_store_n(&g_rows_ver, (g_rows_ver | 1u) + 1u, __ATOMIC_RELEASE); /* even again, but a different version: copies in flight fail */
    g_rows_view.kind = LVW_KINDS;
    g_rows_n = 0;
}

static void reader_close(void) {
    if (g_reader_open) {
        lib_close(&g_mem.lib.r);
        g_reader_open = false;
    }
}

file_entry_t *library_service_take_listing(void) {
    if (g_owner == OWN_LIB) {
        rows_invalidate();
        reader_close();
        g_cur_view.kind = LVW_KINDS;
    }
    g_owner = OWN_LIST;
    return g_mem.entries;
}

static libsvc_status_t map_open(lib_err_t e) {
    switch (e) {
    case LIB_OK: return LIBSVC_OK;
    case LIB_E_NONE: return LIBSVC_NONE;
    case LIB_E_FORMAT: return LIBSVC_DAMAGED;
    default: return LIBSVC_ERROR;
    }
}

// Open the reader (taking the shared buffer over from a folder listing if need be).
static libsvc_status_t reader_ensure(void) {
    if (!card_ready()) {
        return LIBSVC_NO_CARD;
    }
    if (g_owner != OWN_LIB) {
        g_owner = OWN_LIB;
        g_reader_open = false;
        g_rows_view.kind = LVW_KINDS;
        g_rows_n = 0;
        g_cur_view.kind = LVW_KINDS;
    }
    if (g_reader_open) {
        return LIBSVC_OK;
    }
    lib_err_t e = lib_open(&g_mem.lib.r, NULL);
    if (e == LIB_E_NONE && !g_recover_tried) {
        g_recover_tried = true; // a power cut in the middle of a swap leaves the library as lib.old: put it back
        if (lib_recover(NULL) == LIB_OK) {
            e = lib_open(&g_mem.lib.r, NULL);
        }
    }
    if (e != LIB_OK) {
        return map_open(e);
    }
    g_reader_open = true;
    g_gen = lib_manifest(&g_mem.lib.r)->generation;
    g_ready_seen = true;
    return LIBSVC_OK;
}

static libsvc_status_t do_info(const libsvc_req_t *rq) {
    // A list is being opened: open the index afresh, so a card that was swapped or a library that was rebuilt since the last list is never read
    // through stale handles (and a reader left broken by an error recovers). Costs a few file opens, once per list.
    rows_invalidate();
    reader_close();
    g_cur_view.kind = LVW_KINDS; /* g_cur_info is about to be replaced: if this open fails, no list may be taken to match what is left of it */
    libsvc_status_t s = reader_ensure();
    if (s != LIBSVC_OK) {
        return s;
    }
    lib_err_t e = lvw_info(&g_mem.lib.r, rq->view, &g_cur_info);
    if (e != LIB_OK) {
        return e == LIB_E_RANGE ? LIBSVC_DAMAGED : LIBSVC_ERROR;
    }
    g_cur_view = rq->view;
    return LIBSVC_OK;
}

static libsvc_status_t do_rows(const libsvc_req_t *rq) {
    libsvc_status_t s = reader_ensure();
    if (s != LIBSVC_OK) {
        return s;
    }
    if (g_cur_view.kind != rq->view.kind || g_cur_view.id != rq->view.id) {
        g_cur_view.kind = LVW_KINDS; /* lvw_info clears g_cur_info first: a failure must not leave it standing for the old view */
        lib_err_t e = lvw_info(&g_mem.lib.r, rq->view, &g_cur_info);
        if (e != LIB_OK) {
            return LIBSVC_ERROR;
        }
        g_cur_view = rq->view;
    }
    __atomic_store_n(&g_rows_ver, g_rows_ver | 1u, __ATOMIC_RELEASE); /* odd: being written */
    g_rows_view = rq->view;
    g_rows_first = rq->first;
    g_rows_n = 0;
    libsvc_status_t st = LIBSVC_OK;
    for (uint32_t i = 0; i < rq->n && rq->first + i < g_cur_info.count; i++) {
        if (lvw_row(&g_mem.lib.r, rq->view, &g_cur_info, rq->first + i, &g_mem.lib.rows[i]) != LIB_OK) {
            st = LIBSVC_ERROR;
            reader_close(); /* the next list opens it afresh */
            break;
        }
        g_rows_n = i + 1u;
        track_stream_pump(); /* a row can cost a few page reads: keep the audio ring topped up between rows */
    }
    __atomic_store_n(&g_rows_ver, g_rows_ver + 1u, __ATOMIC_RELEASE);
    return st;
}

/* ---- building --------------------------------------------------------------------------------------------------------------------- */

static void build_total(void *user, uint32_t total) {
    (void)user;
    g_prog_total = total;
}

static void build_where(void *user, const char *folder) {
    (void)user;
    const char *leaf = strrchr(folder, '/');
    leaf = leaf != NULL ? leaf + 1 : folder;
    __atomic_add_fetch(&g_prog_dir_ver, 1u, __ATOMIC_ACQ_REL);
    snprintf(g_prog_dir, sizeof g_prog_dir, "%s", leaf);
    __atomic_add_fetch(&g_prog_dir_ver, 1u, __ATOMIC_ACQ_REL);
}

static bool build_progress(void *user, uint32_t done) {
    (void)user;
    g_prog_files = done;
    if ((done & 3u) == 0) {
        os_delay_ms(1); /* the storage task outranks the UI: a millisecond now and then lets the progress screen animate */
    }
#ifdef NEUPOD_LIB_POSIX /* simulator only: slow the scan down so a tour can look at the progress screen (NEUPOD_LIB_SLOW_MS per file) */
    static int slow = -1;
    if (slow < 0) {
        const char *e = getenv("NEUPOD_LIB_SLOW_MS");
        slow = e != NULL ? atoi(e) : 0;
    }
    if (slow > 0) {
        os_delay_ms((uint32_t)slow);
    }
#endif
    return !g_cancel && !storage_request_waiting(); /* Cancel, or a Play / another list wants the card */
}

static bool nothing_playing(void) {
    player_info_t pi;
    player_get(&pi);
    return pi.state == PLAYER_IDLE || pi.state == PLAYER_ENDED || pi.state == PLAYER_ERROR;
}

// The audio core must have let go of the codec arena before the scanner uses it as scratch: stop it, and wait (bounded) until it says so.
static bool audio_released(void) {
    track_stream_stop();
    (void)audio_stop();
    for (int i = 0; i < 100; i++) {
        audio_stats_t st;
        if (audio_stats_get(&st) && st.source == AUDIO_SRC_NONE) {
            return true;
        }
        os_delay_ms(3);
    }
    return false;
}

static libsvc_status_t map_build(lib_err_t e) {
    hal_card_info_t info;
    bool wp = hal_storage_get_info(&info) == HAL_STORAGE_OK && info.write_protected;
    switch (e) {
    case LIB_OK: return LIBSVC_OK;
    case LIB_E_CANCELLED: return LIBSVC_CANCELLED;
    case LIB_E_FULL: return wp ? LIBSVC_NEEDS_WRITABLE : LIBSVC_FULL;
    case LIB_E_BUSY: return LIBSVC_BUSY;
    case LIB_E_DENIED: return LIBSVC_NEEDS_WRITABLE;
    case LIB_E_IO: return wp ? LIBSVC_NEEDS_WRITABLE : LIBSVC_ERROR;
    default: return LIBSVC_ERROR;
    }
}

static libsvc_status_t do_build(void) {
    if (!card_ready()) {
        return LIBSVC_NO_CARD;
    }
    hal_card_info_t info;
    if (hal_storage_get_info(&info) == HAL_STORAGE_OK && info.write_protected) {
        return LIBSVC_NEEDS_WRITABLE; /* the index is written to the card: say so rather than fail halfway */
    }
    if (!nothing_playing()) {
        return LIBSVC_PLAYING;
    }
    if (!audio_released()) {
        return LIBSVC_PLAYING;
    }
    // The reader's files are replaced by the swap: let go of them, and of the shared buffer, first.
    rows_invalidate();
    reader_close();
    g_cur_view.kind = LVW_KINDS;
    size_t scratch_n = 0;
    void *scratch = codec_arena_borrow(&scratch_n);
    lib_build_cfg_t cfg = {.progress = build_progress, .total = build_total, .where = build_where};
    lib_build_stats_t st = {0};
    g_prog_files = 0;
    g_prog_total = 0;
    g_prog_dir[0] = '\0';
    g_prog_active = true;
    printf("library: build started\n");
    uint32_t t0 = os_tick_ms();
    lib_err_t e = lib_build(&cfg, scratch, scratch_n, &st);
    g_prog_active = false;
    g_prog_bg = false;
    printf("library: build %s in %u ms: %u tracks (%u playable), %u artists, %u albums, %u genres, %u sort runs, %u KB scratch\n",
           e == LIB_OK ? "finished" : lib_err_str(e), (unsigned)(os_tick_ms() - t0), (unsigned)st.tracks, (unsigned)st.playable,
           (unsigned)st.artists, (unsigned)st.albums, (unsigned)st.genres, (unsigned)st.sort_runs, (unsigned)(st.scratch_peak / 1024u));
    if (e == LIB_OK) {
        g_last[0] = st.tracks;
        g_last[1] = st.artists;
        g_last[2] = st.albums;
        g_last[3] = st.genres;
    }
    libsvc_status_t s = map_build(e);
    if (s == LIBSVC_CANCELLED && !g_cancel) {
        s = LIBSVC_BUSY; /* given way to another request, not cancelled by the user */
    }
    return s;
}

void library_service_handle(const void *blob, size_t n) {
    libsvc_req_t rq;
    if (n < sizeof rq) {
        return;
    }
    memcpy(&rq, blob, sizeof rq);
    libsvc_status_t s = LIBSVC_ERROR;
    switch (rq.op) {
    case LIBSVC_REQ_INFO: s = do_info(&rq); break;
    case LIBSVC_REQ_ROWS: s = do_rows(&rq); break;
    case LIBSVC_REQ_BUILD: s = do_build(); break;
    default: return;
    }
    publish((libsvc_kind_t)rq.op, rq.seq, s);
}

/* ---- queue handles ------------------------------------------------------------------------------------------------------------------ */

bool library_service_resolve(const char *handle, char *out, size_t n, const char **why) {
    uint8_t order;
    uint32_t pos;
    uint8_t play = 0;
    if (!pq_parse_handle(handle, &order, &pos) || order >= LIB_ORDERS) {
        *why = "Not a library entry";
        return false;
    }
    if (!card_ready()) {
        *why = "No card";
        return false;
    }
    uint32_t album = LIB_NONE, artist = LIB_NONE;
    lib_err_t e = lib_resolve_ids(NULL, (lib_order_t)order, pos, out, n, &play, &album, &artist);
    if (e != LIB_OK) {
        *why = e == LIB_E_NONE || e == LIB_E_RANGE || e == LIB_E_FORMAT ? "Library changed: update it" : "Card read error";
        return false;
    }
    if (play != LIB_PLAY_OK) {
        *why = lib_cant_play_str(play);
        return false;
    }
    if (g_path_mu != NULL && os_mutex_lock(g_path_mu, 20)) {
        unsigned k = g_path_next++ % PATH_CACHE;
        snprintf(g_paths[k].handle, sizeof g_paths[k].handle, "%s", handle);
        snprintf(g_paths[k].path, sizeof g_paths[k].path, "%s", out);
        g_paths[k].album = album;
        g_paths[k].artist = artist;
        os_mutex_unlock(g_path_mu);
    }
    return true;
}

bool library_track_ids(const char *handle, uint32_t *album, uint32_t *artist) {
    bool found = false;
    if (g_path_mu == NULL || !os_mutex_lock(g_path_mu, 20)) {
        return false;
    }
    for (unsigned i = 0; i < PATH_CACHE && !found; i++) {
        if (g_paths[i].handle[0] != '\0' && strcmp(g_paths[i].handle, handle) == 0) {
            *album = g_paths[i].album;
            *artist = g_paths[i].artist;
            found = true;
        }
    }
    os_mutex_unlock(g_path_mu);
    return found;
}

bool library_service_real_path(const char *handle, char *out, size_t n) {
    bool found = false;
    if (g_path_mu == NULL || !os_mutex_lock(g_path_mu, 20)) {
        return false;
    }
    for (unsigned i = 0; i < PATH_CACHE && !found; i++) {
        if (strcmp(g_paths[i].handle, handle) == 0 && strlen(g_paths[i].path) < n) {
            memcpy(out, g_paths[i].path, strlen(g_paths[i].path) + 1u);
            found = true;
        }
    }
    os_mutex_unlock(g_path_mu);
    return found;
}
