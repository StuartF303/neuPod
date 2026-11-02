// SPDX-License-Identifier: MPL-2.0
/**
 * @file storage_service.h
 * @brief Storage / prefetch task (STUB): the place where library scans and track prefetch will
 *        run, between the decoder and the input task in priority.
 */
#ifndef NEUPOD_STORAGE_SERVICE_H
#define NEUPOD_STORAGE_SERVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Task body. Waits for requests; currently only logs them. */
void storage_task(void *arg);

/**
 * Ask the storage task to prefetch @p path (copied; truncated to 63 characters). Returns false if
 * the request queue is full or the task is not running. Safe from any task.
 */
bool storage_prefetch_request(const char *path);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_STORAGE_SERVICE_H
