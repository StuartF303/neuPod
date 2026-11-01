// SPDX-License-Identifier: MPL-2.0
/**
 * @file host_internal.h
 * @brief Private glue between the host HAL modules (window/event pump <-> display/input/system).
 */
#ifndef NEUPOD_HOST_INTERNAL_H
#define NEUPOD_HOST_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

/** Open the simulator window (w x h logical pixels, shown at 2x). False if unavailable. */
bool host_window_open(int w, int h);

/** Upload an RGB565 framebuffer to the window. No-op when there is no window. */
void host_window_present(const uint16_t *fb, int w, int h, uint8_t backlight);

/**
 * Process pending window/keyboard/mouse events. Feeds host_input_* below and exits the process
 * cleanly (exit(0)) when the window is closed. No-op without a window.
 */
void host_pump_events(void);

/** Input injection, called from the event pump. @p btn is one HAL_BTN_* bit. */
void host_input_button(uint8_t btn, bool down);
/** Move the finger by @p steps wheel positions (sign = direction) and mark it touched. */
void host_input_scroll(int steps);

#endif // NEUPOD_HOST_INTERNAL_H
