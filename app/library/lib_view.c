// SPDX-License-Identifier: MPL-2.0
// The lists of the Music screens over the library reader (library_view.h).
#include <stdio.h>
#include <string.h>

#include "library_view.h"

void lvw_copy(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) n--; // src[n] continues a character: drop the partial one
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static lib_err_t with_title(lib_err_t e, lvw_info_t *out, const char *name) {
    if (e == LIB_OK) lvw_copy(out->title, sizeof out->title, name);
    return e;
}

#define HEADER_SONGS_MAX 100u /* an album with more songs than this has its length and format summed over the first ones only */

// The album header's facts: the album artist, the first year found, the summed length and the common codec. Best effort: a read error leaves
// what was found so far (the header is decoration; the list itself must still open).
static void album_header(lib_reader_t *r, const lib_entity_t *en, lvw_info_t *out) {
    if (en->artist != LIB_NONE) {
        lib_entity_t ar;
        if (lib_artist(r, en->artist, &ar) == LIB_OK) lvw_copy(out->artist, sizeof out->artist, ar.name);
    } else {
        lvw_copy(out->artist, sizeof out->artist, "Various Artists");
    }
    bool first = true;
    for (uint32_t i = 0; i < en->count && i < HEADER_SONGS_MAX; i++) {
        uint32_t id;
        lib_track_t t;
        if (lib_by_album_at(r, en->first + i, &id) != LIB_OK || lib_track(r, id, &t) != LIB_OK) break;
        out->total_s += t.duration_s;
        if (out->year == 0) out->year = t.year;
        if (first) {
            out->codec = t.codec;
            first = false;
        } else if (out->codec != t.codec) {
            out->codec = LIB_CODEC_NONE;
        }
    }
}

lib_err_t lvw_info(lib_reader_t *r, lvw_view_t v, lvw_info_t *out) {
    memset(out, 0, sizeof *out);
    lib_entity_t en;
    lib_err_t e;
    switch (v.kind) {
    case LVW_ARTISTS: out->count = lib_list_count(r, LIB_LIST_ARTISTS); lvw_copy(out->title, sizeof out->title, "Artists"); return LIB_OK;
    case LVW_ALBUMS: out->count = lib_list_count(r, LIB_LIST_ALBUMS); lvw_copy(out->title, sizeof out->title, "Albums"); return LIB_OK;
    case LVW_GENRES: out->count = lib_list_count(r, LIB_LIST_GENRES); lvw_copy(out->title, sizeof out->title, "Genres"); return LIB_OK;
    case LVW_COMPILATIONS: out->count = lib_list_count(r, LIB_LIST_COMPILATIONS); lvw_copy(out->title, sizeof out->title, "Compilations"); return LIB_OK;
    case LVW_SONGS:
        out->count = lib_list_count(r, LIB_LIST_SONGS);
        out->songs = true;
        out->range = (pq_range_t){.order = LIB_ORDER_TITLE, .first = 0, .count = out->count};
        lvw_copy(out->title, sizeof out->title, "Songs");
        return LIB_OK;
    case LVW_ARTIST:
        e = lib_artist(r, v.id, &en);
        if (e != LIB_OK) return e;
        out->head = 1;
        out->count = 1 + en.count;
        return with_title(e, out, en.name);
    case LVW_ALBUM:
        e = lib_album(r, v.id, &en);
        if (e != LIB_OK) return e;
        out->count = en.count;
        out->songs = true;
        out->range = (pq_range_t){.order = LIB_ORDER_ALBUM, .first = en.first, .count = en.count};
        album_header(r, &en, out);
        return with_title(e, out, en.name);
    case LVW_ARTIST_SONGS: { // the artist's albums are contiguous in the by-album order: from its first album's start to the next album's start
        e = lib_artist(r, v.id, &en);
        if (e != LIB_OK) return e;
        lib_entity_t a0, a1;
        out->songs = true;
        out->range.order = LIB_ORDER_ALBUM;
        if (en.count > 0) {
            e = lib_album(r, en.first, &a0);
            if (e == LIB_OK) e = lib_album(r, en.first + en.count, &a1); // the sentinel row after the last album of all
            if (e != LIB_OK) return e;
            out->range.first = a0.first;
            out->range.count = a1.first >= a0.first ? a1.first - a0.first : 0;
        }
        out->count = out->range.count;
        return with_title(e, out, en.name);
    }
    case LVW_GENRE:
        e = lib_genre(r, v.id, &en);
        if (e != LIB_OK) return e;
        out->head = 1;
        out->count = 1 + en.count;
        return with_title(e, out, en.name);
    case LVW_GENRE_SONGS:
        e = lib_genre(r, v.id, &en);
        if (e != LIB_OK) return e;
        out->songs = true;
        out->range = (pq_range_t){.order = LIB_ORDER_GENRE, .first = en.tfirst, .count = en.tcount};
        out->count = en.tcount;
        return with_title(e, out, en.name);
    default: return LIB_E_RANGE;
    }
}

static void mmss(char *out, size_t cap, uint32_t s) {
    if (s == 0) out[0] = '\0';
    else snprintf(out, cap, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
}

// A song row: the title and a second text (what is most useful in this list).
typedef enum { SUB_ARTIST, SUB_ALBUM, SUB_TIME } sub_t;

static lib_err_t song_row(lib_reader_t *r, uint32_t track_id, sub_t sub, lvw_row_t *out) {
    lib_track_t t;
    lib_err_t e = lib_track(r, track_id, &t);
    if (e != LIB_OK) return e;
    lvw_copy(out->text, sizeof out->text, t.title);
    out->track = t.track;
    out->codec = t.codec;
    {
        lib_entity_t al;
        char both[LIB_TEXT_MAX * 2 + 8];
        const char *album = lib_album(r, t.album, &al) == LIB_OK ? al.name : "";
        snprintf(both, sizeof both, "%s%s%s", t.artist_name, t.artist_name[0] != '\0' && album[0] != '\0' ? " \xC2\xB7 " : "", album);
        lvw_copy(out->sub, sizeof out->sub, both);
    }
    if (sub == SUB_ARTIST) {
        lvw_copy(out->detail, sizeof out->detail, t.artist_name);
    } else if (sub == SUB_ALBUM) {
        lib_entity_t al;
        if (lib_album(r, t.album, &al) == LIB_OK) lvw_copy(out->detail, sizeof out->detail, al.name);
    } else if (t.artist == LIB_NONE) { // a compilation: each song has its own performer, the length says less
        lvw_copy(out->detail, sizeof out->detail, t.artist_name);
    } else {
        mmss(out->detail, sizeof out->detail, t.duration_s);
    }
    out->child.kind = LVW_KINDS;
    out->play = t.play;
    return LIB_OK;
}

lib_err_t lvw_row(lib_reader_t *r, lvw_view_t v, const lvw_info_t *info, uint32_t i, lvw_row_t *out) {
    memset(out, 0, sizeof *out);
    out->child.kind = LVW_KINDS;
    if (i >= info->count) return LIB_E_RANGE;
    lib_row_t row;
    lib_entity_t ar, al;
    uint32_t id;
    lib_err_t e;
    switch (v.kind) {
    case LVW_ARTISTS:
    case LVW_GENRES:
        e = lib_list_row(r, v.kind == LVW_ARTISTS ? LIB_LIST_ARTISTS : LIB_LIST_GENRES, i, &row);
        if (e != LIB_OK) return e;
        lvw_copy(out->text, sizeof out->text, row.text);
        out->child = (lvw_view_t){v.kind == LVW_ARTISTS ? LVW_ARTIST : LVW_GENRE, row.id};
        return LIB_OK;
    case LVW_ALBUMS:
    case LVW_COMPILATIONS:
        e = lib_list_row(r, v.kind == LVW_ALBUMS ? LIB_LIST_ALBUMS : LIB_LIST_COMPILATIONS, i, &row);
        if (e != LIB_OK) return e;
        lvw_copy(out->text, sizeof out->text, row.text);
        lvw_copy(out->detail, sizeof out->detail, row.sub); // the album artist (empty for a compilation)
        out->child = (lvw_view_t){LVW_ALBUM, row.id};
        return LIB_OK;
    case LVW_SONGS:
        e = lib_list_row(r, LIB_LIST_SONGS, i, &row);
        if (e != LIB_OK) return e;
        lvw_copy(out->text, sizeof out->text, row.text);
        lvw_copy(out->detail, sizeof out->detail, row.sub);
        {
            char both[LIB_TEXT_MAX * 2 + 8];
            snprintf(both, sizeof both, "%s%s%s", row.sub, row.sub[0] != '\0' && row.album[0] != '\0' ? " \xC2\xB7 " : "", row.album);
            lvw_copy(out->sub, sizeof out->sub, both);
        }
        out->play = row.play;
        return LIB_OK;
    case LVW_ARTIST:
        if (i == 0) {
            lvw_copy(out->text, sizeof out->text, "All Songs");
            out->child = (lvw_view_t){LVW_ARTIST_SONGS, v.id};
            return LIB_OK;
        }
        e = lib_artist(r, v.id, &ar);
        if (e == LIB_OK) e = lib_album(r, ar.first + i - 1, &al);
        if (e != LIB_OK) return e;
        lvw_copy(out->text, sizeof out->text, al.name);
        snprintf(out->detail, sizeof out->detail, "%u", (unsigned)al.count);
        out->child = (lvw_view_t){LVW_ALBUM, ar.first + i - 1};
        return LIB_OK;
    case LVW_GENRE:
        if (i == 0) {
            lvw_copy(out->text, sizeof out->text, "All Songs");
            out->child = (lvw_view_t){LVW_GENRE_SONGS, v.id};
            return LIB_OK;
        }
        e = lib_genre(r, v.id, &al);
        if (e == LIB_OK) e = lib_genre_artist_at(r, al.first + i - 1, &id);
        if (e == LIB_OK) e = lib_artist(r, id, &ar);
        if (e != LIB_OK) return e;
        lvw_copy(out->text, sizeof out->text, ar.name);
        out->child = (lvw_view_t){LVW_ARTIST, id};
        return LIB_OK;
    case LVW_ALBUM:
        e = lib_by_album_at(r, info->range.first + i, &id);
        return e != LIB_OK ? e : song_row(r, id, SUB_TIME, out);
    case LVW_ARTIST_SONGS:
        e = lib_by_album_at(r, info->range.first + i, &id);
        return e != LIB_OK ? e : song_row(r, id, SUB_ALBUM, out);
    case LVW_GENRE_SONGS:
        e = lib_by_genre_at(r, info->range.first + i, &id);
        return e != LIB_OK ? e : song_row(r, id, SUB_ARTIST, out);
    default: return LIB_E_RANGE;
    }
}
