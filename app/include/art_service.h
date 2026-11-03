// SPDX-License-Identifier: MPL-2.0
/**
 * @file art_service.h
 * @brief Album art for Now Playing: finds the picture of the track that just started, decodes it into one 64 x 64 RGB565
 *        thumbnail (8 KB of static RAM) and hands it to the UI. Runs inside the storage task (it is the only user of FatFs,
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
