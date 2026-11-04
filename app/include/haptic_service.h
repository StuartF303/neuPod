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
 *
 * Actuator and calibration: the task also owns the DRV2605L actuator choice (ERM / LRA, stored in the settings
 * store) and its auto-calibration. The calibration results are stored too (haptic_logic.h) and re-applied on
 * every boot, so it runs once per actuator: automatically the first time an LRA is selected (or its parameters
 * change), and on request from Settings. It runs on this task (core 0), holding the I2C bus for about a second but
 * waiting with the scheduler's delay, so the UI is never blocked; clicks arriving meanwhile are dropped.
 */
#ifndef NEUPOD_HAPTIC_SERVICE_H
#define NEUPOD_HAPTIC_SERVICE_H

#include <stdbool.h>

#include "hal_haptic.h"

#ifdef __cplusplus
extern "C" {
#endif

void haptic_service_init(void);
bool haptic_service_ready(void);
void haptic_task(void *arg);

/** Request one click; never blocks. */
void rt_haptic_click(void);

/** The piezo click. There is no piezo driver in the firmware yet (the board footprint is not wired): does nothing. */
void rt_piezo_click(void);

typedef enum {
    HAPTIC_CAL_UNKNOWN = 0, /**< not calibrated (yet) */
    HAPTIC_CAL_NOT_NEEDED,  /**< ERM open loop: no calibration applies */
    HAPTIC_CAL_RUNNING,
    HAPTIC_CAL_OK,          /**< calibrated, results stored */
    HAPTIC_CAL_FAILED,      /**< the last attempt failed (the previous good values, if any, are still in use) */
} haptic_cal_state_t;

/** The actuator in use (what the settings say once loaded). Never blocks. */
hal_haptic_actuator_t rt_haptic_actuator(void);
/** Choose the actuator: stored, then applied by the task (an LRA without calibration calibrates itself). Never blocks. */
void rt_haptic_set_actuator(hal_haptic_actuator_t act);
/** Start a calibration now. Never blocks. */
void rt_haptic_request_calibration(void);
haptic_cal_state_t rt_haptic_cal_state(void);
/** The settings store was (re)loaded from the card: pick up the stored actuator and calibration. Never blocks. */
void rt_haptic_settings_loaded(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAPTIC_SERVICE_H
