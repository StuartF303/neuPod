// SPDX-License-Identifier: MPL-2.0
/*
 * Input on the Waveshare ESP32-S3-Touch-AMOLED-2.41: no click wheel and no Hold switch. There is one side key (Power, GPIO15, active
 * low) and the touch panel (hal_touch.c; the runtime's input task owns its init and polling, under the I2C mutex). The wheel is
 * reported absent, as on the other touch board: hal_input_init() answers HAL_ENODEV, so the boot check marks it degraded (touch and
 * the console keys do not depend on it).
 */
#include "board.h"
#include "driver/gpio.h"
#include "hal_input.h"

hal_status_t hal_input_init(void) { return HAL_ENODEV; }

hal_status_t hal_input_reinit(void) { return HAL_ENODEV; }

bool hal_input_poll(hal_wheel_state *out) {
    *out = (hal_wheel_state){0}; // valid = false
    return false;
}

bool hal_input_is_simulated(void) { return false; }

// The key is levels, turned into press/release events by the input task. The UI times the Hold gesture itself (ADR-0020).
// A press that was already down at power-on (it is what switched the board on) is ignored until it has been released once: the same
// rule as the RP2350 port, so the boot does not count as a Power hold.
bool hal_input_power_key(void) {
    static bool g_seen_released;
    bool down = gpio_get_level(PIN_KEY_BAT) == KEY_PRESSED_LEVEL;
    if (!down) {
        g_seen_released = true;
    }
    return down && g_seen_released;
}
bool hal_input_has_power_key(void) { return true; }

hal_hold_switch_t hal_input_hold_switch(void) { return HAL_HOLD_SWITCH_NONE; }
