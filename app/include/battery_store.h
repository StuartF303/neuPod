// SPDX-License-Identifier: MPL-2.0
/**
 * @file battery_store.h
 * @brief Latest battery reading, published by the monitor/boot tasks and read by the UI.
 */
#ifndef NEUPOD_BATTERY_STORE_H
#define NEUPOD_BATTERY_STORE_H

#include <stdbool.h>

#include "hal_battery.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create the lock. Call once before the scheduler starts. */
void battery_store_init(void);

/** Replace the stored reading (latest value wins). */
void battery_store_publish(const hal_battery_status_t *st);

/** Copy the latest reading. False until something valid has been published. */
bool battery_store_get(hal_battery_status_t *out);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BATTERY_STORE_H
