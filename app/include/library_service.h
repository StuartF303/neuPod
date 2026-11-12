// SPDX-License-Identifier: MPL-2.0
/**
 * @file library_service.h
 * @brief The music library on the storage task: open the index, fill list rows, build it, find the file behind a queue entry
 *        (docs/adr/0017-music-library-index.md, L2).
 *
 * FatFs is not re-entrant and the storage task is its only user, so the UI never reads the index itself. It posts a request here (through
 * the storage task's queue), keeps drawing, and polls for the answer, as the Files screen does for a folder listing. Rows come back in a
 * small window the storage task fills ahead of the scrolling (LIBSVC_ROWS rows); anything outside it is a placeholder until the next fill.
 *
 * Memory: the reader (about 17 KB) and the row window share one static buffer with the Files screen's folder listing (storage_service.h:
 * the two are never on screen together), so the library adds no RAM for them. Whoever asks second takes the buffer over: a folder listing
 * closes the reader, and the next library request opens it again.
 *
 * Building (the scan) borrows the audio core's codec arena for its 32 KB+ of working memory, so it only runs with nothing playing and
 * refuses to start otherwise. It runs inside one storage-task request that can last minutes; it watches for Cancel and for another request
 * arriving (a Play press), and gives up in either case, leaving the old library untouched.
 */
#ifndef NEUPOD_LIBRARY_SERVICE_H
#define NEUPOD_LIBRARY_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library_view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIBSVC_ROWS 16u /**< rows in the window the storage task keeps filled */

/** Outcome of a request (also what the screens say). */
typedef enum {
    LIBSVC_OK = 0,
    LIBSVC_NO_CARD,        /**< no card, or no mounted volume */
    LIBSVC_NONE,           /**< no library has been built yet */
    LIBSVC_DAMAGED,        /**< the index is damaged or from another version: rebuild it */
    LIBSVC_ERROR,          /**< a card read error */
    LIBSVC_PLAYING,        /**< a build needs nothing playing */
    LIBSVC_NEEDS_WRITABLE, /**< a build needs a card that can be written (write-protected, or the write lock was refused) */
    LIBSVC_FULL,           /**< no room on the card for the index */
    LIBSVC_CANCELLED,      /**< the build was cancelled (the old library is untouched) */
    LIBSVC_BUSY,           /**< another task holds the card's write scope: try again */
} libsvc_status_t;

/** Plain-language text for a status, for the screens ("Library needs a writable card"). */
const char *library_status_text(libsvc_status_t s);

typedef enum { LIBSVC_REQ_INFO = 0, LIBSVC_REQ_ROWS, LIBSVC_REQ_BUILD, LIBSVC_REQS } libsvc_kind_t;

/* ---- UI side: post a request, poll for its answer ---------------------------------------------------------------------------------- */

/** Open the index if needed and compute the list's size and queue range (library_info()). False if the request queue is full. */
bool library_request_info(lvw_view_t v, uint32_t seq);
/** Fill the row window with rows @p first .. first + n - 1 (n is cut to LIBSVC_ROWS) of @p v. */
bool library_request_rows(lvw_view_t v, uint32_t first, uint32_t n, uint32_t seq);
/** Build the whole index (a rebuild if there is one). Refused with LIBSVC_PLAYING while anything plays. */
bool library_request_build(uint32_t seq);
/** Ask a running build to stop at its next file. Harmless when none runs. */
void library_cancel_build(void);

/** True when request @p seq of kind @p k has finished; @p status gets its outcome. */
bool library_poll(libsvc_kind_t k, uint32_t seq, libsvc_status_t *status);
/** What the last finished library_request_info() found. Valid after its poll said so; copied, so safe to keep. */
void library_info(lvw_info_t *out);
/** Row @p idx of @p v if it is in the window (the window is filled for one list at a time: another list's rows never match). */
bool library_row(lvw_view_t v, uint32_t idx, lvw_row_t *out);

typedef struct {
    bool active;           /**< a build is running */
    uint32_t files;        /**< audio files read so far */
    uint32_t tracks, artists, albums, genres; /**< the last finished build */
} library_progress_t;
void library_progress(library_progress_t *out);

/** "Can't play: <reason>" for a lib_play_t (a constant string: no RAM). The screens and the queue's resolver say the same words. */
const char *lib_cant_play_str(uint8_t play);

/** The album and album artist ids of a track the player started from the library (its queue handle; a compilation's artist is LIB_NONE). Any task.
 *  False when it is not one of the last two tracks the storage task resolved (the track plays from a folder, or the list moved on). */
bool library_track_ids(const char *handle, uint32_t *album, uint32_t *artist);

/** True once a library has been opened this boot and it was fine: Shuffle Songs and the Music screens can then skip the wait. */
bool library_known_ready(void);

/* ---- storage task side (storage_service.c) ----------------------------------------------------------------------------------------- */

void library_service_init(void);
/** One queued request (the payload in @p blob, as stored by the library_request_* calls). Runs on the storage task. */
void library_service_handle(const void *blob, size_t n);
/** The shared buffer's Files-listing view; taking it for a listing closes the reader and forgets the row window. */
file_entry_t *library_service_take_listing(void);
/**
 * Turn a queue handle ("@lib/<order>/<pos>", play_queue.h) into the file path to open. False with @p why set (a message for the user)
 * when the track cannot be played or cannot be found. Keeps the answer for library_service_real_path(). Storage task only.
 */
bool library_service_resolve(const char *handle, char *out, size_t n, const char **why);
/** The real path a handle was resolved to a moment ago (for the resume record); any task. False if it is not one of the last two. */
bool library_service_real_path(const char *handle, char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_LIBRARY_SERVICE_H
