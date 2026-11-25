// SPDX-License-Identifier: MPL-2.0
/*
 * Colour tokens and the Light / Dark themes (design review of 12 Nov 2026, DS-1; docs/ui.md "Design system").
 *
 * Every colour the UI draws with is one of 14 tokens. A theme is a table of those 14 colours (RGB565-safe: red and blue are multiples
 * of 8, green of 4, so the panel shows exactly the value written). Screens never hold a hex value of their own: they name a token
 * (UI_C_INK ...) and the shared styles below read the active theme, so swapping the theme restyles every widget at once.
 *
 * A "colour" argument in the UI helpers (ui_label(), ui_obj_bg() ...) is either a token (UI_C_*, follows the theme) or a literal
 * 0xRRGGBB (fixed: the boot splash, the art placeholder, white on a photo).
 */
#ifndef NEUPOD_UI_THEME_H
#define NEUPOD_UI_THEME_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "ui_logic.h" /* ui_theme_mode_t */

typedef enum {
    UI_TOK_BG,          /**< screen background */
    UI_TOK_SURFACE,     /**< cards, input fields, the right pane */
    UI_TOK_BAR,         /**< status bar fill */
    UI_TOK_LINE,        /**< hairlines, progress track, scrollbar track */
    UI_TOK_INK,         /**< primary text, battery fill */
    UI_TOK_INK2,        /**< secondary text, chevrons, volume fill */
    UI_TOK_INK3,        /**< tertiary text, disabled rows, hints */
    UI_TOK_ACCENT,      /**< selection bar, progress fill, focused value */
    UI_TOK_ON_ACCENT,   /**< text on the selection bar */
    UI_TOK_ACCENT_SOFT, /**< pills, info toast */
    UI_TOK_AMBER,       /**< seek mode, warnings */
    UI_TOK_AMBER_SOFT,  /**< warning toast background */
    UI_TOK_GREEN,       /**< charging, streaming OK, signal full */
    UI_TOK_RED,         /**< battery < 15 %, errors */
    UI_TOK_COUNT
} ui_tok_t;

typedef struct {
    uint32_t c[UI_TOK_COUNT]; /**< 0xRRGGBB per token */
} ui_theme_t;

extern const ui_theme_t k_ui_theme_light;
extern const ui_theme_t k_ui_theme_dark;

/** A token as a colour argument; literals are plain 0xRRGGBB (< 0x01000000). */
#define UI_TOKEN_FLAG 0x01000000u
#define UI_C(tok) (UI_TOKEN_FLAG | (uint32_t)(tok))
#define UI_C_BG UI_C(UI_TOK_BG)
#define UI_C_SURFACE UI_C(UI_TOK_SURFACE)
#define UI_C_BAR UI_C(UI_TOK_BAR)
#define UI_C_LINE UI_C(UI_TOK_LINE)
#define UI_C_INK UI_C(UI_TOK_INK)
#define UI_C_INK2 UI_C(UI_TOK_INK2)
#define UI_C_INK3 UI_C(UI_TOK_INK3)
#define UI_C_ACCENT UI_C(UI_TOK_ACCENT)
#define UI_C_ON_ACCENT UI_C(UI_TOK_ON_ACCENT)
#define UI_C_ACCENT_SOFT UI_C(UI_TOK_ACCENT_SOFT)
#define UI_C_AMBER UI_C(UI_TOK_AMBER)
#define UI_C_AMBER_SOFT UI_C(UI_TOK_AMBER_SOFT)
#define UI_C_GREEN UI_C(UI_TOK_GREEN)
#define UI_C_RED UI_C(UI_TOK_RED)

/** The active theme (Light until ui_theme_set()). */
const ui_theme_t *ui_theme(void);
/** Switch themes: every shared style is updated in place and every widget restyled (one full-screen repaint). No-op when unchanged. */
void ui_theme_set(const ui_theme_t *t);
/** Call @p cb whenever the theme changes (styles a module keeps for itself rewrite their colours there). At most 4. */
void ui_theme_subscribe(void (*cb)(void));
/** 0xRRGGBB of a token or literal under the active theme. */
uint32_t ui_rgb(uint32_t colour);
lv_color_t ui_color(uint32_t colour);

/* Shared styles, one per colour and kind (no per-object style memory: LVGL's pool is small). They follow the theme. */
lv_style_t *ui_style_text(uint32_t colour);   /**< text colour */
lv_style_t *ui_style_bg(uint32_t colour);     /**< opaque background colour */
lv_style_t *ui_style_border(uint32_t colour); /**< border colour (width is the object's own) */

/** Set an object's background / border colour through the shared styles (@p part: LV_PART_MAIN, LV_PART_INDICATOR ...). */
void ui_obj_bg(lv_obj_t *o, uint32_t colour, lv_style_selector_t part);
void ui_obj_border(lv_obj_t *o, uint32_t colour, int32_t width, lv_border_side_t side);
/** Change colour from @p from to @p to (what it was last set to); does nothing when they are equal. */
void ui_obj_bg_swap(lv_obj_t *o, uint32_t from, uint32_t to, lv_style_selector_t part);

#endif // NEUPOD_UI_THEME_H
