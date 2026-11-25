// SPDX-License-Identifier: MPL-2.0
/**
 * @file playlist_service.h
 * @brief Music > Playlists on the storage task (design review LB-7): the list of lists, the entries of one, and On-The-Go (docs/storage.md).
 *
 * Lists: the five smart ones, then the .m3u / .m3u8 files found on the card.
 *  - On-The-Go: /.neupod/otg.m3u8, an ordinary playlist file this firmware writes (the user adds from Now Playing; at most PLS_OTG_MAX entries).
 *  - Recently added: the PLS_RECENT_N newest files of the library (by the file's modified time; one pass over the index).
 *  - Most played: the PLS_MOST_N tracks with the most counted plays (plays_store.h).
 *  - Top rated: every track with four or more stars that the plays store knows (a rating made before this feature appears once the track is played or re-rated).
 *  - Unplayed: library tracks with no counted play, in title order, the first PLS_ENTRIES_MAX of them.
 *  - Files: every .m3u / .m3u8 in "/", "/Playlists" and "/Music" (not their sub-folders), at most PLS_FILES_MAX, by name.
 * Like the library service, the UI posts a request, keeps drawing and polls for the answer; FatFs is the storage task's alone. One list is open at a
 * time (its entries live in one static pool, PLS_POOL_BYTES); a new open replaces it, and the UI reads the entries only after its request was answered and
 * before it sends another. Only the builds with the playlists (NEUPOD_ART_HIRES: hosted, Y2, PSRAM boards) have it; elsewhere every request is refused and
 * PLS_AVAILABLE is 0 (the Pico 2 W is frozen, its RAM is full).
 */
#ifndef NEUPOD_PLAYLIST_SERVICE_H
#define NEUPOD_PLAYLIST_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NEUPOD_ART_HIRES
#define PLS_AVAILABLE 1
#else
#define PLS_AVAILABLE 0
#endif

#define PLS_FILES_MAX 24u
#define PLS_ENTRIES_MAX 128u
#define PLS_OTG_MAX 100u
#define PLS_RECENT_N 50u
#define PLS_MOST_N 25u
#define PLS_POOL_BYTES 16384u
#define PLS_NAME_MAX 40u
#define PLS_OTG_FILE "/.neupod/otg.m3u8"
#define PLS_OTG_TMP "/.neupod/otg.tmp"

typedef enum { PLS_OTG = 0, PLS_RECENT, PLS_MOST, PLS_TOP, PLS_UNPLAYED, PLS_SMART_N, PLS_FILE = 16 } pls_kind_t;

/** What a request answered. */
typedef enum {
    PLS_OK = 0,
    PLS_NO_CARD,
    PLS_NO_LIBRARY, /**< a smart list over the library, and there is no index (Update Library first) */
    PLS_ERROR,      /**< a card read or write error */
    PLS_READONLY,   /**< a write-protected card (On-The-Go cannot be saved) */
    PLS_FULL,       /**< On-The-Go holds PLS_OTG_MAX entries */
    PLS_DUPLICATE,  /**< already in On-The-Go */
    PLS_BAD,        /**< not a file the player can play / nothing to act on */
    PLS_BUSY,       /**< the request queue is full or another task holds the card's write scope: try again */
} pls_status_t;

/** One row of the list of lists. */
typedef struct {
    uint8_t kind;      /**< pls_kind_t; PLS_FILE for a playlist file */
    uint8_t file;      /**< PLS_FILE: its index among the files (what pls_request_open() takes) */
    bool count_known;  /**< false: the library or the card could not say (the row shows no count) */
    bool count_capped; /**< the count is a floor ("128+") */
    uint32_t count;
    char name[PLS_NAME_MAX]; /**< "On-The-Go", "Night drive.m3u" */
} pls_item_t;

typedef enum { PLS_ENT_OK = 0, PLS_ENT_UNRESOLVABLE, PLS_ENT_MISSING, PLS_ENT_CANT_PLAY } pls_ent_state_t;

/** One entry of the open list, as a row shows it. */
typedef struct {
    const char *title; /**< never empty */
    const char *sub;   /**< "Artist" or the folder name; may be "" */
    const char *path;  /**< absolute; "" when unresolvable */
    uint8_t state;     /**< pls_ent_state_t: anything but OK is greyed and skipped when a list is played */
} pls_entry_view_t;

/* ---- UI side: post a request, poll for its answer --------------------------------------------------------------------------------- */

/** Build the list of lists (counts included). Answer: pls_catalog_*(). */
bool pls_request_catalog(uint32_t seq);
/** Open one list (a pls_item_t's kind / file): fills the entries. Answer: pls_entry_*(). */
bool pls_request_open(uint8_t kind, uint8_t file, uint32_t seq);
/** True when request @p seq (of either kind above) is answered; @p st gets the outcome. */
bool pls_poll(uint32_t seq, pls_status_t *st);

uint32_t pls_catalog_count(void);
bool pls_catalog_item(uint32_t i, pls_item_t *out);

/** The open list: entry count, whether the list was cut at the limit (PLS_ENTRIES_MAX or the pool), a row's text. Valid between the answer and the next request. */
uint32_t pls_entry_count(void);
bool pls_entry_truncated(void);
bool pls_entry(uint32_t i, pls_entry_view_t *out);

/* On-The-Go. Each has its own answer (pls_otg_poll). The add takes copies, so the caller's buffers may go at once. */
bool pls_otg_add(const char *path, const char *title, const char *artist, uint32_t secs, uint32_t seq);
bool pls_otg_remove(uint32_t index, uint32_t seq);
bool pls_otg_clear(uint32_t seq);
bool pls_otg_poll(uint32_t seq, pls_status_t *st);

/** Plain-language text for a status (a toast). */
const char *pls_status_text(pls_status_t s);

/* ---- storage task side ------------------------------------------------------------------------------------------------------------- */

/** One queued request (payload as stored by the calls above). Runs on the storage task. */
void pls_service_handle(const void *blob, size_t n);
/** For tests and the host tour: forget the open list and the catalog. */
void pls_service_reset(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_PLAYLIST_SERVICE_H
