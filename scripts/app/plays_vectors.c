// SPDX-License-Identifier: MPL-2.0
// Vectors for the app's port of the firmware's play counting and track statistics (mobile/lib/domain/library/play_stats.dart),
// printed by the firmware's own code compiled for the host:
//   - "tracker": app/runtime/play_tracker.c, ptrk_update(): when a track start counts as a play. Each pass is
//     [now_ms, playing, ended, track_id, duration_ms, counted], replayed in order from ptrk_init().
//   - "store": app/settings/ratings_store.c (ratings_set / ratings_get) and app/settings/plays_store.c (plays_record /
//     plays_at): what a sequence of ratings and records leaves per track. Each op is ["rate", path, size, stars] or
//     ["record", path, size, played]; "after" lists every track of the pool as [path, size, stars, plays, last, stamp]
//     (0 where the firmware has no entry). The pool stays far below RATINGS_MAX and PLAYS_MAX (128), so no entry is
//     ever evicted: the app keeps every track (constitution IV: memory lets a device do less, never differently), and
//     paths are under PLAYS_PATH_MAX for the same reason.
//   - "saturate": plays_record(played) repeated: the u16 count stops at 65535.
// Built and run by scripts/app/gen-plays-vectors.sh; CI diffs the output against mobile/test/fixtures/plays/firmware_vectors.json.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal_storage.h"
#include "nfs.h"
#include "play_tracker.h"
#include "plays_store.h"
#include "ratings_store.h"
#include "settings_store.h"

/* ---- stubs: no card. ratings_poll / plays_poll are never called; these only satisfy the linker. ---- */
nfs_err_t nfs_open(nfs_file_t **out, const char *path, unsigned flags) { (void)out, (void)path, (void)flags; return NFS_ERR_IO; }
nfs_err_t nfs_close(nfs_file_t *f) { (void)f; return NFS_ERR_IO; }
nfs_err_t nfs_read(nfs_file_t *f, void *buf, size_t n, size_t *got) { (void)f, (void)buf, (void)n, (void)got; return NFS_ERR_IO; }
nfs_err_t nfs_write(nfs_file_t *f, const void *buf, size_t n, size_t *put) { (void)f, (void)buf, (void)n, (void)put; return NFS_ERR_IO; }
nfs_err_t nfs_size(nfs_file_t *f, uint64_t *size) { (void)f, (void)size; return NFS_ERR_IO; }
nfs_err_t nfs_sync(nfs_file_t *f) { (void)f; return NFS_ERR_IO; }
nfs_err_t nfs_mkdir(const char *path) { (void)path; return NFS_ERR_IO; }
nfs_err_t nfs_unlink(const char *path) { (void)path; return NFS_ERR_IO; }
nfs_err_t nfs_rename(const char *from, const char *to) { (void)from, (void)to; return NFS_ERR_IO; }
nfs_err_t nfs_stat(const char *path, nfs_stat_t *out) { (void)path, (void)out; return NFS_ERR_IO; }
nfs_err_t nfs_write_scope_enter(const char *dir) { (void)dir; return NFS_ERR_IO; }
void nfs_write_scope_leave(void) {}
void hal_storage_set_write_enabled(bool enabled) { (void)enabled; }
bool hal_storage_get_write_enabled(void) { return false; }
void settings_get_status(settings_status_t *out) { memset(out, 0, sizeof *out); }

/* ---- a small deterministic generator (the vectors must not change between runs) ---- */
static uint32_t g_rng = 0x2545F491u;
static uint32_t rnd(uint32_t n) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (g_rng >> 8) % n;
}

static bool g_first;
static void sep(void) {
    if (!g_first) {
        printf(",");
    }
    g_first = false;
}

static void json_str(const char *s) {
    putchar('"');
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            printf("\\%c", c);
        } else if (c < 0x20) {
            printf("\\u%04x", c);
        } else {
            putchar(c); /* UTF-8 passes through */
        }
    }
    putchar('"');
}

/* ---- tracker ---- */
typedef struct {
    uint32_t now, id, dur;
    bool playing, ended;
} pass_t;

static ptrk_t g_t;
static void pass(pass_t p) {
    bool counted = ptrk_update(&g_t, p.now, p.playing, p.ended, p.id, p.dur);
    sep();
    printf("[%u,%d,%d,%u,%u,%d]", (unsigned)p.now, p.playing, p.ended, (unsigned)p.id, (unsigned)p.dur, counted);
}

static void scripted(const char *name, const pass_t *ps, size_t n) {
    ptrk_init(&g_t);
    printf("{\"name\":");
    json_str(name);
    printf(",\"passes\":[");
    g_first = true;
    for (size_t i = 0; i < n; i++) {
        pass(ps[i]);
    }
    printf("]}");
}

static void every_100ms(const char *name, uint32_t start, uint32_t id, uint32_t dur, uint32_t passes, bool ended_at_last) {
    ptrk_init(&g_t);
    printf("{\"name\":");
    json_str(name);
    printf(",\"passes\":[");
    g_first = true;
    for (uint32_t i = 0; i < passes; i++) {
        bool last = i + 1 == passes;
        pass((pass_t){.now = start + i * 100u, .playing = !(last && ended_at_last), .ended = last && ended_at_last, .id = id, .dur = dur});
    }
    printf("]}");
}

static void random_run(const char *name, uint32_t start, uint32_t passes) {
    static const uint32_t durs[] = {0, 1, 2500, 5000, 6000, 61000, 240000};
    ptrk_init(&g_t);
    printf("{\"name\":");
    json_str(name);
    printf(",\"passes\":[");
    g_first = true;
    uint32_t now = start, id = 1, dur = durs[rnd(7)];
    for (uint32_t i = 0; i < passes; i++) {
        uint32_t r = rnd(100);
        now += r < 5 ? 1000u + rnd(30000) : rnd(1500); /* mostly 0..1.5 s apart, now and then held up for longer */
        if (rnd(40) == 0) {
            id++; /* another track starts */
            dur = durs[rnd(7)];
        }
        bool ended = rnd(30) == 0;
        bool playing = !ended && rnd(10) < 8;
        pass((pass_t){.now = now, .playing = playing, .ended = ended, .id = id, .dur = dur});
    }
    printf("]}");
}

/* ---- store ---- */
static const char *const k_paths[] = {
    "/Music/Aurora Lane/First Light/01 Harbour Lights.flac",
    "/Music/Aurora Lane/First Light/02 Tidewater.flac",
    "/Music/Various/Ümlaut Ärger.mp3",
    "/Music/日本語/曲.m4a",
    "/a.wav",
    "/Music/Disc Two/03.flac",
};
#define N_PATHS (sizeof k_paths / sizeof k_paths[0])
static const uint64_t k_sizes[] = {1234567u, 4294967296ull + 17u, 0u};
#define N_SIZES (sizeof k_sizes / sizeof k_sizes[0])

static void dump_pool(void) {
    printf(",\"after\":[");
    g_first = true;
    for (unsigned p = 0; p < N_PATHS; p++) {
        for (unsigned s = 0; s < N_SIZES; s++) {
            uint32_t key = ratings_key(k_paths[p], k_sizes[s]);
            plays_entry_t e;
            memset(&e, 0, sizeof e);
            for (uint32_t i = 0; i < plays_slots(); i++) {
                plays_entry_t x;
                if (plays_at(i, &x) && x.key == key) {
                    e = x;
                }
            }
            sep();
            printf("[");
            json_str(k_paths[p]);
            printf(",%llu,%u,%u,%u,%u]", (unsigned long long)k_sizes[s], (unsigned)ratings_get(key), (unsigned)e.plays, (unsigned)e.last,
                   (unsigned)e.stamp);
        }
    }
    printf("]");
}

static void store_run(const char *name, uint32_t ops) {
    ratings_reset();
    plays_reset();
    printf("{\"name\":");
    json_str(name);
    printf(",\"ops\":[");
    g_first = true;
    for (uint32_t i = 0; i < ops; i++) {
        unsigned p = rnd(N_PATHS), s = rnd(N_SIZES);
        sep();
        if (rnd(3) == 0) {
            uint8_t stars = (uint8_t)rnd(8); /* 6 and 7 are out of range: ratings_set ignores them */
            ratings_set(ratings_key(k_paths[p], k_sizes[s]), stars);
            printf("[\"rate\",");
            json_str(k_paths[p]);
            printf(",%llu,%u]", (unsigned long long)k_sizes[s], (unsigned)stars);
        } else {
            bool played = rnd(3) != 0;
            (void)plays_record(k_paths[p], k_sizes[s], "", "", played);
            (void)plays_get(0); /* the storage task folds the inbox in (it holds 6) */
            printf("[\"record\",");
            json_str(k_paths[p]);
            printf(",%llu,%s]", (unsigned long long)k_sizes[s], played ? "true" : "false");
        }
    }
    printf("]");
    dump_pool();
    printf("}");
}

int main(void) {
    printf("{\"tracker\":[");
    /* half of a known length heard, at 100 ms passes */
    every_100ms("half of 10 s, at 100 ms passes", 1000u, 7u, 10000u, 60u, false);
    printf(",");
    every_100ms("odd length 9999 ms", 0u, 1u, 9999u, 60u, false);
    printf(",");
    every_100ms("length unknown: 30 s", 5u, 3u, 0u, 310u, false);
    printf(",");
    every_100ms("ends by itself after 3 s heard", 0u, 9u, 600000u, 32u, true);
    printf(",");
    every_100ms("ends by itself after 2.9 s heard", 0u, 9u, 600000u, 31u, true);
    printf(",");
    every_100ms("the clock wraps", 4294967295u - 2000u, 2u, 6000u, 40u, false);
    printf(",");
    {
        const pass_t ps[] = {
            {0, 1, 2000, true, false},    {100, 1, 2000, true, false},   {5000, 1, 2000, true, false}, /* held up 4.9 s: adds 1 s, counts */
            {5100, 1, 2000, true, false}, {5200, 2, 2000, true, false},  {5300, 2, 2000, false, false},
            {9300, 2, 2000, false, false}, {9400, 2, 2000, true, false}, {10400, 2, 2000, true, false},
        };
        scripted("a gap adds 1 s; paused time does not count; a new id starts again", ps, sizeof ps / sizeof ps[0]);
    }
    printf(",");
    {
        const pass_t ps[] = {
            {0, 4, 1000, true, false}, {100, 4, 1000, true, false}, {200, 4, 1000, true, false}, {300, 4, 1000, true, false},
            {400, 4, 1000, true, false}, {500, 4, 1000, true, false}, {600, 4, 1000, true, false}, {700, 4, 1000, false, true},
            {800, 4, 1000, false, true}, {900, 5, 1000, false, true}, {1000, 5, 1000, false, true},
        };
        scripted("counted once per start; ended while paused", ps, sizeof ps / sizeof ps[0]);
    }
    printf(",");
    {
        const pass_t ps[] = {
            {0, 1, 1, true, false},   {0, 1, 1, true, false},       {1, 1, 1, true, false},
            {1, 2, 0, false, true},   {3001, 2, 0, false, true},    {3002, 3, 0, true, true},
            {4002, 3, 0, true, true}, {5002, 3, 0, true, true},     {6002, 3, 0, true, true},
        };
        scripted("1 ms tracks, ended without play, ended while playing", ps, sizeof ps / sizeof ps[0]);
    }
    for (int i = 0; i < 8; i++) {
        char name[32];
        snprintf(name, sizeof name, "random %d", i);
        printf(",");
        random_run(name, i == 3 ? 4294967295u - 20000u : (uint32_t)i * 977u, 400u);
    }
    printf("],\"store\":[");
    for (int i = 0; i < 12; i++) {
        char name[32];
        snprintf(name, sizeof name, "random %d", i);
        if (i > 0) {
            printf(",");
        }
        store_run(name, 20u + (uint32_t)i * 15u);
    }
    printf("],\"saturate\":");
    ratings_reset();
    plays_reset();
    for (uint32_t i = 0; i < 65540u; i++) {
        (void)plays_record("/a.flac", 1u, "", "", true);
        (void)plays_get(0);
    }
    plays_entry_t e;
    (void)plays_at(0, &e);
    printf("{\"records\":65540,\"plays\":%u,\"last\":%u,\"stamp\":%u}}\n", (unsigned)e.plays, (unsigned)e.last, (unsigned)e.stamp);
    return 0;
}
