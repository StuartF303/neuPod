// SPDX-License-Identifier: MPL-2.0
/**
 * @file gfx_font.h
 * @brief Bitmap font description + the two embedded fonts (ASCII 32..126).
 *
 * Glyph data: @c height rows per glyph, one uint16_t per row, bit 15 = leftmost pixel. Sources
 * and licences: app/gfx/fonts/README.md.
 */
#ifndef NEUPOD_GFX_FONT_H
#define NEUPOD_GFX_FONT_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t height;        /**< Line height in pixels (cell height; use as line advance). */
    uint8_t ascent;        /**< Rows above the baseline (baseline y = top + ascent). */
    uint8_t first, last;   /**< Inclusive ASCII range covered. */
    uint8_t fixed_w;       /**< Advance for every glyph when @c widths is NULL. */
    const uint8_t *widths; /**< Per-glyph advance (last-first+1 entries), or NULL if fixed. */
    const uint16_t *rows;  /**< (last-first+1) * height row words. */
} gfx_font_t;

/** 8x8 fixed font (public domain font8x8). */
extern const gfx_font_t gfx_font_8x8;
/** Proportional 15 px font rendered from DejaVu Sans. */
extern const gfx_font_t gfx_font_16;

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_GFX_FONT_H
