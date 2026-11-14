// SPDX-License-Identifier: MPL-2.0
/*
 * Input on the Waveshare ESP32-S3-Touch-AMOLED-2.41: no click wheel and no Hold switch. There is one side key (Power, GPIO15, active
 * low) and the touch panel (hal_touch.c, read by the input task). The wheel is a simulation as far as the runtime is concerned
 * (hal_input_is_simulated(), the Y2's arrangement): it is always a live source, never fails the boot check, and reports one
 * valid idle frame at start (no buttons, no finger) so the check can pass; every later poll has nothing new.
 */
#include <stdio.h>

#include "board.h"
#include "driver/gpio.h"
#include "hal_input.h"
#include "hal_touch.h"

static bool g_idle_sent;

hal_status_t hal_input_init(void) {
    g_idle_sent = false;
    // The touch controller is this board's real pointer: reset and identify it here, at boot, so its state is in the boot log
    // (hal_touch_init() is idempotent; the input task that reads it may call it again). A missing controller does not fail the
    // wheel check: the side key and the console still work.
    (void)hal_touch_init();
    printf("input: power key (GPIO%d) reads %d, pressed level %d\n", PIN_KEY_BAT, gpio_get_level(PIN_KEY_BAT), KEY_PRESSED_LEVEL);
    return HAL_OK;
}

hal_status_t hal_input_reinit(void) { return HAL_OK; }

bool hal_input_poll(hal_wheel_state *out) {
    *out = (hal_wheel_state){.buttons = 0, .position = 0, .touched = false, .valid = true};
    if (g_idle_sent) {
        return false;
    }
    g_idle_sent = true;
    return true;
}

bool hal_input_is_simulated(void) { return true; }

// The key is levels, turned into press/release events by the input task. The UI times the Hold gesture itself (ADR-0020).
bool hal_input_power_key(void) { return gpio_get_level(PIN_KEY_BAT) == KEY_PRESSED_LEVEL; }
bool hal_input_has_power_key(void) { return true; }

hal_hold_switch_t hal_input_hold_switch(void) { return HAL_HOLD_SWITCH_NONE; }
