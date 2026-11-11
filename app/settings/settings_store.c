// SPDX-License-Identifier: MPL-2.0
// Persistent key/value store on the SD card. Design and rules: app/include/settings_store.h, docs/bluetooth.md.
#include <string.h>

#include "crypto_secret.h"
#include "hal_storage.h"
#include "nfs.h"
#include "settings_store.h"

#define MAGIC0 'N'
#define MAGIC1 'P'
#define MAGIC2 'S'
#define MAGIC3 'T'
#define HEADER_BYTES 12u /* magic(4) version(2) count(2) seq(4) */
#define ITEM_OVERHEAD 5u /* tag(4) len(1) */
#define CRC_BYTES 4u
#define IMAGE_MAX (HEADER_BYTES + SETTINGS_MAX_ITEMS * (ITEM_OVERHEAD + SETTINGS_VALUE_MAX) + CRC_BYTES)

typedef struct {
    uint32_t tag;
    uint8_t len; /* 0 = slot free */
    uint8_t val[SETTINGS_VALUE_MAX];
} item_t;

static item_t g_items[SETTINGS_MAX_ITEMS];
static settings_lock_t g_lock;
static bool g_known;    /* settings_attach_storage() has been told whether there is a card */
static bool g_attached;
static bool g_loaded;
static bool g_dirty;
static settings_load_result_t g_load_result;
static uint32_t g_seq;
static uint32_t g_saves;
static uint32_t g_fail_total;
static uint32_t g_fail_streak;
static volatile uint32_t g_generation;
static int g_crash_step = -1;
static uint8_t g_image[IMAGE_MAX]; /* serialisation buffer: only the one task that loads/flushes touches it */

static void lock(void) {
    if (g_lock.lock != NULL) {
        g_lock.lock(g_lock.ctx);
    }
}
static void unlock(void) {
    if (g_lock.unlock != NULL) {
        g_lock.unlock(g_lock.ctx);
    }
}

/* ---- CRC-32 (IEEE, reflected), bitwise: the image is a kilobyte and written a few times per session ---- */

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n-- > 0) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

static void put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (get16(p + 2) << 16); }

/* ---- image <-> items ---- */

/* Caller holds the lock. Returns the image length. */
static uint32_t serialise(uint32_t seq) {
    uint8_t *p = g_image;
    uint32_t count = 0;
    p[0] = MAGIC0;
    p[1] = MAGIC1;
    p[2] = MAGIC2;
    p[3] = MAGIC3;
    put16(p + 4, SETTINGS_VERSION);
    put32(p + 8, seq);
    p += HEADER_BYTES;
    for (uint32_t i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (g_items[i].len == 0) {
            continue;
        }
        put32(p, g_items[i].tag);
        p[4] = g_items[i].len;
        memcpy(p + ITEM_OVERHEAD, g_items[i].val, g_items[i].len);
        p += ITEM_OVERHEAD + g_items[i].len;
        count++;
    }
    put16(g_image + 6, count);
    uint32_t body = (uint32_t)(p - g_image);
    put32(p, crc32(g_image, body));
    return body + CRC_BYTES;
}

/* Validate an image of @p n bytes. On success fills @p items (cleared first) and @p seq. Touches no global state. */
static bool g_newer_seen;  /* set by parse() for the file just read */
static uint8_t g_newer_mask; /* bit 0 = file A, bit 1 = file B holds a NEWER layout: kept aside (settings.old) at the next save */

/* @p items NULL: validate only (nothing is stored). */
static bool parse(const uint8_t *img, uint32_t n, item_t *items, uint32_t *seq) {
    if (n < HEADER_BYTES + CRC_BYTES || n > IMAGE_MAX) {
        return false;
    }
    if (img[0] != MAGIC0 || img[1] != MAGIC1 || img[2] != MAGIC2 || img[3] != MAGIC3) {
        return false;
    }
    if (get32(img + n - CRC_BYTES) != crc32(img, n - CRC_BYTES)) {
        return false;
    }
    if (get16(img + 4) > SETTINGS_VERSION) {
        g_newer_seen = true; /* a rollback: do not wipe the newer firmware's file, move it aside when we save */
        return false;
    }
    if (get16(img + 4) != SETTINGS_VERSION && get16(img + 4) != SETTINGS_VERSION_V1) {
        return false; /* an older layout: no migration exists yet */
    }
    uint32_t count = get16(img + 6);
    if (count > SETTINGS_MAX_ITEMS) {
        return false;
    }
    if (items != NULL) {
        memset(items, 0, sizeof(item_t) * SETTINGS_MAX_ITEMS);
    }
    const uint8_t *p = img + HEADER_BYTES;
    const uint8_t *end = img + n - CRC_BYTES;
    for (uint32_t i = 0; i < count; i++) {
        if ((uint32_t)(end - p) < ITEM_OVERHEAD) {
            return false;
        }
        uint32_t len = p[4];
        if (len == 0 || len > SETTINGS_VALUE_MAX || (uint32_t)(end - p) < ITEM_OVERHEAD + len) {
            return false;
        }
        if (items != NULL) {
            items[i].tag = get32(p);
            items[i].len = (uint8_t)len;
            memcpy(items[i].val, p + ITEM_OVERHEAD, len);
        }
        p += ITEM_OVERHEAD + len;
    }
    *seq = get32(img + 8);
    return p == end;
}

/* ---- file helpers ---- */

typedef enum { FILE_MISSING, FILE_BAD, FILE_GOOD, FILE_IOERR } file_state_t;

static int g_test_fault; /* test hook: the next committing read fails: 1 = does not parse, 2 = I/O error */

/* Read and validate one file. @p commit false: validate only (CRC, structure, sequence). true: parse it into g_items under the lock
 * (g_items is read by other tasks, so it is only touched inside the lock; the parse is CPU only). */
static file_state_t read_file(const char *path, bool commit, uint32_t *seq) {
    nfs_file_t *f;
    nfs_err_t e = nfs_open(&f, path, NFS_O_READ);
    if (e == NFS_ERR_NOT_FOUND) {
        return FILE_MISSING;
    }
    if (e != NFS_OK) {
        return FILE_IOERR;
    }
    uint64_t size = 0;
    size_t got = 0;
    e = nfs_size(f, &size);
    file_state_t st = FILE_BAD;
    if (e != NFS_OK) {
        st = FILE_IOERR;
    } else if (size <= IMAGE_MAX) {
        e = nfs_read(f, g_image, (size_t)size, &got);
        if (e != NFS_OK) {
            st = FILE_IOERR;
        } else if (got == size) {
            int fault = commit ? g_test_fault : 0;
            if (commit) {
                g_test_fault = 0;
            }
            if (fault == 2) {
                st = FILE_IOERR;
            } else {
                /* Validate first, outside the lock: g_items is only touched by a parse that is known to succeed, so readers never see a half-filled store. */
                bool ok = fault != 1 && parse(g_image, (uint32_t)size, NULL, seq);
                if (ok && commit) {
                    lock();
                    ok = parse(g_image, (uint32_t)size, g_items, seq);
                    unlock();
                }
                st = ok ? FILE_GOOD : FILE_BAD;
            }
        }
    }
    (void)nfs_close(f);
    return st;
}

/* ---- public: lifecycle ---- */

void settings_init(const settings_lock_t *l) {
    memset(g_items, 0, sizeof g_items);
    memset(&g_lock, 0, sizeof g_lock);
    if (l != NULL) {
        g_lock = *l;
    }
    g_known = g_attached = g_loaded = g_dirty = false;
    g_load_result = SETTINGS_LOAD_NONE;
    g_seq = g_saves = g_fail_total = g_fail_streak = 0;
    g_generation = 0;
    g_crash_step = -1;
}

void settings_attach_storage(bool available) {
    lock();
    g_known = true;
    g_attached = available;
    if (available) {
        g_fail_streak = 0;
    }
    unlock();
}

static int g_active = -1; /* 0 = A holds the newest valid state, 1 = B, -1 = neither; the next save goes to the other one */

settings_load_result_t settings_load(void) {
    if (!g_attached) {
        return SETTINGS_LOAD_NONE;
    }
    uint32_t a_seq = 0, b_seq = 0;
    bool pick_is_a = false;
    g_newer_mask = 0;
    g_newer_seen = false;
    file_state_t a = read_file(SETTINGS_FILE_A, false, &a_seq);
    g_newer_mask |= g_newer_seen ? 1u : 0u;
    g_newer_seen = false;
    file_state_t b = read_file(SETTINGS_FILE_B, false, &b_seq);
    g_newer_mask |= g_newer_seen ? 2u : 0u;

    settings_load_result_t res;
    int active = -1;
    uint32_t use_seq = 0;
    if (a == FILE_IOERR || b == FILE_IOERR) {
        res = SETTINGS_LOAD_IO_ERROR;
    } else if (a == FILE_GOOD || b == FILE_GOOD) {
        pick_is_a = a == FILE_GOOD && (b != FILE_GOOD || (int32_t)(a_seq - b_seq) > 0);
        res = (a == FILE_BAD || b == FILE_BAD) ? SETTINGS_LOAD_RECOVERED : SETTINGS_LOAD_OK;
        /* The winner is read again, this time into g_items (no second copy of the store in RAM). If that fails, fall back to the other. */
        file_state_t got = read_file(pick_is_a ? SETTINGS_FILE_A : SETTINGS_FILE_B, true, &use_seq);
        active = pick_is_a ? 0 : 1;
        if (got != FILE_GOOD) {
            bool other_good = pick_is_a ? b == FILE_GOOD : a == FILE_GOOD;
            got = other_good ? read_file(pick_is_a ? SETTINGS_FILE_B : SETTINGS_FILE_A, true, &use_seq) : FILE_IOERR;
            active = got == FILE_GOOD ? (pick_is_a ? 1 : 0) : -1;
            /* A file that validated a moment ago but cannot be read now is a card fault, not "no settings": report it as an I/O error so the
             * store goes RAM-only and never writes defaults over what may be the only good copy (Wi-Fi secrets, Bluetooth keys). */
            res = SETTINGS_LOAD_IO_ERROR;
            /* The older file's contents are used so the device works, but the store stays RAM-only for this boot: the file that could not be
             * read may be the newer, intact one, and no save may touch either file. The next boot tries again. */
        }
    } else if (a == FILE_BAD || b == FILE_BAD) {
        res = SETTINGS_LOAD_CORRUPT;
    } else {
        res = SETTINGS_LOAD_FRESH;
    }

    lock();
    if (active < 0) {
        memset(g_items, 0, sizeof g_items);
    } else {
        g_seq = use_seq;
    }
    g_active = active;
    g_loaded = res != SETTINGS_LOAD_IO_ERROR || active >= 0; /* (IO_ERROR with items: loaded from the older file, read-only) */
    g_dirty = false;
    if (res == SETTINGS_LOAD_IO_ERROR) {
        g_attached = false; /* the card cannot even be read: RAM-only */
    }
    g_load_result = res;
    g_generation++;
    unlock();
    return res;
}

uint32_t settings_generation(void) { return g_generation; }

bool settings_dirty(void) {
    lock();
    bool d = g_dirty && g_attached;
    unlock();
    return d;
}

void settings_get_status(settings_status_t *out) {
    lock();
    out->persistent = g_attached && g_loaded;
    out->loaded = g_loaded;
    out->dirty = g_dirty;
    out->load = g_load_result;
    out->seq = g_seq;
    out->saves = g_saves;
    out->save_failures = g_fail_total;
    uint32_t n = 0;
    for (uint32_t i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        n += g_items[i].len != 0;
    }
    out->items = n;
    unlock();
}

/* ---- public: save ---- */

void settings_test_set_power_loss_step(int step) { g_crash_step = step; }
void settings_test_fail_next_reread(void) { g_test_fault = 1; }
void settings_test_ioerr_next_reread(void) { g_test_fault = 2; }

static bool power_lost_at(int step) { return g_crash_step == step; }

/* The save proper. Runs inside the write scope with the card unlocked. @p target = the file to overwrite (the inactive one). No rename and no
 * delete: the other file is never touched, so there is always one complete state on the card. */
static nfs_err_t save_image(const char *target, uint32_t n) {
    nfs_stat_t st;
    nfs_err_t e = nfs_stat(SETTINGS_DIR, &st);
    if (e == NFS_ERR_NOT_FOUND) {
        e = nfs_mkdir(SETTINGS_DIR);
    } else if (e == NFS_OK && !st.is_dir) {
        e = NFS_ERR_NOT_DIR;
    }
    if (e != NFS_OK) {
        return e;
    }
    for (unsigned i = 0; i < 2 && g_newer_mask != 0; i++) { /* a newer firmware's file is in the way of our rotation: keep it */
        if (g_newer_mask & (1u << i)) {
            nfs_stat_t os;
            if (nfs_stat(SETTINGS_FILE_OLD, &os) == NFS_OK) {
                (void)nfs_unlink(SETTINGS_FILE_OLD);
            }
            (void)nfs_rename(i == 0 ? SETTINGS_FILE_A : SETTINGS_FILE_B, SETTINGS_FILE_OLD);
        }
    }
    g_newer_mask = 0;
    nfs_file_t *f;
    e = nfs_open(&f, target, NFS_O_WRITE | NFS_O_CREATE | NFS_O_TRUNC);
    if (e != NFS_OK) {
        return e;
    }
    uint32_t half = n / 2u;
    size_t put = 0;
    e = nfs_write(f, g_image, half, &put);
    if (e == NFS_OK && power_lost_at(0)) {
        (void)nfs_close(f); /* a real power cut would not close; the half-written file is what matters */
        return NFS_ERR_IO;
    }
    if (e == NFS_OK) {
        e = nfs_write(f, g_image + half, n - half, &put);
    }
    if (e == NFS_OK) {
        e = nfs_sync(f);
    }
    nfs_err_t ce = nfs_close(f);
    if (e == NFS_OK) {
        e = ce;
    }
    if (e == NFS_OK && power_lost_at(1)) {
        return NFS_ERR_IO; /* complete and synced: only the caller's report is lost */
    }
    return e;
}

int settings_flush(void) {
    lock();
    bool want = g_dirty;
    bool ok = g_attached && g_loaded;
    uint32_t n = 0, seq = 0;
    int target = 0;
    if (want && ok) {
        seq = g_seq + 1u;
        n = serialise(seq);
        target = g_active == 0 ? 1 : 0; /* the inactive file (A when neither is valid) */
        g_dirty = false; /* a change made while we write sets it again */
    }
    unlock();
    if (!want) {
        return 0;
    }
    if (!ok) {
        return NFS_ERR_NOT_MOUNTED; /* RAM-only: keep the dirty flag so a later attach still saves */
    }

    /* The narrow unlock. Order matters: scope first (it can refuse), then the hardware lock; on the way out the
     * hardware lock closes first, then the scope. Every path below goes through the single exit. */
    nfs_err_t e = nfs_write_scope_enter(SETTINGS_DIR);
    if (e == NFS_OK) {
        bool was_enabled = hal_storage_get_write_enabled();
        hal_storage_set_write_enabled(true);
        e = save_image(target == 0 ? SETTINGS_FILE_A : SETTINGS_FILE_B, n);
        hal_storage_set_write_enabled(was_enabled);
        nfs_write_scope_leave();
    }

    lock();
    if (e == NFS_OK) {
        g_seq = seq;
        g_active = target;
        g_saves++;
        g_fail_streak = 0;
    } else if (e == NFS_ERR_BUSY) {
        g_dirty = true; /* someone else (the library scan) holds the write scope: nothing failed, try again next tick */
    } else {
        g_dirty = true;
        g_fail_total++;
        if (++g_fail_streak >= SETTINGS_MAX_SAVE_FAILURES && g_crash_step < 0) {
            g_attached = false; /* give up on the card: RAM-only until the next attach */
        }
    }
    unlock();
    return (int)e;
}

/* ---- public: values ---- */

static item_t *find(uint32_t tag) {
    for (uint32_t i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (g_items[i].len != 0 && g_items[i].tag == tag) {
            return &g_items[i];
        }
    }
    return NULL;
}

uint32_t settings_get(uint32_t tag, void *buf, uint32_t size) {
    lock();
    const item_t *it = find(tag);
    uint32_t len = 0;
    if (it != NULL) {
        len = it->len;
        if (buf != NULL) {
            memcpy(buf, it->val, len < size ? len : size);
        }
    }
    unlock();
    return len;
}

bool settings_put(uint32_t tag, const void *data, uint32_t len) {
    if (data == NULL || len == 0 || len > SETTINGS_VALUE_MAX) {
        return false;
    }
    lock();
    item_t *it = find(tag);
    bool ok = true;
    if (it == NULL) {
        for (uint32_t i = 0; i < SETTINGS_MAX_ITEMS && it == NULL; i++) {
            if (g_items[i].len == 0) {
                it = &g_items[i];
            }
        }
    }
    if (it == NULL) {
        ok = false; /* full */
    } else if (!(it->len == len && it->tag == tag && memcmp(it->val, data, len) == 0)) {
        it->tag = tag;
        it->len = (uint8_t)len;
        memcpy(it->val, data, len);
        g_dirty = true;
    }
    unlock();
    return ok;
}

void settings_delete(uint32_t tag) {
    lock();
    item_t *it = find(tag);
    if (it != NULL) {
        it->len = 0;
        g_dirty = true;
    }
    unlock();
}

bool settings_get_u8(uint32_t tag, uint8_t *out) {
    uint8_t v;
    if (settings_get(tag, &v, 1) != 1) {
        return false;
    }
    *out = v;
    return true;
}

void settings_set_u8(uint32_t tag, uint8_t v) { (void)settings_put(tag, &v, 1); }

/* ---- Secrets ---- */

bool settings_put_secret(uint32_t tag, const void *data, uint32_t len) {
    uint8_t rec[SETTINGS_VALUE_MAX], same[SETTINGS_VALUE_MAX];
    if (len == 0 || len + SECRET_OVERHEAD > sizeof rec) {
        return false;
    }
    bool ok;
    uint32_t cur = settings_get_secret(tag, same, sizeof same);
    if (cur == len && memcmp(same, data, len) == 0) {
        ok = true; /* unchanged: a fresh nonce would make the record differ and cost an SD write for nothing */
    } else {
        size_t n = secret_seal(tag, data, len, rec, sizeof rec);
        ok = n != 0 && settings_put(tag, rec, (uint32_t)n);
    }
    secret_wipe(same, sizeof same);
    secret_wipe(rec, sizeof rec);
    return ok;
}

/* Returns the bytes copied into @p buf (at most @p size), or the full plaintext length when @p buf is NULL; 0 if absent or not authentic. */
uint32_t settings_get_secret(uint32_t tag, void *buf, uint32_t size) {
    uint8_t rec[SETTINGS_VALUE_MAX], plain[SETTINGS_VALUE_MAX];
    uint32_t n = settings_get(tag, rec, sizeof rec);
    uint32_t r = 0;
    if (n != 0 && n <= sizeof rec) {
        int pl = secret_open(tag, rec, n, plain, sizeof plain);
        if (pl > 0) {
            r = (uint32_t)pl;
            if (buf != NULL) {
                if (r > size) {
                    r = size;
                }
                memcpy(buf, plain, r);
            }
        }
    }
    secret_wipe(plain, sizeof plain);
    secret_wipe(rec, sizeof rec);
    return r;
}

void settings_migrate_bt_secrets(void) {
    for (uint32_t i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        lock();
        uint32_t tag = g_items[i].tag;
        uint8_t len = g_items[i].len;
        uint8_t plain[SETTINGS_VALUE_MAX];
        bool todo = len != 0 && (tag >> 24) == 'B' && ((tag >> 16) & 0xFFu) == 'T' && len < SECRET_OVERHEAD;
        if (todo) {
            memcpy(plain, g_items[i].val, len);
        }
        unlock();
        if (todo) {
            (void)settings_put_secret(tag, plain, len); /* a sealed record is always >= SECRET_OVERHEAD + 1 bytes, so this runs once.
                                                         * Note: the old plaintext may remain in freed SD clusters (FAT does not scrub). */
            secret_wipe(plain, sizeof plain);
        }
    }
}

/* ---- BTstack storage adapter ---- */

static int bt_get(void *ctx, uint32_t tag, uint8_t *buf, uint32_t size) {
    (void)ctx;
    return (int)settings_get_secret(tag, buf, size); /* 0 when absent or not authentic: BTstack then pairs again; else bytes copied (length for NULL) */
}

/* Writes are refused while the card's contents are still to be loaded (or it is not known yet whether there is a
 * card): the load replaces the RAM image and would silently drop them. With no card at all (RAM-only) they work, for
 * the session. */
static bool writes_ok(void) {
    lock();
    bool ok = g_known && (!g_attached || g_loaded);
    unlock();
    return ok;
}

static int bt_put(void *ctx, uint32_t tag, const uint8_t *data, uint32_t size) {
    (void)ctx;
    return writes_ok() && settings_put_secret(tag, data, size) ? 0 : -1;
}

static void bt_del(void *ctx, uint32_t tag) {
    (void)ctx;
    if (writes_ok()) {
        settings_delete(tag);
    }
}

static const hal_bt_storage_t k_bt_storage = {.get = bt_get, .put = bt_put, .del = bt_del, .ctx = NULL};

const hal_bt_storage_t *settings_bt_storage(void) { return &k_bt_storage; }
