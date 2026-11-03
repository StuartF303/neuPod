// SPDX-License-Identifier: MPL-2.0
/**
 * @file settings_service.h
 * @brief RTOS glue for the settings store (settings_store.h): the lock, the load after the card is up, the debounced save.
 *
 * The storage task does all of the card access (the load and the writes, settings_service_poll). Core 1 never touches it.
 */
#ifndef NEUPOD_SETTINGS_SERVICE_H
#define NEUPOD_SETTINGS_SERVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Create the lock and reset the store to RAM-only defaults. Before the scheduler starts. */
void settings_service_init(void);

/**
 * The storage boot check's verdict. @p card_ok = the card is mounted and healthy: attach the store and load it, then apply
 * what it holds (volume; the UI re-reads settings_generation()). The load itself runs later on the storage task. Otherwise the store stays RAM-only.
 */
void settings_service_storage_result(bool card_ok);

/** Storage task: perform the queued load (settings_store.h) and apply the values that need applying (volume). */
void settings_service_load_now(void);

/**
 * Storage task, every few hundred ms and while it streams: write pending changes about 1.5 s after the first one.
 * @p quiet_now = false while the audio data ring is low (a file is streaming and the card must keep up): the save waits.
 */
void settings_service_poll(bool quiet_now);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_SETTINGS_SERVICE_H
