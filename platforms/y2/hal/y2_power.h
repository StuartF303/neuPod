// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_power.h
 * @brief Battery state from /sys/class/power_supply (the Y2 has no fuel-gauge chip we talk to). Pure parsing
 *        over a directory argument, unit-tested against a fake tree.
 *
 * The MediaTek battery node reports capacity (0..100), batt_vol (mV; a standard kernel would say voltage_now
 * in microvolts, both are accepted) and status. It has no charge-rate reading, so the rate in the result is
 * synthetic: +/- Y2_POWER_RATE_MPCT_PER_H while charging/discharging, 0 otherwise. The UI only compares it
 * against +/-2000 to pick "charging" / "discharging" / "idle".
 */
#ifndef NEUPOD_Y2_POWER_H
#define NEUPOD_Y2_POWER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal_battery.h"

#ifdef __cplusplus
extern "C" {
#endif

#define Y2_POWER_RATE_MPCT_PER_H 10000

typedef enum {
    Y2_PS_UNKNOWN = 0,
    Y2_PS_CHARGING,
    Y2_PS_DISCHARGING,
    Y2_PS_FULL,
    Y2_PS_NOT_CHARGING,
} y2_power_status_t;

/** Map the sysfs status text ("Charging", "Discharging", "Full", "Not charging"). */
y2_power_status_t y2_power_parse_status(const char *text);

/** Normalise a voltage reading to millivolts: values of 100000 and above are microvolts. */
uint32_t y2_power_to_mv(long raw);

/**
 * Find the battery under @p ps_root (the first supply whose "type" is "Battery") and fill @p out and @p status
 * (may be NULL). Returns false (out->valid false) if there is no battery supply or capacity/voltage cannot be read.
 */
bool y2_power_read(const char *ps_root, hal_battery_status_t *out, y2_power_status_t *status);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_POWER_H
