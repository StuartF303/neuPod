// SPDX-License-Identifier: MPL-2.0
// Library index on the card: file set description, CRC, the scratch arena, small FatFs helpers, the manifest (read, check, write)
// and boot recovery of the lib / lib.old / lib.tmp swap. See docs/adr/0017-music-library-index.md.
#include <stdio.h>
#include <string.h>

#include "hal_storage.h"
#include "lib_internal.h"
#include "lib_store.h"

const lib_file_info_t lib_files[LIB_FILES + 1] = {
    [LF_DIRS] = {"dirs.bin", LIB_REC_DIR, 0},
    [LF_NAMES] = {"names.str", 1, 0},
    [LF_TRACKS] = {"tracks.bin", LIB_REC_TRACK, 0},
    [LF_STRINGS] = {"strings.str", 1, 0},
    [LF_ARTISTS] = {"artists.bin", LIB_REC_ARTIST, 1},
    [LF_ALBUMS] = {"albums.bin", LIB_REC_ALBUM, 1},
    [LF_GENRES] = {"genres.bin", LIB_REC_GENRE, 1},
    [LF_BY_ALBUM] = {"by_album.u32", 4, 0},
    [LF_BY_TITLE] = {"by_title.u32", 4, 0},
    [LF_BY_ALBUM_NAME] = {"by_album_name.u32", 4, 0},
    [LF_GENRE_ARTISTS] = {"genre_artists.u32", 4, 0},
    [LF_ENAMES] = {"enames.str", 1, 0},
    [LF_MANIFEST] = {"manifest.bin", 1, 0},
};

const uint32_t lib_rates[12] = {0, 44100, 48000, 88200, 96000, 32000, 22050, 24000, 16000, 8000, 11025, 12000};

const char *lib_err_str(lib_err_t e) {
    switch (e) {
    case LIB_OK: return "ok";
    case LIB_E_IO: return "Card error";
    case LIB_E_FULL: return "Card is full";
    case LIB_E_NONE: return "No library yet";
    case LIB_E_FORMAT: return "Library damaged";
    case LIB_E_RANGE: return "Out of range";
    case LIB_E_SCRATCH: return "Not enough working memory";
    case LIB_E_CANCELLED: return "Cancelled";
#ifdef NEUPOD_LIB_TEST_HOOKS
    case LIB_E_TEST_STOP: return "Stopped (test)";
#endif
    }
    return "?";
}

uint32_t lib_magic(unsigned file) { return (uint32_t)'N' | ((uint32_t)'P' << 8) | ((uint32_t)'L' << 16) | ((uint32_t)('A' + file) << 24); }

uint32_t lib_crc32(uint32_t crc, const void *data, size_t n) {
    static const uint32_t t[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
                                   0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
    const uint8_t *p = data;
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ t[crc & 15u];
        crc = (crc >> 4) ^ t[crc & 15u];
    }
    return ~crc;
}

void *lib_arena_alloc(lib_arena_t *a, size_t n) {
    size_t at = (a->used + 7u) & ~(size_t)7u;
    if (n > a->cap || at > a->cap - n) return NULL;
    a->used = at + n;
    if (a->used > a->peak) a->peak = a->used;
    return a->base + at;
}

lib_err_t lib_map_fr(FRESULT fr) {
    switch (fr) {
    case FR_OK: return LIB_OK;
    case FR_DENIED: return LIB_E_FULL; // full volume or directory (the only FR_DENIED a writer meets)
    default: return LIB_E_IO;
    }
}

bool lib_exists(const char *path) {
    FILINFO fi;
    return f_stat(path, &fi) == FR_OK;
}

void lib_path(char *out, size_t cap, const char *base, const char *leaf) { snprintf(out, cap, "%s/%s", base, leaf); }

// A folder of plain files: take the first entry, close the folder, delete it, repeat (FatFs does not like deleting while a folder is open).
lib_err_t lib_rmtree(const char *dir) {
    if (!lib_exists(dir)) return LIB_OK;
    for (int guard = 0; guard < 100000; guard++) {
        DIR d;
        FILINFO fi;
        char path[96 + 260];
        FRESULT fr = f_opendir(&d, dir);
        if (fr != FR_OK) return LIB_E_IO;
        fr = f_readdir(&d, &fi);
        f_closedir(&d);
        if (fr != FR_OK) return LIB_E_IO;
        if (fi.fname[0] == '\0') break;
        snprintf(path, sizeof path, "%s/%s", dir, fi.fname);
        if (fi.fattrib & AM_DIR) {
            lib_err_t e = lib_rmtree(path);
            if (e != LIB_OK) return e;
        } else if (f_unlink(path) != FR_OK) {
            return LIB_E_IO;
        }
    }
    return f_unlink(dir) == FR_OK ? LIB_OK : LIB_E_IO;
}

/* ---- manifest ----------------------------------------------------------------------------------------------------- */

// payload: generation, decoder, fingerprint(2), tracks, playable, artists, albums, genres, dirs, comp_first, links, [crc,size]*11, jumps
#define MF_WORDS (2 + 2 + 8 + 2 * LIB_FILES + LIB_LISTS * LIB_BUCKETS)
#define MF_BYTES (MF_WORDS * 4u)

size_t lib_manifest_bytes(void) { return LIB_HDR + MF_BYTES; }

void lib_manifest_encode(const lib_manifest_t *m, uint8_t *out) {
    uint8_t *p = out + LIB_HDR;
    uint32_t w[MF_WORDS];
    size_t k = 0;
    w[k++] = m->generation;
    w[k++] = m->decoder_version;
    w[k++] = (uint32_t)m->fingerprint;
    w[k++] = (uint32_t)(m->fingerprint >> 32);
    w[k++] = m->tracks;
    w[k++] = m->playable;
    w[k++] = m->artists;
    w[k++] = m->albums;
    w[k++] = m->genres;
    w[k++] = m->dirs;
    w[k++] = m->comp_first;
    w[k++] = m->genre_artist_links;
    for (unsigned i = 0; i < LIB_FILES; i++) {
        w[k++] = m->file_crc[i];
        w[k++] = m->file_size[i];
    }
    for (unsigned l = 0; l < LIB_LISTS; l++) {
        for (unsigned b = 0; b < LIB_BUCKETS; b++) w[k++] = m->jump[l][b];
    }
    for (size_t i = 0; i < MF_WORDS; i++) st32(p + 4 * i, w[i]);
    st32(out, lib_magic(LF_MANIFEST));
    st32(out + 4, LIB_VERSION);
    st32(out + 8, 1);
    st32(out + 12, lib_crc32(0, p, MF_BYTES));
}

// A manifest whose CRC is right can still come from a bug or a hostile card: nothing in it may point outside what it describes. Every
// count must agree with the size of the file that holds it, and the reader relies on that (it never reads past a table it has counted).
static uint32_t list_rows(const lib_manifest_t *m, unsigned l) {
    switch (l) {
    case LIB_LIST_ARTISTS: return m->artists;
    case LIB_LIST_ALBUMS: return m->albums;
    case LIB_LIST_SONGS: return m->tracks;
    case LIB_LIST_GENRES: return m->genres;
    default: return m->albums - m->comp_first;
    }
}

static bool manifest_consistent(const lib_manifest_t *m) {
    if (m->playable > m->tracks || m->comp_first > m->albums) return false;
    static const int8_t kind[LIB_FILES] = {[LF_DIRS] = 1, [LF_TRACKS] = 2, [LF_ARTISTS] = 3, [LF_ALBUMS] = 4, [LF_GENRES] = 5, [LF_BY_ALBUM] = 2,
                                           [LF_BY_TITLE] = 2, [LF_BY_ALBUM_NAME] = 4, [LF_GENRE_ARTISTS] = 6}; // which count sizes each file; 0 = a pool
    for (unsigned i = 0; i < LIB_FILES; i++) {
        uint64_t n;
        switch (kind[i]) {
        case 1: n = m->dirs; break;
        case 2: n = m->tracks; break;
        case 3: n = m->artists; break;
        case 4: n = m->albums; break;
        case 5: n = m->genres; break;
        case 6: n = m->genre_artist_links; break;
        default: if (m->file_size[i] < LIB_HDR) return false; continue;
        }
        if ((uint64_t)m->file_size[i] != LIB_HDR + (uint64_t)lib_files[i].rec * (n + lib_files[i].sentinel)) return false;
    }
    for (unsigned l = 0; l < LIB_LISTS; l++) {
        uint32_t rows = list_rows(m, l), prev = 0;
        for (unsigned b = 0; b < LIB_BUCKETS; b++) {
            if (m->jump[l][b] > rows || m->jump[l][b] < prev) return false;
            prev = m->jump[l][b];
        }
    }
    return true;
}

static bool manifest_decode(const uint8_t *in, size_t n, lib_manifest_t *m) {
    if (n != LIB_HDR + MF_BYTES || ld32(in) != lib_magic(LF_MANIFEST) || ld32(in + 4) != LIB_VERSION || ld32(in + 8) != 1) return false;
    const uint8_t *p = in + LIB_HDR;
    if (lib_crc32(0, p, MF_BYTES) != ld32(in + 12)) return false;
    uint32_t w[MF_WORDS];
    for (size_t i = 0; i < MF_WORDS; i++) w[i] = ld32(p + 4 * i);
    size_t k = 0;
    memset(m, 0, sizeof *m);
    m->generation = w[k++];
    m->decoder_version = w[k++];
    m->fingerprint = w[k] | ((uint64_t)w[k + 1] << 32);
    k += 2;
    m->tracks = w[k++];
    m->playable = w[k++];
    m->artists = w[k++];
    m->albums = w[k++];
    m->genres = w[k++];
    m->dirs = w[k++];
    m->comp_first = w[k++];
    m->genre_artist_links = w[k++];
    for (unsigned i = 0; i < LIB_FILES; i++) {
        m->file_crc[i] = w[k++];
        m->file_size[i] = w[k++];
    }
    for (unsigned l = 0; l < LIB_LISTS; l++) {
        for (unsigned b = 0; b < LIB_BUCKETS; b++) m->jump[l][b] = w[k++];
    }
    return manifest_consistent(m);
}

lib_err_t lib_manifest_load(const char *dir, bool full, lib_manifest_t *out) {
    char path[96];
    uint8_t buf[LIB_HDR + MF_BYTES + 1];
    FIL f;
    UINT got = 0;
    snprintf(path, sizeof path, "%s/%s", dir, lib_files[LF_MANIFEST].name);
    FRESULT fr = f_open(&f, path, FA_READ);
    if (fr == FR_NO_FILE || fr == FR_NO_PATH) return LIB_E_NONE;
    if (fr != FR_OK) return LIB_E_IO;
    fr = f_read(&f, buf, sizeof buf, &got);
    f_close(&f);
    if (fr != FR_OK) return LIB_E_IO;
    lib_manifest_t m;
    if (!manifest_decode(buf, got, &m)) return LIB_E_FORMAT;
    for (unsigned i = 0; i < LIB_FILES; i++) {
        FILINFO fi;
        snprintf(path, sizeof path, "%s/%s", dir, lib_files[i].name);
        if (f_stat(path, &fi) != FR_OK || fi.fsize != m.file_size[i]) return LIB_E_FORMAT;
        if (!full) continue;
        // header (magic, version, count) and the CRC of the payload
        uint8_t h[LIB_HDR], blk[512];
        if (f_open(&f, path, FA_READ) != FR_OK) return LIB_E_IO;
        fr = f_read(&f, h, LIB_HDR, &got);
        if (fr == FR_OK && got != LIB_HDR) {
            f_close(&f);
            return LIB_E_FORMAT;
        }
        uint32_t crc = 0;
        while (fr == FR_OK) {
            fr = f_read(&f, blk, sizeof blk, &got);
            if (fr != FR_OK || got == 0) break;
            crc = lib_crc32(crc, blk, got);
        }
        f_close(&f);
        if (fr != FR_OK) return LIB_E_IO;
        if (ld32(h) != lib_magic(i) || ld32(h + 4) != LIB_VERSION || ld32(h + 12) != crc || crc != m.file_crc[i]) return LIB_E_FORMAT;
    }
    if (out != NULL) *out = m;
    return LIB_OK;
}

/* ---- swap and recovery -------------------------------------------------------------------------------------------- */

#ifdef NEUPOD_LIB_TEST_HOOKS
int lib_test_fail_rename; // fail the Nth rename made from now on (1 = the next one); 0 = never
static FRESULT lib_rename(const char *from, const char *to) {
    if (lib_test_fail_rename > 0 && --lib_test_fail_rename == 0) return FR_DISK_ERR;
    return f_rename(from, to);
}
#else
#define lib_rename f_rename
#endif

lib_err_t lib_swap_install(const char *base, int stop_step, int *step_done) {
    char lib[64], old[64], tmp[64];
    lib_path(lib, sizeof lib, base, "lib");
    lib_path(old, sizeof old, base, "lib.old");
    lib_path(tmp, sizeof tmp, base, "lib.tmp");
    lib_err_t e = lib_rmtree(old); // a leftover from a build that was cut after the swap
    if (e != LIB_OK) return e;
    bool moved = false;
    if (lib_exists(lib)) {
        if (lib_rename(lib, old) != FR_OK) return LIB_E_IO; // nothing has changed
        moved = true;
        *step_done = 3;
        if (LIB_STOP_AT(stop_step, 3)) return LIB_E_TEST_STOP_;
    }
    if (lib_rename(tmp, lib) != FR_OK) {
        if (moved) f_rename(old, lib); // put the old library back: the card must not be left without one (lib_recover would too)
        return LIB_E_IO;
    }
    *step_done = 4;
    if (LIB_STOP_AT(stop_step, 4)) return LIB_E_TEST_STOP_;
    lib_rmtree(old); // the new library is live: a failure here only leaves a stray folder, which the next build or lib_recover() removes
    *step_done = 5;
    if (LIB_STOP_AT(stop_step, 5)) return LIB_E_TEST_STOP_;
    return LIB_OK;
}

lib_err_t lib_recover(const char *base) {
    if (base == NULL) base = LIB_BASE_DEFAULT;
    char lib[64], old[64], tmp[64], work[64];
    lib_path(lib, sizeof lib, base, "lib");
    lib_path(old, sizeof old, base, "lib.old");
    lib_path(tmp, sizeof tmp, base, "lib.tmp");
    lib_path(work, sizeof work, base, "work");
    bool was = hal_storage_get_write_enabled();
    hal_storage_set_write_enabled(true);
    lib_err_t res;
    if (lib_manifest_load(lib, false, NULL) == LIB_OK) {
        res = LIB_OK;
    } else if (lib_manifest_load(old, false, NULL) == LIB_OK) { // the swap was cut after lib moved aside: put the old library back
        res = lib_rmtree(lib);
        if (res == LIB_OK && lib_rename(old, lib) != FR_OK) res = LIB_E_IO;
    } else if (lib_manifest_load(tmp, false, NULL) == LIB_OK) { // first build, cut just before the final rename: the new one is complete
        res = lib_rmtree(lib);
        if (res == LIB_OK && lib_rename(tmp, lib) != FR_OK) res = LIB_E_IO;
    } else {
        res = LIB_E_NONE;
        lib_rmtree(tmp); // an unfinished build is of no use
        lib_rmtree(work);
    }
    if (res == LIB_OK) { // leftovers are no longer needed
        lib_rmtree(old);
        lib_rmtree(tmp);
        lib_rmtree(work);
    }
    hal_storage_set_write_enabled(was);
    return res;
}
