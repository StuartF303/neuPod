// SPDX-License-Identifier: MPL-2.0
/**
 * @file art_service.h
 * @brief Album art for Now Playing: finds the picture of the track that just started, decodes it into an RGB565 thumbnail
 *        (160 x 160 with a cache of 24 albums on the hosted builds, one 64 x 64 on the Pico 2 W: art.h) and hands it to the UI.
 *        Also decodes the picture of the album being browsed (the library's album header). Runs inside the storage task (it is the only user of FatFs,
 *        which is not re-entrant): between pieces of decoding it calls a yield hook that keeps the audio data ring fed, sleeps
 *        1 ms after each 2 ms of work so the input and UI tasks run, and aborts as soon as a new request is queued.
 *
 * Sources in order: the embedded picture (JPEG only), then cover.jpg / folder.jpg / front.jpg in the track's folder. PNG and
 * progressive JPEG are not decoded (placeholder). The last result is kept per folder + album + embedded picture size, so the
 * next track of the same album shows it at once without decoding again.
 *
 * UI side: poll art_view_get() (cheap, never blocks), draw the pixels only while the state is READY, and call art_view_ack()
 * with the generation just seen so the decoder knows the old pixels are no longer on screen before it overwrites them.
 */
#ifndef NEUPOD_ART_SERVICE_H
#define NEUPOD_ART_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "art.h"
#include "meta.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ART_VIEW_NONE = 0, /**< nothing has been asked for yet */
    ART_VIEW_LOADING,  /**< a picture is being found / decoded: show the placeholder */
    ART_VIEW_READY,    /**< px holds a w x h thumbnail */
    ART_VIEW_FAILED,   /**< no usable picture (none, PNG, progressive, damaged): show the placeholder */
} art_view_state_t;

typedef struct {
    art_view_state_t state;
    uint32_t gen;        /**< changes whenever state or pixels change */
    uint16_t w, h;       /**< READY: thumbnail size, stride w */
    const uint16_t *px;  /**< RGB565, native order (as LVGL's draw buffers); only valid to read while READY */
} art_view_t;

/** UI side. */
void art_view_get(art_view_t *out);
void art_view_ack(uint32_t gen);

#define ART_BROWSE_N 5u /**< albums that can be asked for at once (the five Cover Flow tiles) */

/**
 * The albums being browsed. The library's album header asks for one (art_browse_want, art_browse_get); the Cover Flow asks for up to ART_BROWSE_N at once
 * with art_browse_want_set(): @p handles are library handles, most important first (the centre tile, then its neighbours), n = 0 says none is wanted
 * any more. The storage task decodes them in that order into the cache; a wanted album already in the cache costs nothing. The wanted ones, and the
 * ones wanted just before this call, are not evicted. art_browse_get_handle() answers for one handle (READY with the pixels, LOADING, FAILED for
 * an album with no usable picture, NONE for one that is not wanted); a READY answer's gen differs from any other picture's. Reads are safe against the
 * storage task changing the cache (the same retry counter as art_view_get()); the pixels stay valid until the next art_browse_want_set() call and the
 * one after it (the call after that may reuse the slot). UI task only. Builds without a cache (ART_CACHE_N == 1) answer NONE.
 */
void art_browse_want_set(const char *const *handles, unsigned n);
void art_browse_get_handle(const char *handle, art_view_t *out);

/**
 * The library's album header: the UI asks for the picture of the album whose first song is the library handle @p handle ("@lib/<order>/<pos>",
 * play_queue.h); "" says it is no longer wanted. The storage task resolves the handle, finds the picture and decodes it into the cache (a hit
 * when the album played before). Poll art_browse_get(): READY gives the thumbnail, FAILED means the album has no usable picture. Only the
 * builds with a cache (ART_CACHE_N > 1) have it; the others always say NONE.
 */
void art_browse_want(const char *handle);
void art_browse_get(art_view_t *out);

/** Turns a library handle into a file path (library_service_resolve; storage task only). Set once at start-up. */
typedef bool (*art_resolve_fn)(const char *handle, char *out, size_t n, const char **why);
void art_service_set_resolver(art_resolve_fn fn);

/** Storage task side. A track has just started: queue its art unless the cached one is for the same album. Cheap. */
void art_service_track(const char *path, const audio_tags_t *tags);
bool art_service_pending(void);
/**
 * Do the queued job now (storage task only). Returns when it is finished, failed, or @p yield returned false (the job stays
 * pending and restarts later). @p yield is called often; it should feed audio and may sleep.
 */
void art_service_run(art_yield_fn yield, void *yield_ctx);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_ART_SERVICE_H
