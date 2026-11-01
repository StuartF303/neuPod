// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_power.h
 * @brief Battery state and low-power control.
 */
#ifndef NEUPOD_HAL_POWER_H
#define NEUPOD_HAL_POWER_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Battery snapshot. */
typedef struct {
    uint16_t millivolts;   /**< Cell voltage. */
    uint8_t percent;       /**< State of charge 0..100 (fuel-gauge estimate). */
    bool external_power;   /**< USB power present; false if unknown. */
    bool valid;            /**< False if the gauge could not be read. */
} hal_battery_state;

/** Initialise power monitoring (fuel gauge etc). */
hal_status_t hal_power_init(void);

/** Read the current battery state. */
hal_status_t hal_power_battery(hal_battery_state *out);

/** Enter the lowest-power state from which the given wake sources can resume. TODO: wake API. */
void hal_power_sleep(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_POWER_H
