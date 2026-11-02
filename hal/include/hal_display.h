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
 * (DMA); @p pixels must stay valid until hal_display_wait() returns. Writing more than w*h
 * pixels in total wraps back to the window origin (as on the controller).
 */
hal_status_t hal_display_blit(const hal_pixel_t *pixels, size_t count);

/** Block until any in-flight blit has completed. */
void hal_display_wait(void);

/** Present the frame (no-op on panels with their own frame memory; flips the host window). */
void hal_display_present(void);

/**
 * Fill the inclusive-exclusive rectangle [x, x+w) x [y, y+h) with one colour. Needs no pixel
 * buffer (RP2350: DMA with a non-incrementing read from a single pixel). Blocks until done.
 * Leaves the write window set to the rectangle. Returns HAL_EINVAL if it does not fit the panel.
 */
hal_status_t hal_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                   hal_pixel_t colour);

/**
 * Enter (true) or leave (false) panel sleep. Sleeping sends DISPOFF then SLPIN; waking sends
 * SLPOUT then DISPON, honouring the datasheet gaps (>= 5 ms after SLPIN, 120 ms between SLPIN
 * and SLPOUT, 120 ms after SLPOUT), so it blocks for up to ~125 ms. Frame memory is retained.
 * The backlight is NOT touched: the caller turns it off/on. After waking, set a window before
 * writing pixels. On the host the window is blanked while asleep.
 */
void hal_display_sleep(bool sleep);

/**
 * Service the display's event loop; call regularly from the task that draws. Hardware: no-op.
 * Host: pumps the simulator window's events (and exits cleanly when it is closed) on the one
 * thread that owns the window, which keeps SDL single-threaded.
 */
void hal_display_service(void);

/** Backlight level, 0 (off) to 255 (full). Active-high PWM on the Phase 1 testbed. */
void hal_display_backlight(uint8_t level);

/*
 * Bring-up hooks: let a test menu find the right panel settings on the bench. Call only after
 * hal_display_init(). Each re-sends the command, so set a window again before the next write.
 */

/** MADCTL (0x36) bits: 0x80 MY, 0x40 MX, 0x20 MV, 0x10 ML, 0x08 BGR, 0x04 MH. */
#define HAL_MADCTL_MY 0x80u
#define HAL_MADCTL_MX 0x40u
#define HAL_MADCTL_MV 0x20u
#define HAL_MADCTL_BGR 0x08u

/**
 * Write MADCTL (orientation: MY/MX/MV; colour order: BGR bit). hal_display_width()/height()
 * keep reporting 320x240, so only values with MV set (landscape) are meaningful. The host
 * stores the value and emulates the BGR bit (red/blue swap) but not the mirroring bits.
 */
void hal_display_set_madctl(uint8_t madctl);

/** Last MADCTL value written (the init default until changed). */
uint8_t hal_display_get_madctl(void);

/**
 * Inversion on (INVON, true) or off (INVOFF). The Phase 1 panel NEEDS inversion on (the init
 * default); turning it off shows inverted colours. The host emulates this.
 */
void hal_display_set_inversion(bool on);

/** Current inversion state. */
bool hal_display_get_inversion(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_DISPLAY_H
