// SPDX-License-Identifier: MPL-2.0
/**
 * @file haptic_service.h
 * @brief Click feedback: a request queue served by its own task, so the UI never waits on the I2C bus.
 *
 * rt_haptic_click() only drops a byte in a 2-deep queue (no wait, drops when full). The task owns the
 * bus mutex while it talks to the DRV2605L, rate-limits requests (RT_HAPTIC_MIN_GAP_MS), wakes the
 * chip from standby on the first click (click_prepare once), and puts it back into standby after
 * RT_HAPTIC_IDLE_MS without clicks. Any driver error puts the motor driver into its safe state.
 * Requests are ignored unless the haptics boot check passed.
 */
#ifndef NEUPOD_HAPTIC_SERVICE_H
#define NEUPOD_HAPTIC_SERVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void haptic_service_init(void);
bool haptic_service_ready(void);
void haptic_task(void *arg);

/** Request one click; never blocks. */
void rt_haptic_click(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAPTIC_SERVICE_H
