// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 battery: implements hal_battery.h from /sys/class/power_supply (parsing in y2_power.c, unit-tested). There is
 * no MAX17048 on the Y2, so the gauge-register calls (version, config, hibernate, alerts, quick start, reset) report
 * HAL_ENOTSUP. This file is linked into the executable ahead of the neupod_drivers library, so the MAX17048
 * driver's definitions of the same functions are never pulled in.
 */
#include "hal_battery.h"
#include "y2_hal.h"
#include "y2_power.h"

hal_status_t hal_battery_init(void) {
    hal_battery_status_t st;
    return y2_power_read(Y2_POWER_SUPPLY, &st, NULL) ? HAL_OK : HAL_ENODEV;
}

hal_status_t hal_battery_get_version(uint16_t *version) {
    (void)version;
    return HAL_ENOTSUP;
}

hal_status_t hal_battery_get_config(uint16_t *config) {
    (void)config;
    return HAL_ENOTSUP;
}

hal_status_t hal_battery_get_hibrt(uint16_t *hibrt) {
    (void)hibrt;
    return HAL_ENOTSUP;
}

hal_status_t hal_battery_read(hal_battery_status_t *out) {
    if (out == NULL) {
        return HAL_EINVAL;
    }
    return y2_power_read(Y2_POWER_SUPPLY, out, NULL) ? HAL_OK : HAL_ENODEV;
}

hal_status_t hal_battery_set_alert_threshold(uint8_t pct) {
    (void)pct;
    return HAL_ENOTSUP;
}

hal_status_t hal_battery_clear_alert(void) { return HAL_ENOTSUP; }
hal_status_t hal_battery_quick_start(void) { return HAL_ENOTSUP; }

hal_status_t hal_battery_set_hibernate(uint32_t hib_mpct_per_h, uint32_t act_mv) {
    (void)hib_mpct_per_h;
    (void)act_mv;
    return HAL_ENOTSUP;
}

hal_status_t hal_battery_reset(void) { return HAL_ENOTSUP; }
