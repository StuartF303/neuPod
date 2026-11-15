// SPDX-License-Identifier: MPL-2.0
// Boards that cannot suspend (the Pico 2 W, the RP2350 and ESP32-S3 AMOLED dev boards): no sleep, no external-power sensing. The core powers the player off
// after the idle time where hal_system_can_poweroff() allows it (docs/adr/0021-power-sleep.md).
#include "hal_power.h"

hal_status_t hal_power_init(void) { return HAL_OK; }

bool hal_power_can_sleep(void) { return false; }

hal_status_t hal_power_sleep(uint32_t max_ms, hal_wake_reason_t *why) {
    (void)max_ms;
    if (why != NULL) {
        *why = HAL_WAKE_NONE;
    }
    return HAL_ENOTSUP;
}

bool hal_power_external(void) { return false; }
