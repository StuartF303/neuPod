// SPDX-License-Identifier: MPL-2.0
/*
 * Input on the Waveshare RP2350-Touch-AMOLED-2.41: no click wheel. The runtime copes with that by itself: hal_input_init() reports
 * the receiver as absent, the boot check marks the wheel capability unavailable, and the input task never forwards wheel frames.
 * The one hardware key is the board's Power button (KEY_BAT, GPIO24, active low). BOOT and RESET are not readable by the firmware.
 */
#include "board.h"
#include "hal_input.h"
#include "hardware/gpio.h"

static bool g_key_ready;
static bool g_seen_released; // the press that switched the board on must not count as a Power hold

void hal_input_power_key_setup(void) {
    if (!g_key_ready) {
        gpio_init(BOARD_PIN_KEY_BAT);
        gpio_set_dir(BOARD_PIN_KEY_BAT, GPIO_IN);
        gpio_pull_up(BOARD_PIN_KEY_BAT);
        g_key_ready = true;
    }
}

hal_status_t hal_input_init(void) {
    hal_input_power_key_setup();
    return HAL_ENODEV; // no wheel receiver on this board
}

bool hal_input_poll(hal_wheel_state *out) {
    if (out != NULL) {
        *out = (hal_wheel_state){0}; // valid = false: no frame
    }
    return false;
}

bool hal_input_is_simulated(void) { return false; }

hal_status_t hal_input_reinit(void) { return HAL_ENODEV; }

bool hal_input_power_key(void) {
    hal_input_power_key_setup();
    bool pressed = !gpio_get(BOARD_PIN_KEY_BAT); // pressed pulls the line low
    if (!pressed) {
        g_seen_released = true;
    }
    return pressed && g_seen_released;
}

bool hal_input_has_power_key(void) { return true; }

hal_hold_switch_t hal_input_hold_switch(void) { return HAL_HOLD_SWITCH_NONE; }
