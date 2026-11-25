// SPDX-License-Identifier: MPL-2.0
// Path of a queue entry without a lib_reader_t (library.h, lib_resolve): the storage task resolves what the player asks for while the
// browse reader is closed (Files overlays its memory) or reading another list. Four small files, one open at a time.
#include <stdio.h>
#include <string.h>

#include "lib_internal.h"
#include "library.h"

// Read @p n bytes at payload offset @p off of index file @p lf (@p dir = "<base>/lib"). @p fp (may be NULL) keeps the file open for the
// next call; pass the same FIL and the file stays open until lres_close.
typedef struct {
    FIL f;
    bool open;
    uint32_t payload;
} lres_t;

static lib_err_t lres_open(lres_t *l, const char *dir, unsigned lf) {
    char p[96];
    snprintf(p, sizeof p, "%s/%s", dir, lib_files[lf].name);
    FRESULT fr = f_open(&l->f, p, FA_READ);
    if (fr == FR_NO_FILE || fr == FR_NO_PATH) return LIB_E_NONE;
    if (fr != FR_OK) return LIB_E_IO;
    if (f_size(&l->f) < LIB_HDR) {
        f_close(&l->f);
        return LIB_E_FORMAT;
    }
    l->open = true;
    l->payload = (uint32_t)f_size(&l->f) - LIB_HDR;
    return LIB_OK;
}

static void lres_close(lres_t *l) {
    if (l->open) f_close(&l->f);
    l->open = false;
}

static lib_err_t lres_read(lres_t *l, uint32_t off, void *dst, uint32_t n, uint32_t *got_out) {
    if (off >= l->payload) return LIB_E_RANGE;
    if ((uint64_t)off + n > l->payload) n = l->payload - off; // a string near the end of a pool: take what is there
    UINT got = 0;
    if (f_lseek(&l->f, LIB_HDR + (FSIZE_t)off) != FR_OK || f_read(&l->f, dst, n, &got) != FR_OK) return LIB_E_IO;
    if (got_out != NULL) *got_out = got;
    return LIB_OK;
}

lib_err_t lib_resolve_ids(const char *base, lib_order_t order, uint32_t pos, char *path, size_t cap, uint8_t *play, uint32_t *album, uint32_t *album_artist) {
    if (base == NULL) base = LIB_BASE_DEFAULT;
    if (strlen(base) > LIB_BASE_MAX || cap < 2 || (unsigned)order >= LIB_ORDERS) return LIB_E_RANGE;
    char dir[64];
    snprintf(dir, sizeof dir, "%s/lib", base);
    static const uint8_t k_order_file[LIB_ORDERS] = {LF_BY_TITLE, LF_BY_ALBUM, LF_BY_GENRE};
    lres_t l = {.open = false};
    uint8_t w[LIB_REC_TRACK];
    uint32_t got = 0;
    lib_err_t e = lres_open(&l, dir, k_order_file[order]);
    if (e == LIB_OK) e = lres_read(&l, pos * 4u, w, 4, &got);
    if (e == LIB_OK && got != 4) e = LIB_E_FORMAT;
    lres_close(&l);
    if (e != LIB_OK) return e;
    uint32_t id = ld32(w);
    e = lres_open(&l, dir, LF_TRACKS);
    if (e == LIB_OK) e = lres_read(&l, id * LIB_REC_TRACK, w, LIB_REC_TRACK, &got);
    if (e == LIB_OK && got != LIB_REC_TRACK) e = LIB_E_FORMAT;
    lres_close(&l);
    if (e != LIB_OK) return e;
    if (play != NULL) *play = w[TR_PLAY];
    if (album != NULL) *album = ld32(w + TR_ALBUM);
    if (album_artist != NULL) *album_artist = ld32(w + TR_AARTIST);
    uint32_t names[17], d = ld32(w + TR_DIR);
    unsigned n = 0;
    names[16] = ld32(w + TR_NAME);
    if (d != LIB_NONE) {
        e = lres_open(&l, dir, LF_DIRS);
        while (e == LIB_OK && d != LIB_NONE && n < 16) { // folders up to the root (its name is the whole music-folder path)
            uint8_t rec[LIB_REC_DIR];
            e = lres_read(&l, d * LIB_REC_DIR, rec, sizeof rec, &got);
            if (e == LIB_OK && got != sizeof rec) e = LIB_E_FORMAT;
            if (e != LIB_OK) break;
            names[n++] = ld32(rec + 4);
            d = ld32(rec);
        }
        lres_close(&l);
        if (e == LIB_OK && d != LIB_NONE) e = LIB_E_FORMAT;
        if (e != LIB_OK) return e;
    }
    e = lres_open(&l, dir, LF_NAMES);
    size_t at = 0;
    for (unsigned k = 0; e == LIB_OK && k <= n; k++) { // root first, the file name last
        char part[256];
        uint32_t off = k < n ? names[n - 1 - k] : names[16];
        e = lres_read(&l, off, part, sizeof part - 1, &got);
        if (e != LIB_OK) break;
        part[got] = '\0';
        size_t pl = strlen(part);
        bool sep = at > 0 && !(at == 1 && path[0] == '/');
        if (at + pl + (sep ? 1u : 0u) + 1 > cap) {
            e = LIB_E_RANGE;
            break;
        }
        if (sep) path[at++] = '/';
        memcpy(path + at, part, pl);
        at += pl;
    }
    lres_close(&l);
    if (e == LIB_OK) path[at] = '\0';
    return e;
}

lib_err_t lib_resolve(const char *base, lib_order_t order, uint32_t pos, char *path, size_t cap, uint8_t *play) {
    return lib_resolve_ids(base, order, pos, path, cap, play, NULL, NULL);
}
