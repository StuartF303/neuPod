// SPDX-License-Identifier: MPL-2.0
/**
 * @file library_view.h
 * @brief The lists the Music screens show, as pure functions over a lib_reader_t (docs/adr/0017-music-library-index.md, L2).
 *
 * A view is one list: the artists, an artist's albums, an album's songs, all songs, a genre. lvw_info() says how many rows it has and, for a
 * list of songs, which range of the library's order tables a Select queues (play_queue.h, pq_range_t); lvw_row() fills one row. Nothing here
 * keeps state or touches the UI, so every list and every queue range is unit-tested on a FatFs disk image (tests/test_library_view.c).
 * Rows are read through the reader's small page cache, so a caller that scrolls a long list keeps its own cache of the rows it shows
 * (app/runtime/library_service.c does, on the storage task: the only task that may use FatFs).
 *
 * Browse tree (a row opens the view in brackets; a song row plays):
 *   Artists [artist] -> "All Songs" [artist songs], albums [album] -> songs      Albums [album] -> songs      Songs: play
 *   Genres [genre] -> "All Songs" [genre songs], artists [artist] -> ...         Compilations [album] -> songs
 */
#ifndef NEUPOD_LIBRARY_VIEW_H
#define NEUPOD_LIBRARY_VIEW_H

#include "library.h"
#include "play_queue.h" /* pq_range_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LVW_ARTISTS = 0,
    LVW_ALBUMS,
    LVW_SONGS,
    LVW_GENRES,
    LVW_COMPILATIONS,
    LVW_ARTIST,       /**< id = artist: "All Songs", then its albums */
    LVW_ALBUM,        /**< id = album: its songs in track order */
    LVW_GENRE,        /**< id = genre: "All Songs", then its artists */
    LVW_ARTIST_SONGS, /**< id = artist: every song of its albums, album by album */
    LVW_GENRE_SONGS,  /**< id = genre: every song of the genre, in album order */
    LVW_KINDS,        /**< (also: "no view" in lvw_row_t.child) */
} lvw_kind_t;

typedef struct {
    uint8_t kind; /**< lvw_kind_t */
    uint32_t id;
} lvw_view_t;

#define LVW_TEXT_MAX 48u   /**< first line of a row, UTF-8, NUL included (longer is cut at a character boundary) */
#define LVW_DETAIL_MAX 24u /**< right-hand text */

typedef struct {
    uint32_t count;          /**< rows, "All Songs" included */
    uint32_t head;           /**< rows before the entries (1 = the "All Songs" row) */
    bool songs;              /**< the rows after the head are songs: Select on row i queues range at (i - head) */
    pq_range_t range;        /**< songs lists: what a Select queues */
    char title[LVW_TEXT_MAX]; /**< status bar title: "Artists", the artist's name, ... */
} lvw_info_t;

typedef struct {
    char text[LVW_TEXT_MAX];
    char detail[LVW_DETAIL_MAX];
    lvw_view_t child; /**< the list a Select opens; kind LVW_KINDS for a song */
    uint8_t play;    /**< songs: lib_play_t (0 = playable; a Select shows why it is not) */
} lvw_row_t;

/** Rows and queue range of @p v. LIB_E_RANGE if the id does not exist. */
lib_err_t lvw_info(lib_reader_t *r, lvw_view_t v, lvw_info_t *out);
/** Row @p i (0 .. info->count - 1) of @p v. @p info is what lvw_info() returned for it. */
lib_err_t lvw_row(lib_reader_t *r, lvw_view_t v, const lvw_info_t *info, uint32_t i, lvw_row_t *out);

/** Copy @p src into @p dst (@p cap bytes) cutting at a UTF-8 character boundary. */
void lvw_copy(char *dst, size_t cap, const char *src);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_LIBRARY_VIEW_H
