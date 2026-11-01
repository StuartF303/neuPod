// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_display.h
 * @brief Pixel display: RGB565 window + blit, backlight.
 *
 * The model is a controller-side frame memory (ST7789-style): set a window, then stream
 * pixels into it row-major. Phase 1 panel is 320x240 landscape.
 */
#ifndef NEUPOD_HAL_DISPLAY_H
#define NEUPOD_HAL_DISPLAY_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One pixel, RGB565, native endianness (the driver handles bus byte order). */
typedef uint16_t hal_pixel_t;

/** Pack 8-bit-per-channel RGB into RGB565. */
#define HAL_RGB565(r, g, b)                                                                        \
    ((hal_pixel_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/** Initialise the panel (reset, init sequence, orientation) and clear it to black. */
hal_status_t hal_display_init(void);

/** Panel width in pixels in the current orientation. */
uint16_t hal_display_width(void);

/** Panel height in pixels in the current orientation. */
uint16_t hal_display_height(void);

/**
 * Set the write window to the inclusive-exclusive rectangle [x, x+w) x [y, y+h).
 * Waits for any in-flight blit to finish first.
 */
hal_status_t hal_display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/**
 * Stream @p count pixels into the current window. May return before the transfer completes
 * (DMA); @p pixels must stay valid until hal_display_wait() returns.
 */
hal_status_t hal_display_blit(const hal_pixel_t *pixels, size_t count);

/** Block until any in-flight blit has completed. */
void hal_display_wait(void);

/** Present the frame (no-op on panels with their own frame memory; flips the host window). */
void hal_display_present(void);

/** Backlight level, 0 (off) to 255 (full). */
void hal_display_backlight(uint8_t level);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_DISPLAY_H
