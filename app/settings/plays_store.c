// SPDX-License-Identifier: MPL-2.0
#include "plays_store.h"

#include <string.h>

#include "hal_storage.h"
#include "nfs.h"
#include "ratings_store.h"
#include "settings_store.h"

#if PLAYS_MAX > 0

/* Records noted by other tasks wait in a small inbox (a producer takes a few instructions, never the card); the storage task, the only reader of the table,
 * folds them in. A full inbox drops the newest record: a play count lost is better than a task that waits. */
#define INBOX_N 6u
typedef struct {
    uint32_t key;
    bool played;
    char path[PLAYS_PATH_MAX], title[PLAYS_TITLE_MAX], artist[PLAYS_ARTIST_MAX];
} inbox_t;
static inbox_t g_inbox[INBOX_N];
static volatile uint32_t g_in_head, g_in_tail; /* head: next free (producers), tail: next to read (the consumer) */
static volatile int g_in_lock;

static plays_entry_t g_t[PLAYS_MAX];
static uint32_t g_serial;
static volatile bool g_dirty;
static bool g_loaded, g_persistent;
static uint32_t g_retry_at;
static bool g_retry_set;

#define MAGIC 0x4C50504Eu /* "NPPL" little endian */
#define VERSION 1u

static void copy_cut(char *dst, size_t cap, const char *src) {
    size_t n = src != NULL ? strlen(src) : 0;
    if (n >= cap) {
        n = cap - 1u;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            n--; /* never cut a UTF-8 character in half */
        }
    }
    if (n > 0) {
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
}

/* the table, storage task (or a single-threaded test) only */
static void apply(const inbox_t *r) {
    int at = -1, free_slot = -1, victim = -1;
    for (unsigned i = 0; i < PLAYS_MAX; i++) {
        if (g_t[i].key == r->key) {
            at = (int)i;
            break;
        }
        if (g_t[i].key == 0) {
            if (free_slot < 0) {
                free_slot = (int)i;
            }
            continue;
        }
        /* eviction candidate: fewest plays, a rated one only when nothing else is left, then the least recently touched */
        if (victim < 0) {
            victim = (int)i;
        } else {
            bool rv = ratings_get(g_t[victim].key) != 0, ri = ratings_get(g_t[i].key) != 0;
            if (rv != ri ? !ri : (g_t[i].plays != g_t[victim].plays ? g_t[i].plays < g_t[victim].plays : (int32_t)(g_t[i].stamp - g_t[victim].stamp) < 0)) {
                victim = (int)i;
            }
        }
    }
    if (at < 0) {
        at = free_slot >= 0 ? free_slot : victim;
        memset(&g_t[at], 0, sizeof g_t[at]);
        g_t[at].key = r->key;
    }
    plays_entry_t *e = &g_t[at];
    memcpy(e->path, r->path, sizeof e->path);
    memcpy(e->title, r->title, sizeof e->title);
    memcpy(e->artist, r->artist, sizeof e->artist);
    e->stamp = ++g_serial;
    if (r->played) {
        if (e->plays < UINT16_MAX) {
            e->plays++;
        }
        e->last = g_serial;
    }
    g_dirty = true;
}

static void drain(void) {
    while (g_in_tail != g_in_head) {
        inbox_t r = g_inbox[g_in_tail % INBOX_N]; /* copied out before the slot is given back */
        __atomic_store_n(&g_in_tail, g_in_tail + 1u, __ATOMIC_RELEASE);
        apply(&r);
    }
}

bool plays_record(const char *path, uint64_t size, const char *title, const char *artist, bool played) {
    if (path == NULL || path[0] != '/' || strlen(path) >= PLAYS_PATH_MAX) {
        return false;
    }
    if (__atomic_exchange_n(&g_in_lock, 1, __ATOMIC_ACQUIRE) != 0) {
        /* another producer is mid-copy. Never spin: all tasks share core 0 and the UI (priority 2) outranks the monitor (1), so a UI task spinning on a lock
         * held by a preempted monitor would never let it finish. A record lost to this is as acceptable as one lost to a full inbox. */
        return false;
    }
    bool ok = g_in_head - g_in_tail < INBOX_N;
    if (ok) {
        inbox_t *r = &g_inbox[g_in_head % INBOX_N];
        r->key = ratings_key(path, size);
        r->played = played;
        copy_cut(r->path, sizeof r->path, path);
        copy_cut(r->title, sizeof r->title, title);
        copy_cut(r->artist, sizeof r->artist, artist);
        __atomic_store_n(&g_in_head, g_in_head + 1u, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&g_in_lock, 0, __ATOMIC_RELEASE);
    return ok;
}

uint16_t plays_get(uint32_t key) {
    drain();
    for (unsigned i = 0; i < PLAYS_MAX; i++) {
        if (g_t[i].key == key && key != 0) {
            return g_t[i].plays;
        }
    }
    return 0;
}

uint32_t plays_slots(void) { return PLAYS_MAX; }

bool plays_at(uint32_t i, plays_entry_t *out) {
    drain();
    if (i >= PLAYS_MAX || g_t[i].key == 0) {
        return false;
    }
    *out = g_t[i];
    return true;
}

bool plays_persistent(void) { return g_persistent; }

void plays_reset(void) {
    memset(g_t, 0, sizeof g_t);
    g_serial = 0;
    g_in_head = g_in_tail = 0;
    g_dirty = false;
    g_loaded = false;
    g_persistent = false;
    g_retry_set = false;
}

/* ---- the file ---------------------------------------------------------------------------------------------------------------------------- */

static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return c;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* Writer: a 256-byte buffer in front of the file and the running CRC (the table is never copied whole). */
typedef struct {
    nfs_file_t *f;
    uint8_t buf[256];
    size_t n;
    uint32_t crc;
    nfs_err_t err;
} wr_t;

static void wr_flush(wr_t *w) {
    if (w->err == NFS_OK && w->n > 0) {
        size_t put = 0;
        w->err = nfs_write(w->f, w->buf, w->n, &put);
        if (w->err == NFS_OK && put != w->n) {
            w->err = NFS_ERR_IO;
        }
    }
    w->n = 0;
}
static void wr_bytes(wr_t *w, const void *d, size_t n) {
    const uint8_t *p = d;
    w->crc = crc_update(w->crc, p, n);
    while (n > 0) {
        size_t k = sizeof w->buf - w->n < n ? sizeof w->buf - w->n : n;
        memcpy(w->buf + w->n, p, k);
        w->n += k;
        p += k;
        n -= k;
        if (w->n == sizeof w->buf) {
            wr_flush(w);
        }
    }
}
static void wr_str(wr_t *w, const char *s) {
    uint8_t l = (uint8_t)strlen(s);
    wr_bytes(w, &l, 1);
    wr_bytes(w, s, l);
}

static nfs_err_t save_to(const char *path) {
    nfs_file_t *f;
    nfs_err_t e = nfs_open(&f, path, NFS_O_WRITE | NFS_O_CREATE | NFS_O_TRUNC);
    if (e != NFS_OK) {
        return e;
    }
    wr_t w = {.f = f, .crc = 0xFFFFFFFFu};
    uint32_t n = 0;
    for (unsigned i = 0; i < PLAYS_MAX; i++) {
        n += g_t[i].key != 0;
    }
    uint8_t h[12];
    put32(h, MAGIC);
    put32(h + 4, VERSION);
    put32(h + 8, g_serial);
    wr_bytes(&w, h, sizeof h);
    put32(h, n);
    wr_bytes(&w, h, 4);
    for (unsigned i = 0; i < PLAYS_MAX; i++) {
        const plays_entry_t *p = &g_t[i];
        if (p->key == 0) {
            continue;
        }
        uint8_t r[14];
        put32(r, p->key);
        r[4] = (uint8_t)p->plays;
        r[5] = (uint8_t)(p->plays >> 8);
        put32(r + 6, p->last);
        put32(r + 10, p->stamp);
        wr_bytes(&w, r, sizeof r);
        wr_str(&w, p->path);
        wr_str(&w, p->title);
        wr_str(&w, p->artist);
    }
    uint8_t c[4];
    put32(c, ~w.crc);
    wr_bytes(&w, c, 4);
    wr_flush(&w);
    e = w.err;
    if (e == NFS_OK) {
        e = nfs_sync(f);
    }
    nfs_err_t ce = nfs_close(f);
    return e != NFS_OK ? e : ce;
}

bool plays_save_file(const char *path) {
    drain();
    return save_to(path) == NFS_OK;
}

/* Reader: the same buffer in front of the file. */
typedef struct {
    nfs_file_t *f;
    uint8_t buf[256];
    size_t len, pos;
    uint32_t crc;
    bool bad;
    uint64_t left; /* bytes of the file not yet pulled into buf */
} rd_t;

static bool rd_bytes(rd_t *r, void *dst, size_t n, bool crc) {
    uint8_t *d = dst;
    while (n > 0 && !r->bad) {
        if (r->pos == r->len) {
            size_t want = r->left < sizeof r->buf ? (size_t)r->left : sizeof r->buf, got = 0;
            if (want == 0 || nfs_read(r->f, r->buf, want, &got) != NFS_OK || got == 0) {
                r->bad = true;
                break;
            }
            r->left -= got;
            r->len = got;
            r->pos = 0;
        }
        size_t k = r->len - r->pos < n ? r->len - r->pos : n;
        if (d != NULL) {
            memcpy(d, r->buf + r->pos, k);
            d += k;
        }
        if (crc) {
            r->crc = crc_update(r->crc, r->buf + r->pos, k);
        }
        r->pos += k;
        n -= k;
    }
    return !r->bad;
}

static bool rd_str(rd_t *r, char *dst, size_t cap) {
    uint8_t l = 0;
    if (!rd_bytes(r, &l, 1, true) || l >= cap) {
        return false;
    }
    if (!rd_bytes(r, dst, l, true)) {
        return false;
    }
    dst[l] = '\0';
    return true;
}

/* Two passes: the first only checks the CRC over the whole file, so a damaged one never leaves the table half replaced. */
static bool load_from(const char *path) {
    for (int pass = 0; pass < 2; pass++) {
        nfs_file_t *f;
        if (nfs_open(&f, path, NFS_O_READ) != NFS_OK) {
            return false;
        }
        uint64_t size = 0;
        if (nfs_size(f, &size) != NFS_OK || size < 20u || size > (uint64_t)PLAYS_MAX * (14u + 3u + PLAYS_PATH_MAX + PLAYS_TITLE_MAX + PLAYS_ARTIST_MAX) + 20u) {
            (void)nfs_close(f);
            return false;
        }
        rd_t r = {.f = f, .crc = 0xFFFFFFFFu, .left = size};
        bool ok = true;
        if (pass == 0) {
            uint8_t head[12];
            ok = rd_bytes(&r, head, sizeof head, true) && get32(head) == MAGIC && get32(head + 4) == VERSION;
            while (ok && r.left + (r.len - r.pos) > 4u) {
                size_t skip = (size_t)(r.left + (r.len - r.pos) - 4u);
                ok = rd_bytes(&r, NULL, skip < 256u ? skip : 256u, true);
            }
            uint8_t tail[4];
            ok = ok && rd_bytes(&r, tail, 4, false) && get32(tail) == ~r.crc;
        } else {
            uint8_t head[16];
            ok = rd_bytes(&r, head, sizeof head, false);
            uint32_t cnt = ok ? get32(head + 12) : 0;
            ok = ok && cnt <= PLAYS_MAX;
            memset(g_t, 0, sizeof g_t);
            g_serial = ok ? get32(head + 8) : 0;
            for (uint32_t i = 0; ok && i < cnt; i++) {
                uint8_t rec[14];
                plays_entry_t *p = &g_t[i];
                ok = rd_bytes(&r, rec, sizeof rec, false) && rd_str(&r, p->path, sizeof p->path) && rd_str(&r, p->title, sizeof p->title) &&
                     rd_str(&r, p->artist, sizeof p->artist);
                if (ok) {
                    p->key = get32(rec);
                    p->plays = (uint16_t)(rec[4] | (rec[5] << 8));
                    p->last = get32(rec + 6);
                    p->stamp = get32(rec + 10);
                    if (p->key == 0 || p->path[0] != '/') {
                        memset(p, 0, sizeof *p); /* a record that cannot be used is dropped */
                    }
                }
            }
            if (!ok) {
                memset(g_t, 0, sizeof g_t); /* cannot happen after the CRC pass; start clean rather than keep half a table */
            }
        }
        (void)nfs_close(f);
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool plays_load_file(const char *path) { return load_from(path); }

/* Fold a RAM entry (noted before the card was ready) into the table the file just filled: the plays add up. */
static void merge(const plays_entry_t *b) {
    inbox_t r = {.key = b->key, .played = false};
    memcpy(r.path, b->path, sizeof r.path);
    memcpy(r.title, b->title, sizeof r.title);
    memcpy(r.artist, b->artist, sizeof r.artist);
    apply(&r); /* finds or makes the entry and touches it */
    for (unsigned j = 0; j < PLAYS_MAX; j++) {
        if (g_t[j].key == b->key) {
            uint32_t sum = (uint32_t)g_t[j].plays + b->plays;
            g_t[j].plays = sum > UINT16_MAX ? UINT16_MAX : (uint16_t)sum;
            if (b->last != 0) {
                g_t[j].last = g_serial;
            }
        }
    }
}

static void do_load(void) {
    static plays_entry_t before[PLAYS_MAX]; /* storage task only: off its stack */
    memcpy(before, g_t, sizeof before);
    uint32_t serial = g_serial;
    bool had = g_dirty;
    /* a complete temp file is newer than the main one (a cut fell between the unlink and the rename of a save); a partial one fails its CRC */
    if (load_from(PLAYS_TMP) || load_from(PLAYS_FILE)) {
        for (unsigned i = 0; had && i < PLAYS_MAX; i++) {
            if (before[i].key != 0) {
                merge(&before[i]);
            }
        }
    } else {
        g_serial = serial; /* no usable file: the RAM table stands as it is */
    }
    g_loaded = true;
}

static nfs_err_t do_save(void) {
    nfs_stat_t st;
    nfs_err_t e = nfs_stat(SETTINGS_DIR, &st);
    if (e == NFS_ERR_NOT_FOUND) {
        e = nfs_mkdir(SETTINGS_DIR);
    }
    if (e != NFS_OK) {
        return e;
    }
    e = save_to(PLAYS_TMP);
    if (e != NFS_OK) {
        (void)nfs_unlink(PLAYS_TMP); /* the old main file is untouched */
        return e;
    }
    if (nfs_stat(PLAYS_FILE, &st) == NFS_OK) {
        e = nfs_unlink(PLAYS_FILE);
        if (e != NFS_OK) {
            return e; /* the complete temp file stays; the next save overwrites it */
        }
    }
    return nfs_rename(PLAYS_TMP, PLAYS_FILE);
}

void plays_poll(uint32_t now_ms) {
    drain();
    settings_status_t ss;
    settings_get_status(&ss);
    if (!ss.persistent) {
        g_persistent = false; /* no usable card: RAM only */
        return;
    }
    if (!g_loaded) {
        do_load();
        g_persistent = !g_dirty;
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
        g_retry_at = now_ms + (e == NFS_ERR_BUSY ? 1000u : PLAYS_RETRY_MS);
    }
}

#else /* the Pico 2 W (frozen): no table, nothing to do */

bool plays_record(const char *path, uint64_t size, const char *title, const char *artist, bool played) {
    (void)path, (void)size, (void)title, (void)artist, (void)played;
    return false;
}
uint16_t plays_get(uint32_t key) { (void)key; return 0; }
uint32_t plays_slots(void) { return 0; }
bool plays_at(uint32_t i, plays_entry_t *out) { (void)i, (void)out; return false; }
bool plays_persistent(void) { return false; }
void plays_poll(uint32_t now_ms) { (void)now_ms; }
bool plays_save_file(const char *path) { (void)path; return false; }
bool plays_load_file(const char *path) { (void)path; return false; }
void plays_reset(void) {}

#endif
