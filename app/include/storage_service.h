// SPDX-License-Identifier: MPL-2.0
/**
 * @file storage_service.h
 * @brief Storage task: directory listings and volume space, run off the UI task.
 *
 * Why a task: nfs calls on the SD card block for milliseconds each (a directory scan is hundreds of
 * sector reads) and the UI task must never stall, so the UI posts a request and polls for the result.
 * The task also runs at a higher priority than the UI and yields every few entries so input and the UI
 * keep running. Read-only: nothing here writes to the card.
 *
 * The task also streams the WAV file chosen for playback (see storage_play_request): while a stream runs it
 * wakes every millisecond to top up the audio data ring.
 *
 * Single result buffer: the entries of the LATEST listing live in one static array. The UI may read
 * them only after the result for its latest request arrived and must not read them while a newer request
 * is in flight (it stops reading as soon as it sends one).
 */
#ifndef NEUPOD_STORAGE_SERVICE_H
#define NEUPOD_STORAGE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILES_MAX_ENTRIES 256u /**< entries kept per directory (dirs first, then files); more are reported as truncated */
#define FILES_NAME_LEN 64u     /**< bytes per name kept (UTF-8, cut at a character boundary) */

typedef struct {
    char name[FILES_NAME_LEN];
    uint64_t size;
    bool is_dir;
} file_entry_t;

typedef enum { STORAGE_RES_LIST = 1, STORAGE_RES_SPACE } storage_result_kind_t;

typedef struct {
    storage_result_kind_t kind;
    uint32_t seq; /**< the request's sequence number */
    int err;      /**< nfs_err_t (0 = ok) */
    uint32_t count;
    bool truncated;
    uint64_t total_bytes, free_bytes; /**< STORAGE_RES_SPACE */
} storage_result_t;

/** Create queues (before the scheduler). */
void storage_service_init(void);
bool storage_service_ready(void);

/** Task body. */
void storage_task(void *arg);

/** Ask for the listing of @p path (absolute, e.g. "/"). False if the request queue is full. */
bool storage_list_request(const char *path, uint32_t seq);
/** Ask for total/free bytes of the volume (slow on a big FAT volume). */
bool storage_space_request(uint32_t seq);
/** Next finished request, if any. */
bool storage_result_poll(storage_result_t *out);
/** Entries of the latest listing (valid per the rules above). */
const file_entry_t *storage_list_entries(void);

/**
 * Ask the storage task to play the WAV at @p path: it reads the header, starts the audio core's PCM stream
 * and keeps feeding the data ring from the card until the end of the file. Progress and errors are reported
 * through player_get() (player.h). A new request replaces the stream in progress.
 */
bool storage_play_request(const char *path);
/** Stop feeding the current file stream (the caller stops the audio core itself). */
bool storage_stop_request(void);

/**
 * Ask the storage task to prefetch @p path (stub: logged). Copied; truncated to 63 characters.
 */
bool storage_prefetch_request(const char *path);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_STORAGE_SERVICE_H
