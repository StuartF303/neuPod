// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_power.h
 * @brief Low-power control. (Battery state lives in hal_battery.h.)
 */
#ifndef NEUPOD_HAL_POWER_H
#define NEUPOD_HAL_POWER_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Battery state / fuel gauge: see hal_battery.h (MAX17048). */

/** Initialise power management. */
hal_status_t hal_power_init(void);

/** Enter the lowest-power state from which the given wake sources can resume. TODO: wake API. */
void hal_power_sleep(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_POWER_H
