// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_input.h
 * @brief Click wheel and buttons.
 *
 * The iPod 6G click wheel streams 32-bit status frames; the HAL decodes them into
 * hal_wheel_state. See docs/click-wheel-protocol.md.
 */
#ifndef NEUPOD_HAL_INPUT_H
#define NEUPOD_HAL_INPUT_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Button bits in hal_wheel_state.buttons (frame bits 8..12, shifted down by 8). */
enum {
    HAL_BTN_SELECT = 1u << 0,
    HAL_BTN_RIGHT = 1u << 1,
    HAL_BTN_LEFT = 1u << 2,
    HAL_BTN_PLAY = 1u << 3,
    HAL_BTN_MENU = 1u << 4,
};

/** Number of discrete finger positions around the wheel (0..HAL_WHEEL_POSITIONS-1). */
#define HAL_WHEEL_POSITIONS 96

/** Decoded click-wheel state. */
typedef struct {
    uint8_t buttons;  /**< HAL_BTN_* bitmask of buttons currently pressed. */
    uint8_t position; /**< Finger position 0..95; meaningful only when @c touched. */
    bool touched;     /**< Finger is on the wheel. */
    bool valid;       /**< Frame passed the validity check; other fields undefined if false. */
} hal_wheel_state;

/** Start the wheel receiver and send the init packet. */
hal_status_t hal_input_init(void);

/**
 * Fetch the most recent wheel state.
 * @return true if a new valid frame arrived since the previous call; @p out is always written
 *         with the latest known state.
 */
bool hal_input_poll(hal_wheel_state *out);

/** Re-send the wheel init packet (call when Hold is released). */
hal_status_t hal_input_reinit(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_INPUT_H
