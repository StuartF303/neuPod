// SPDX-License-Identifier: MPL-2.0
/**
 * @file library.h
 * @brief Music library index (docs/adr/0017-music-library-index.md, stage L1): collation, the scanner that builds the index
 *        on the card, and the reader that browses it.
 *
 * The index is a set of immutable files in <base>/lib/ (base is "/.neupod"), each written once per build, little-endian, with a
 * 16-byte header (magic, version, count, crc32). The manifest is written last; a build without a valid manifest does not exist.
 * A build goes to <base>/lib.tmp/ and is swapped in with renames (lib -> lib.old, lib.tmp -> lib), so a power cut leaves the old
 * library or the new one, never a mix (lib_recover() finishes or undoes an interrupted swap at boot).
 *
 * All file access is FatFs (ff.h) on a mounted volume, the same code on the card and on the host disk image. Nothing here allocates:
 * the scanner works in a caller-supplied scratch buffer (the codec arena while nothing is playing) and the reader is a caller-
 * allocated lib_reader_t. Writing needs the storage write lock (hal_storage_set_write_enabled): lib_build() and lib_recover() turn
 * it on for their duration, as the settings store does.
 */
#ifndef NEUPOD_LIBRARY_H
#define NEUPOD_LIBRARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef NEUPOD_LIB_POSIX
#include "lib_ff_posix.h" /* the host simulator's card is a folder: FatFs-style calls over POSIX */
#else
#include "ff.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LIB_OK = 0,
    LIB_E_IO,        /**< a FatFs / card error */
    LIB_E_FULL,      /**< no space on the card */
    LIB_E_NONE,      /**< no valid library on the card */
    LIB_E_FORMAT,    /**< a file of the index is damaged or of another version */
    LIB_E_RANGE,     /**< row / id out of range */
    LIB_E_SCRATCH,   /**< scratch buffer too small */
    LIB_E_DENIED,    /**< a write outside the library's folder was refused by the nfs write scope (or no scope was open) */
    LIB_E_BUSY,      /**< another task holds the nfs write scope, or a scan / parse already has the tag hooks: try again later */
    LIB_E_CANCELLED, /**< the progress callback asked to stop; the old library is untouched */
#ifdef NEUPOD_LIB_TEST_HOOKS
    LIB_E_TEST_STOP, /**< lib_build_cfg_t.test_stop_step reached (power-cut simulation in tests) */
#endif
} lib_err_t;

const char *lib_err_str(lib_err_t e);

/* ---- collation ---------------------------------------------------------------------------------------------------- */

/** Longest sort key lib_collate_key() writes (longer names are cut: only their order beyond this point is lost). */
#define LIB_KEY_MAX 96u
/** Bytes of the folded key kept in the sort records. The on-card tables keep the first 8 of them. */
#define LIB_KEY_PREFIX 24u
/** Letter-jump buckets: 0 = "#" (digits, symbols, everything that is not A-Z once folded), 1..26 = A..Z. */
#define LIB_BUCKETS 27u

/**
 * Sort key of @p name, or of @p sort when it is non-empty (a sort tag wins). Compare keys with memcmp (zero-padded). Rules: ASCII is
 * case-folded, Latin-1 and Latin Extended-A letters lose their accents (ss, ae, oe, th and ij expand), a leading "The " is ignored,
 * and the classes order as symbols (by code point, a space first), then digits, then letters. Returns the key length (no NUL).
 */
size_t lib_collate_key(const char *name, const char *sort, uint8_t *key, size_t cap);
/** Compare two names / sort tags the way the library orders them (a key comparison). */
int lib_collate_cmp(const char *a, const char *sa, const char *b, const char *sb);
/** Letter-jump bucket (0..26) of a key: the class of its first character. @p key must hold at least one byte; an empty name's key is just the
 *  zero lib_collate_key() stores after it (cap >= 1), which is bucket 0. */
unsigned lib_collate_bucket(const uint8_t *key);

/* ---- what a build needs and returns ------------------------------------------------------------------------------- */

#define LIB_BASE_DEFAULT "/.neupod"
#define LIB_BASE_MAX 46u /**< longest base path (longer is LIB_E_RANGE): every path the library builds from it fits its buffers */
#define LIB_ROOT_DEFAULT "/Music"
/** Scratch the scanner needs at least (the ADR's 32 KB): run formation, merge fan-in and the open files all live in it. */
#define LIB_SCRATCH_MIN (32u * 1024u)
/** Bumped by firmware that adds a codec or lifts a limit: the next scan re-checks only the tracks that could not be played (D6, L3). */
#define LIB_DECODER_SET_VERSION 1u

typedef struct {
    const char *base;         /**< where lib/, lib.old/, lib.tmp/ live; NULL = LIB_BASE_DEFAULT */
    const char *root;         /**< the music folder to walk; NULL = LIB_ROOT_DEFAULT. Missing = an empty library */
    uint64_t fingerprint;     /**< recorded in the manifest (D3: volume serial + free clusters; the caller computes it) */
    uint32_t decoder_version; /**< 0 = LIB_DECODER_SET_VERSION */
    /** Called after every audio file: files done so far. Return false to cancel (the old library is untouched). May be NULL. */
    bool (*progress)(void *user, uint32_t files_done);
    /** Called once, before the walk, with the number of audio files a quick directory pass found (the progress's denominator). May be NULL. */
    void (*total)(void *user, uint32_t files_total);
    /** Called when the walk enters a folder, with its full path (valid during the call only). May be NULL. */
    void (*where)(void *user, const char *folder);
    void *user;
#ifdef NEUPOD_LIB_TEST_HOOKS /* only in the test library (neupod_library_test): production code carries none of this */
    int test_stop_step; /**< stop with LIB_E_TEST_STOP after this step (1 = tables written, before the manifest; 2 = tmp complete;
                             3 = lib moved to lib.old; 4 = tmp moved to lib; 5 = lib.old deleted); 0 = run on */
#endif
} lib_build_cfg_t;

typedef struct {
    uint32_t tracks, playable, artists, albums, genres, dirs;
    uint32_t generation;
    uint32_t scratch_peak;    /**< most scratch bytes in use at once */
    uint32_t sort_runs, sort_merges; /**< external sort work: runs written, merge steps made */
} lib_build_stats_t;

/**
 * PRECONDITIONS (L2 wires the caller): call from the storage task only, with nothing playing and no other parse running. The scanner
 * borrows the parsers' two process-wide hooks (meta_set_scratch, meta_set_tags_ext) for its whole run, so a concurrent track open would
 * corrupt both; track_stream refuses to open a file while the tag hooks are held, and lib_build() returns LIB_E_BUSY if they already
 * are. LIB_E_BUSY is also returned when another task holds the nfs write scope (a settings save): wait and call again.
 *
 * Build the whole index from scratch and swap it in. @p scratch / @p scratch_n: at least LIB_SCRATCH_MIN bytes, 8-byte aligned
 * (the codec arena). Takes minutes on a big card: call it from the storage task with nothing playing. @p stats may be NULL.
 */
lib_err_t lib_build(const lib_build_cfg_t *cfg, void *scratch, size_t scratch_n, lib_build_stats_t *stats);

/**
 * Boot recovery after a power cut: if lib is missing or its manifest is bad and lib.old is valid, lib.old becomes lib again; a leftover
 * lib.old / lib.tmp / work folder is deleted once lib is valid. LIB_OK when a valid library is in place, LIB_E_NONE if there is none.
 */
lib_err_t lib_recover(const char *base);
#ifdef NEUPOD_LIB_TEST_HOOKS
extern int lib_test_fail_rename; /**< tests: make the Nth rename of the swap / recovery fail (1 = the next); 0 = off */
#endif

/* ---- manifest ----------------------------------------------------------------------------------------------------- */

/** Lists the UI browses. Row i of a list is an id in one of the tables. */
typedef enum { LIB_LIST_ARTISTS = 0, LIB_LIST_ALBUMS, LIB_LIST_SONGS, LIB_LIST_GENRES, LIB_LIST_COMPILATIONS, LIB_LISTS } lib_list_t;

#define LIB_FILES 13u /**< files of the set besides the manifest */

typedef struct {
    uint32_t generation;
    uint32_t decoder_version;
    uint64_t fingerprint;
    uint32_t tracks, playable, artists, albums, genres, dirs;
    uint32_t comp_first;          /**< first album that is a compilation (they follow all the artists' albums) */
    uint32_t genre_artist_links;
    uint32_t genre_tracks;        /**< tracks that have a genre (the length of by_genre) */
    uint32_t file_crc[LIB_FILES], file_size[LIB_FILES];
    uint32_t jump[LIB_LISTS][LIB_BUCKETS]; /**< first row whose letter bucket is >= the index (the row count if none) */
} lib_manifest_t;

/** Read and check <dir>/manifest.bin and that every file of the set has its recorded size; @p full also checks every file's CRC. */
lib_err_t lib_manifest_load(const char *dir, bool full, lib_manifest_t *out);

/* ---- reader ------------------------------------------------------------------------------------------------------- */

#define LIB_TEXT_MAX 65u /**< a name or title, UTF-8, NUL included */
#define LIB_PAGE_BYTES 2048u
#ifdef NEUPOD_ART_HIRES
#define LIB_PAGES 5u /* a song row touches five table pages (order, track, strings, album): the pages of one row must all stay cached; the reader plus the row window must fit the Files buffer (library_service.c) */
#else
#define LIB_PAGES 4u /* the Pico 2 W (frozen): its song rows re-read pages instead; the reader + row window stay within the Files buffer */
#endif
#define LIB_NONE 0xFFFFFFFFu

/** Why a track cannot be played (D6); 0 = playable. */
typedef enum {
    LIB_PLAY_OK = 0,
    LIB_PLAY_CODEC,    /**< AAC or another codec the player has no decoder for */
    LIB_PLAY_CHANNELS, /**< more than 2 channels */
    LIB_PLAY_BITS,     /**< bit depth the decoder cannot take (incl. 32-bit ALAC) */
    LIB_PLAY_DAMAGED,  /**< bad header, truncated, no audio */
    LIB_PLAY_RATE,     /**< not 44.1 / 48 / 88.2 / 96 kHz */
    LIB_PLAY_BLOCK,    /**< FLAC block size above what the decoder memory allows */
} lib_play_t;

/** Container / codec of a track as the scanner saw it (an AAC file is recognised, not decodable: see lib_play_t). */
typedef enum { LIB_CODEC_NONE = 0, LIB_CODEC_WAV, LIB_CODEC_FLAC, LIB_CODEC_MP3, LIB_CODEC_ALAC, LIB_CODEC_AAC } lib_codec_t;

typedef struct {
    uint32_t id;
    uint32_t dir, size, mtime, duration_s, rate;
    uint16_t genre; /**< genre id, 0xFFFF = none */
    uint16_t year, track;
    uint8_t disc, compilation, codec, bits, play; /**< play: lib_play_t */
    uint32_t artist; /**< album-artist id (LIB_NONE for compilations) */
    uint32_t album;
    char title[LIB_TEXT_MAX], artist_name[LIB_TEXT_MAX];
} lib_track_t;

typedef struct {
    char name[LIB_TEXT_MAX];
    uint32_t first;  /**< artist: first album id; album: first position in the by-album order; genre: first position in the genre-artist order */
    uint32_t count;  /**< albums of the artist / tracks of the album / artists of the genre */
    uint32_t artist; /**< albums only: the album artist's id (LIB_NONE for compilations) */
    uint32_t tfirst, tcount; /**< genres only: its tracks, as positions in the by-genre order (album order within a genre) */
} lib_entity_t;

typedef struct {
    uint32_t id;             /**< artist / album / track / genre id */
    char text[LIB_TEXT_MAX]; /**< first line: name or title */
    char sub[LIB_TEXT_MAX];  /**< second line: album artist of an album, artist of a song; empty otherwise */
    char album[LIB_TEXT_MAX]; /**< songs: the album's name (the second line of a two-line row is "artist . album"); empty otherwise */
    uint32_t count;          /**< children: albums of an artist, tracks of an album, artists of a genre; 0 for songs */
    uint8_t play;            /**< songs: lib_play_t */
} lib_row_t;

/** One open table: a FIL, its fast-seek link map and its size. */
typedef struct {
    FIL f;
    DWORD clmt[16];
    bool open, fast;
    uint32_t payload; /**< payload bytes (the file minus its 16-byte header) */
} lib_table_t;

typedef struct {
    uint8_t data[LIB_PAGE_BYTES];
    uint32_t table, page, stamp, len;
    bool valid;
} lib_page_t;

/** Caller-allocated; no heap. The FIL objects plus the 8 KB page cache (the build report states sizeof). */
typedef struct {
    lib_manifest_t m;
    lib_table_t t[10]; /**< tracks, strings, artists, albums, genres, by_album, by_title, by_album_name, genre_artists, enames */
    lib_table_t aux;  /**< dirs.bin / names.str, opened on demand */
    int aux_which;
    lib_page_t page[LIB_PAGES];
    uint32_t clock;
    uint32_t page_hits, page_misses;
    char dir[64]; /**< "<base>/lib" */
} lib_reader_t;

/** Open <base>/lib (NULL = LIB_BASE_DEFAULT): reads the manifest, opens the tables. LIB_E_NONE if there is no valid library. */
lib_err_t lib_open(lib_reader_t *r, const char *base);
void lib_close(lib_reader_t *r);
const lib_manifest_t *lib_manifest(const lib_reader_t *r);

/** Rows in list @p l (compilations: the albums that are compilations). */
uint32_t lib_list_count(const lib_reader_t *r, lib_list_t l);
/** Row @p i of list @p l: what to show. */
lib_err_t lib_list_row(lib_reader_t *r, lib_list_t l, uint32_t i, lib_row_t *out);
/** Row @p i of list @p l as an id (artist / album / track / genre id, by the list). */
lib_err_t lib_list_id(lib_reader_t *r, lib_list_t l, uint32_t i, uint32_t *id);
/** Row of list @p l to jump to for letter @p ch ('A'..'Z' either case, anything else = '#'): the first row at or after that letter. */
uint32_t lib_list_jump(const lib_reader_t *r, lib_list_t l, char ch);

lib_err_t lib_track(lib_reader_t *r, uint32_t id, lib_track_t *out);
/** Just the file's modified time, size and play verdict (lib_play_t) of track @p id: one 48-byte record, no strings (the Recently added pass over every track). */
lib_err_t lib_track_stamp(lib_reader_t *r, uint32_t id, uint32_t *mtime, uint32_t *size, uint8_t *play);
lib_err_t lib_artist(lib_reader_t *r, uint32_t id, lib_entity_t *out); /**< first / count = its albums (ids first .. first+count-1) */
lib_err_t lib_album(lib_reader_t *r, uint32_t id, lib_entity_t *out);  /**< first / count = its tracks, as positions in the by-album order */
lib_err_t lib_genre(lib_reader_t *r, uint32_t id, lib_entity_t *out);  /**< first / count = its artists, as positions in the genre-artist order */
/** Track id at position @p pos of the by-album order (an album's tracks are contiguous, and so are an artist's albums' tracks). */
lib_err_t lib_by_album_at(lib_reader_t *r, uint32_t pos, uint32_t *track_id);
/** Track id at position @p pos of the by-genre order (all of a genre's tracks are contiguous). */
lib_err_t lib_by_genre_at(lib_reader_t *r, uint32_t pos, uint32_t *track_id);
/** Artist id at position @p pos of the genre-artist order. */
lib_err_t lib_genre_artist_at(lib_reader_t *r, uint32_t pos, uint32_t *artist_id);
/** Full path of a track ("/Music/Artist/Album/01 Song.flac"). */
lib_err_t lib_track_path(lib_reader_t *r, uint32_t id, char *out, size_t cap);

/* ---- playing from the library ------------------------------------------------------------------------------------- */

/** The three orders a queue can run through (a range of one of them is "the album", "all songs", "this genre"): each file lists track ids. */
typedef enum { LIB_ORDER_TITLE = 0, LIB_ORDER_ALBUM, LIB_ORDER_GENRE, LIB_ORDERS } lib_order_t;

/**
 * Path of the track at position @p pos of @p order, without a lib_reader_t: opens the few small files it needs one at a time on a local
 * FIL (about 600 B of stack), so the storage task can resolve a queue entry while the browse reader is closed, busy or in use for another
 * list. @p play (may be NULL) gets the track's lib_play_t. LIB_E_NONE if there is no library.
 */
lib_err_t lib_resolve(const char *base, lib_order_t order, uint32_t pos, char *path, size_t cap, uint8_t *play);
/** lib_resolve() that also says which album (@p album) and album artist (@p album_artist; LIB_NONE for a compilation) the track belongs to (either may be NULL):
 *  what "Go to album" / "Go to artist" on Now Playing open. */
lib_err_t lib_resolve_ids(const char *base, lib_order_t order, uint32_t pos, char *path, size_t cap, uint8_t *play, uint32_t *album, uint32_t *album_artist);

#ifdef __cplusplus
}
#endif

#endif
