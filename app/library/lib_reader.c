// SPDX-License-Identifier: MPL-2.0
// Library reader (docs/adr/0017-music-library-index.md, "Reader"): one FIL per open table (fast seek where the link map fits) and a
// small LRU cache of 2 KB pages shared by all of them. Everything lives in the caller's lib_reader_t: no heap.
#include <stdio.h>
#include <string.h>

#include "lib_internal.h"
#include "library.h"

enum { T_TRACKS, T_STRINGS, T_ARTISTS, T_ALBUMS, T_GENRES, T_BY_ALBUM, T_BY_TITLE, T_BY_ALBUM_NAME, T_GENRE_ARTISTS, T_ENAMES, T_PERSISTENT, T_DIRS = T_PERSISTENT, T_NAMES, T_BY_GENRE };

static const uint8_t k_file[T_PERSISTENT] = {LF_TRACKS, LF_STRINGS, LF_ARTISTS, LF_ALBUMS, LF_GENRES, LF_BY_ALBUM, LF_BY_TITLE, LF_BY_ALBUM_NAME, LF_GENRE_ARTISTS, LF_ENAMES};

static lib_err_t table_open(lib_table_t *t, const char *dir, unsigned file) {
    char path[96];
    snprintf(path, sizeof path, "%s/%s", dir, lib_files[file].name);
    if (f_open(&t->f, path, FA_READ) != FR_OK) return LIB_E_IO;
    if (f_size(&t->f) < LIB_HDR) {
        f_close(&t->f);
        return LIB_E_FORMAT;
    }
    t->open = true;
    t->payload = (uint32_t)f_size(&t->f) - LIB_HDR;
    t->fast = false;
#if FF_USE_FASTSEEK
    t->f.cltbl = t->clmt;
    t->clmt[0] = (DWORD)(sizeof t->clmt / sizeof t->clmt[0]);
    if (f_lseek(&t->f, CREATE_LINKMAP) == FR_OK) t->fast = true;
    else t->f.cltbl = NULL; // fragmented beyond the table (FR_NOT_ENOUGH_CORE): plain seeks walk the FAT chain
#endif
    return LIB_OK;
}

static void table_close(lib_table_t *t) {
    if (t->open) f_close(&t->f);
    t->open = false;
}

lib_err_t lib_open(lib_reader_t *r, const char *base) {
    memset(r, 0, sizeof *r);
    r->aux_which = -1;
    if (base == NULL) base = LIB_BASE_DEFAULT;
    if (strlen(base) > LIB_BASE_MAX) return LIB_E_RANGE;
    snprintf(r->dir, sizeof r->dir, "%s/lib", base);
    lib_err_t e = lib_manifest_load(r->dir, false, &r->m);
    if (e != LIB_OK) return e;
    for (unsigned i = 0; i < T_PERSISTENT && e == LIB_OK; i++) e = table_open(&r->t[i], r->dir, k_file[i]);
    if (e != LIB_OK) lib_close(r);
    return e;
}

void lib_close(lib_reader_t *r) {
    for (unsigned i = 0; i < T_PERSISTENT; i++) table_close(&r->t[i]);
    table_close(&r->aux);
    r->aux_which = -1;
    for (unsigned i = 0; i < LIB_PAGES; i++) r->page[i].valid = false;
}

const lib_manifest_t *lib_manifest(const lib_reader_t *r) { return &r->m; }

static lib_table_t *table(lib_reader_t *r, unsigned id) {
    if (id < T_PERSISTENT) return &r->t[id];
    if (r->aux_which != (int)id) {
        table_close(&r->aux);
        r->aux_which = -1;
        if (table_open(&r->aux, r->dir, id == T_DIRS ? LF_DIRS : id == T_NAMES ? LF_NAMES : LF_BY_GENRE) != LIB_OK) return NULL;
        r->aux_which = (int)id;
    }
    return &r->aux;
}

// The page holding payload byte `off` of table `id`; valid until the next call.
static const lib_page_t *get_page(lib_reader_t *r, unsigned id, uint32_t off) {
    uint32_t pg = off / LIB_PAGE_BYTES;
    lib_page_t *victim = &r->page[0];
    r->clock++;
    for (unsigned i = 0; i < LIB_PAGES; i++) {
        lib_page_t *p = &r->page[i];
        if (p->valid && p->table == id && p->page == pg) {
            p->stamp = r->clock;
            r->page_hits++;
            return p;
        }
        if (!p->valid || (victim->valid && p->stamp < victim->stamp)) victim = p;
    }
    lib_table_t *t = table(r, id);
    if (t == NULL) return NULL;
    UINT got = 0;
    victim->valid = false;
    if (f_lseek(&t->f, LIB_HDR + (FSIZE_t)pg * LIB_PAGE_BYTES) != FR_OK || f_read(&t->f, victim->data, LIB_PAGE_BYTES, &got) != FR_OK) return NULL;
    victim->table = id;
    victim->page = pg;
    victim->len = got;
    victim->stamp = r->clock;
    victim->valid = true;
    r->page_misses++;
    return victim;
}

static lib_err_t read_bytes(lib_reader_t *r, unsigned id, uint32_t off, void *dst, uint32_t n) {
    lib_table_t *t = table(r, id);
    if (t == NULL) return LIB_E_IO;
    if ((uint64_t)off + n > t->payload) return LIB_E_RANGE;
    uint8_t *d = dst;
    while (n > 0) {
        const lib_page_t *p = get_page(r, id, off);
        if (p == NULL) return LIB_E_IO;
        uint32_t in = off % LIB_PAGE_BYTES, k = LIB_PAGE_BYTES - in;
        if (k > n) k = n;
        if (in + k > p->len) return LIB_E_FORMAT;
        memcpy(d, p->data + in, k);
        d += k;
        off += k;
        n -= k;
    }
    return LIB_OK;
}

// NUL-terminated string at payload offset `off`, cut to cap-1 bytes.
static lib_err_t read_str(lib_reader_t *r, unsigned id, uint32_t off, char *dst, size_t cap) {
    lib_table_t *t = table(r, id);
    if (t == NULL) return LIB_E_IO;
    if (off >= t->payload) return LIB_E_RANGE;
    size_t n = 0;
    for (;;) {
        const lib_page_t *p = get_page(r, id, off);
        if (p == NULL) return LIB_E_IO;
        uint32_t in = off % LIB_PAGE_BYTES;
        if (in >= p->len) break;
        for (; in < p->len && off < t->payload; in++, off++) {
            char ch = (char)p->data[in];
            if (ch == '\0') goto done;
            if (n + 1 < cap) dst[n++] = ch;
        }
        if (off >= t->payload) break;
    }
done:
    dst[n] = '\0';
    return LIB_OK;
}

// tbl: T_STRINGS for a track's title / artist, T_ENAMES for the name of an artist, album or genre (those are stored together in sorted
// order, so the names of a screenful of rows share a page)
static lib_err_t name_at(lib_reader_t *r, unsigned tbl, uint32_t off, char *dst) {
    if (off == LIB_NONE) {
        dst[0] = '\0';
        return LIB_OK;
    }
    return read_str(r, tbl, off, dst, LIB_TEXT_MAX);
}

/* ---- records -------------------------------------------------------------------------------------------------------- */

lib_err_t lib_track(lib_reader_t *r, uint32_t id, lib_track_t *out) {
    if (id >= r->m.tracks) return LIB_E_RANGE;
    uint8_t b[LIB_REC_TRACK];
    lib_err_t e = read_bytes(r, T_TRACKS, id * LIB_REC_TRACK, b, sizeof b);
    if (e != LIB_OK) return e;
    memset(out, 0, sizeof *out);
    out->id = id;
    out->dir = ld32(b + TR_DIR);
    out->size = ld32(b + TR_SIZE);
    out->mtime = ld32(b + TR_MTIME);
    out->duration_s = ld32(b + TR_DUR);
    out->rate = b[TR_RATE] < 12 ? lib_rates[b[TR_RATE]] : 0;
    out->genre = ld16(b + TR_GENRE);
    out->year = ld16(b + TR_YEAR);
    out->track = ld16(b + TR_TRACK);
    out->disc = b[TR_DISC];
    out->compilation = b[TR_FLAGS] & 1;
    out->codec = b[TR_CODEC];
    out->bits = b[TR_BITS];
    out->play = b[TR_PLAY];
    out->artist = ld32(b + TR_AARTIST);
    out->album = ld32(b + TR_ALBUM);
    e = name_at(r, T_STRINGS, ld32(b + TR_TITLE), out->title);
    if (e == LIB_OK) e = name_at(r, T_STRINGS, ld32(b + TR_ARTIST), out->artist_name);
    return e;
}

// artists, albums, genres: name, key8, first [, album artist]; the row after it (the sentinel for the last) gives the child count
static lib_err_t entity(lib_reader_t *r, unsigned tbl, uint32_t rs, uint32_t n, uint32_t id, lib_entity_t *out) {
    if (id >= n) return LIB_E_RANGE;
    uint8_t b[2 * LIB_REC_ALBUM];
    lib_err_t e = read_bytes(r, tbl, id * rs, b, 2 * rs);
    if (e != LIB_OK) return e;
    uint32_t first = ld32(b + 12), next = ld32(b + rs + 12);
    out->first = first;
    out->count = next >= first ? next - first : 0;
    out->artist = LIB_NONE;
    out->tfirst = out->tcount = 0;
    if (tbl == T_ALBUMS) {
        out->artist = ld32(b + 16);
    } else if (tbl == T_GENRES) { // 20 B rows: the genre's tracks in by_genre start at +16
        uint32_t tf = ld32(b + 16), tn = ld32(b + rs + 16);
        out->tfirst = tf;
        out->tcount = tn >= tf ? tn - tf : 0;
    }
    return name_at(r, T_ENAMES, ld32(b), out->name);
}

lib_err_t lib_artist(lib_reader_t *r, uint32_t id, lib_entity_t *out) { return entity(r, T_ARTISTS, LIB_REC_ARTIST, r->m.artists, id, out); }
lib_err_t lib_album(lib_reader_t *r, uint32_t id, lib_entity_t *out) { return entity(r, T_ALBUMS, LIB_REC_ALBUM, r->m.albums, id, out); }
lib_err_t lib_genre(lib_reader_t *r, uint32_t id, lib_entity_t *out) { return entity(r, T_GENRES, LIB_REC_GENRE, r->m.genres, id, out); }

static lib_err_t u32_at(lib_reader_t *r, unsigned tbl, uint32_t n, uint32_t pos, uint32_t *v) {
    if (pos >= n) return LIB_E_RANGE;
    uint8_t b[4];
    lib_err_t e = read_bytes(r, tbl, pos * 4, b, 4);
    if (e == LIB_OK) *v = ld32(b);
    return e;
}

lib_err_t lib_by_album_at(lib_reader_t *r, uint32_t pos, uint32_t *track_id) { return u32_at(r, T_BY_ALBUM, r->m.tracks, pos, track_id); }
lib_err_t lib_by_genre_at(lib_reader_t *r, uint32_t pos, uint32_t *track_id) { return u32_at(r, T_BY_GENRE, r->m.genre_tracks, pos, track_id); }
lib_err_t lib_genre_artist_at(lib_reader_t *r, uint32_t pos, uint32_t *artist_id) {
    return u32_at(r, T_GENRE_ARTISTS, r->m.genre_artist_links, pos, artist_id);
}

/* ---- lists ---------------------------------------------------------------------------------------------------------- */

uint32_t lib_list_count(const lib_reader_t *r, lib_list_t l) {
    switch (l) {
    case LIB_LIST_ARTISTS: return r->m.artists;
    case LIB_LIST_ALBUMS: return r->m.albums;
    case LIB_LIST_SONGS: return r->m.tracks;
    case LIB_LIST_GENRES: return r->m.genres;
    case LIB_LIST_COMPILATIONS: return r->m.albums > r->m.comp_first ? r->m.albums - r->m.comp_first : 0;
    default: return 0;
    }
}

lib_err_t lib_list_id(lib_reader_t *r, lib_list_t l, uint32_t i, uint32_t *id) {
    if (i >= lib_list_count(r, l)) return LIB_E_RANGE;
    switch (l) {
    case LIB_LIST_ARTISTS:
    case LIB_LIST_GENRES: *id = i; return LIB_OK;
    case LIB_LIST_ALBUMS: return u32_at(r, T_BY_ALBUM_NAME, r->m.albums, i, id);
    case LIB_LIST_SONGS: return u32_at(r, T_BY_TITLE, r->m.tracks, i, id);
    case LIB_LIST_COMPILATIONS: *id = r->m.comp_first + i; return LIB_OK;
    default: return LIB_E_RANGE;
    }
}

lib_err_t lib_list_row(lib_reader_t *r, lib_list_t l, uint32_t i, lib_row_t *out) {
    uint32_t id;
    lib_err_t e = lib_list_id(r, l, i, &id);
    if (e != LIB_OK) return e;
    memset(out, 0, sizeof *out);
    out->id = id;
    if (l == LIB_LIST_SONGS) {
        lib_track_t t;
        e = lib_track(r, id, &t);
        if (e != LIB_OK) return e;
        memcpy(out->text, t.title, sizeof out->text);
        memcpy(out->sub, t.artist_name, sizeof out->sub);
        out->play = t.play;
        return LIB_OK;
    }
    lib_entity_t en;
    e = l == LIB_LIST_ARTISTS ? lib_artist(r, id, &en) : l == LIB_LIST_GENRES ? lib_genre(r, id, &en) : lib_album(r, id, &en);
    if (e != LIB_OK) return e;
    memcpy(out->text, en.name, sizeof out->text);
    out->count = en.count;
    if ((l == LIB_LIST_ALBUMS || l == LIB_LIST_COMPILATIONS) && en.artist != LIB_NONE) {
        lib_entity_t ar;
        e = lib_artist(r, en.artist, &ar);
        if (e == LIB_OK) memcpy(out->sub, ar.name, sizeof out->sub);
    }
    return e;
}

uint32_t lib_list_jump(const lib_reader_t *r, lib_list_t l, char ch) {
    unsigned b = 0;
    if (ch >= 'a' && ch <= 'z') b = 1u + (unsigned)(ch - 'a');
    else if (ch >= 'A' && ch <= 'Z') b = 1u + (unsigned)(ch - 'A');
    return r->m.jump[l][b];
}

/* ---- paths ---------------------------------------------------------------------------------------------------------- */

lib_err_t lib_track_path(lib_reader_t *r, uint32_t id, char *out, size_t cap) {
    if (id >= r->m.tracks || cap == 0) return LIB_E_RANGE;
    uint8_t b[LIB_REC_TRACK];
    lib_err_t e = read_bytes(r, T_TRACKS, id * LIB_REC_TRACK, b, sizeof b);
    if (e != LIB_OK) return e;
    uint32_t names[17];
    unsigned n = 0;
    uint32_t d = ld32(b + TR_DIR);
    while (d != LIB_NONE && n < 16) { // folders up to the root (the root's own name is the full music-folder path)
        uint8_t rec[LIB_REC_DIR];
        if (d >= r->m.dirs) return LIB_E_FORMAT;
        e = read_bytes(r, T_DIRS, d * LIB_REC_DIR, rec, sizeof rec);
        if (e != LIB_OK) return e;
        names[n] = ld32(rec + 4);
        n++;
        d = ld32(rec);
    }
    if (d != LIB_NONE) return LIB_E_FORMAT;
    names[n] = ld32(b + TR_NAME);
    size_t at = 0;
    for (unsigned k = 0; k <= n; k++) { // root first, the file name last
        char part[256];
        e = read_str(r, T_NAMES, names[k < n ? n - 1 - k : n], part, sizeof part);
        if (e != LIB_OK) return e;
        size_t pl = strlen(part);
        size_t need = pl + (at > 0 && !(at == 1 && out[0] == '/') ? 1 : 0);
        if (at + need + 1 > cap) return LIB_E_RANGE;
        if (at > 0 && !(at == 1 && out[0] == '/')) out[at++] = '/';
        memcpy(out + at, part, pl);
        at += pl;
    }
    out[at] = '\0';
    return LIB_OK;
}
