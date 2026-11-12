// SPDX-License-Identifier: MPL-2.0
#include "ratings_store.h"

#include <string.h>

#include "hal_storage.h"
#include "nfs.h"
#include "settings_store.h"

typedef struct {
    uint32_t key; /* 0 = free */
    uint8_t stars;
    uint32_t stamp;
} entry_t;

static entry_t g_t[RATINGS_MAX];
static uint32_t g_clock;
static volatile bool g_dirty;
static bool g_loaded, g_persistent;
static uint32_t g_retry_at;
static bool g_retry_set;

#define MAGIC 0x5452504Eu /* "NPRT" little endian */
#define VERSION 1u

uint32_t ratings_key(const char *path, uint64_t size) {
    uint32_t h = 2166136261u;
    for (const char *s = path; *s != '\0'; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    h ^= (uint32_t)size * 2654435761u;
    h ^= (uint32_t)(size >> 32) * 40503u;
    return h != 0 ? h : 1u;
}

uint8_t ratings_get(uint32_t key) {
    for (unsigned i = 0; i < RATINGS_MAX; i++) {
        if (g_t[i].key == key && key != 0) {
            return g_t[i].stars;
        }
    }
    return 0;
}

void ratings_set(uint32_t key, uint8_t stars) {
    if (key == 0 || stars > 5) {
        return;
    }
    int free_slot = -1, oldest = 0, at = -1;
    for (unsigned i = 0; i < RATINGS_MAX; i++) {
        if (g_t[i].key == key) {
            at = (int)i;
        } else if (g_t[i].key == 0 && free_slot < 0) {
            free_slot = (int)i;
        }
        if ((int32_t)(g_t[i].stamp - g_t[oldest].stamp) < 0) {
            oldest = (int)i;
        }
    }
    if (at < 0) {
        if (stars == 0) {
            return;
        }
        at = free_slot >= 0 ? free_slot : oldest;
    }
    if (stars == 0) {
        memset(&g_t[at], 0, sizeof g_t[at]);
    } else {
        g_t[at].key = key;
        g_t[at].stars = stars;
        g_t[at].stamp = ++g_clock;
    }
    g_dirty = true;
}

bool ratings_persistent(void) { return g_persistent; }

void ratings_reset(void) {
    memset(g_t, 0, sizeof g_t);
    g_clock = 0;
    g_dirty = false;
    g_loaded = false;
    g_persistent = false;
    g_retry_set = false;
}

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

size_t ratings_serialise(uint8_t *out, size_t cap) {
    uint32_t n = 0;
    for (unsigned i = 0; i < RATINGS_MAX; i++) {
        n += g_t[i].key != 0;
    }
    size_t need = 12u + (size_t)n * 5u + 4u;
    if (cap < need) {
        return 0;
    }
    put32(out, MAGIC);
    put32(out + 4, VERSION);
    put32(out + 8, n);
    size_t at = 12;
    for (uint32_t pass = 0; pass < RATINGS_MAX && at < need - 4u; pass++) {
        const entry_t *e = &g_t[pass];
        if (e->key != 0) {
            put32(out + at, e->key);
            out[at + 4] = e->stars;
            at += 5;
        }
    }
    put32(out + at, crc32(out, at));
    return at + 4u;
}

bool ratings_parse(const uint8_t *in, size_t n) {
    if (n < 16 || get32(in) != MAGIC || get32(in + 4) != VERSION) {
        return false;
    }
    uint32_t cnt = get32(in + 8);
    if (cnt > RATINGS_MAX || n != 12u + (size_t)cnt * 5u + 4u || get32(in + n - 4) != crc32(in, n - 4)) {
        return false;
    }
    memset(g_t, 0, sizeof g_t);
    g_clock = 0;
    for (uint32_t i = 0; i < cnt; i++) {
        const uint8_t *e = in + 12 + i * 5u;
        if (get32(e) != 0 && e[4] >= 1 && e[4] <= 5) {
            g_t[i].key = get32(e);
            g_t[i].stars = e[4];
            g_t[i].stamp = ++g_clock;
        }
    }
    return true;
}

static void do_load(void) {
    static uint8_t img[12 + RATINGS_MAX * 5 + 4];
    nfs_file_t *f;
    if (nfs_open(&f, RATINGS_FILE, NFS_O_READ) == NFS_OK) {
        size_t got = 0;
        nfs_err_t e = nfs_read(f, img, sizeof img, &got);
        (void)nfs_close(f);
        if (e == NFS_OK) {
            entry_t keep[RATINGS_MAX]; /* ratings made before the card was ready win over the file's */
            memcpy(keep, g_t, sizeof keep);
            if (ratings_parse(img, got)) {
                for (unsigned i = 0; i < RATINGS_MAX; i++) {
                    if (keep[i].key != 0) {
                        ratings_set(keep[i].key, keep[i].stars);
                    }
                }
            }
        }
    }
    g_loaded = true;
}

static nfs_err_t do_save(void) {
    static uint8_t img[12 + RATINGS_MAX * 5 + 4];
    size_t n = ratings_serialise(img, sizeof img);
    nfs_stat_t st;
    nfs_err_t e = nfs_stat(SETTINGS_DIR, &st);
    if (e == NFS_ERR_NOT_FOUND) {
        e = nfs_mkdir(SETTINGS_DIR);
    }
    if (e != NFS_OK) {
        return e;
    }
    nfs_file_t *f;
    e = nfs_open(&f, RATINGS_FILE, NFS_O_WRITE | NFS_O_CREATE | NFS_O_TRUNC);
    if (e != NFS_OK) {
        return e;
    }
    size_t put = 0;
    e = nfs_write(f, img, n, &put);
    if (e == NFS_OK) {
        e = nfs_sync(f);
    }
    nfs_err_t ce = nfs_close(f);
    return e != NFS_OK ? e : ce;
}

void ratings_poll(uint32_t now_ms) {
    settings_status_t ss;
    settings_get_status(&ss);
    if (!ss.persistent) {
        g_persistent = false; /* no usable card: RAM only */
        return;
    }
    if (!g_loaded) {
        do_load();
        g_persistent = !g_dirty; /* what the card had is in; nothing waits to be saved */
    }
    if (!g_dirty && g_persistent) {
        return;
    }
    if (g_retry_set && (int32_t)(now_ms - g_retry_at) < 0) {
        return;
    }
    g_dirty = false;
    nfs_err_t e = nfs_write_scope_enter(SETTINGS_DIR);
    if (e == NFS_OK) {
        bool was = hal_storage_get_write_enabled();
        hal_storage_set_write_enabled(true);
        e = do_save();
        hal_storage_set_write_enabled(was);
        nfs_write_scope_leave();
    }
    if (e == NFS_OK) {
        g_persistent = true;
        g_retry_set = false;
    } else {
        g_dirty = true; /* write-protected, busy or failing card: stay in RAM, try again later */
        g_persistent = false;
        g_retry_set = true;
        g_retry_at = now_ms + (e == NFS_ERR_BUSY ? 1000u : RATINGS_RETRY_MS);
    }
}
