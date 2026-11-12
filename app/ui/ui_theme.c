// SPDX-License-Identifier: MPL-2.0
/*
 * The two themes and the shared styles that read them (ui_theme.h). One lv_style_t per (kind, colour) pair, created the first time it is asked
 * for and kept for good: a list screen has dozens of labels and every local style property would cost heap in LVGL's pool. A theme switch
 * rewrites the colour inside each token style and asks LVGL to restyle everything; literal colours never change.
 */
#include <string.h>

#include "ui_theme.h"

/* RGB565-safe values from the review (R and B multiples of 8, G of 4). Contrast: ink2 on surface 5.9:1 light / 7.1:1 dark, on_accent on accent 7.3:1. */
const ui_theme_t k_ui_theme_light = {{
    [UI_TOK_BG] = 0xF8F8F8,          [UI_TOK_SURFACE] = 0xFFFFFF,     [UI_TOK_BAR] = 0xE8E8E8,        [UI_TOK_LINE] = 0xE0E0E0,
    [UI_TOK_INK] = 0x181818,         [UI_TOK_INK2] = 0x606060,        [UI_TOK_INK3] = 0xA0A0A0,       [UI_TOK_ACCENT] = 0x1858C8,
    [UI_TOK_ON_ACCENT] = 0xFFFFFF,   [UI_TOK_ACCENT_SOFT] = 0xD8E4F8, [UI_TOK_AMBER] = 0xD08018,      [UI_TOK_AMBER_SOFT] = 0xF8E8C8,
    [UI_TOK_GREEN] = 0x209858,       [UI_TOK_RED] = 0xD03028,
}};

const ui_theme_t k_ui_theme_dark = {{
    [UI_TOK_BG] = 0x101010,          [UI_TOK_SURFACE] = 0x181818,     [UI_TOK_BAR] = 0x202020,        [UI_TOK_LINE] = 0x303030,
    [UI_TOK_INK] = 0xF0F0F0,         [UI_TOK_INK2] = 0xA0A0A0,        [UI_TOK_INK3] = 0x686868,       [UI_TOK_ACCENT] = 0x3078E8,
    [UI_TOK_ON_ACCENT] = 0xFFFFFF,   [UI_TOK_ACCENT_SOFT] = 0x203050, [UI_TOK_AMBER] = 0xE89828,      [UI_TOK_AMBER_SOFT] = 0x483818,
    [UI_TOK_GREEN] = 0x30B870,       [UI_TOK_RED] = 0xE84840,
}};

static const ui_theme_t *g_theme = &k_ui_theme_light;

const ui_theme_t *ui_theme(void) { return g_theme; }

uint32_t ui_rgb(uint32_t colour) {
    if ((colour & UI_TOKEN_FLAG) == 0) {
        return colour;
    }
    uint32_t tok = colour & 0xFFu;
    return g_theme->c[tok < UI_TOK_COUNT ? tok : UI_TOK_INK];
}

lv_color_t ui_color(uint32_t colour) { return lv_color_hex(ui_rgb(colour)); }

typedef enum { K_TEXT, K_BG, K_BORDER, K_KINDS } kind_t;
/* 14 tokens, plus the literals in use (white on art, the splash palette, the seek track ...): 8 spare per kind. */
#define SLOTS (UI_TOK_COUNT + 12)

typedef struct {
    uint32_t colour;
    lv_style_t style;
} cs_t;

static cs_t g_cs[K_KINDS][SLOTS];
static uint8_t g_n[K_KINDS];

static void cs_apply(kind_t k, cs_t *c) {
    lv_color_t col = ui_color(c->colour);
    switch (k) {
    case K_TEXT: lv_style_set_text_color(&c->style, col); break;
    case K_BG:
        lv_style_set_bg_color(&c->style, col);
        lv_style_set_bg_opa(&c->style, LV_OPA_COVER);
        break;
    default: lv_style_set_border_color(&c->style, col); break;
    }
}

static lv_style_t *cs_get(kind_t k, uint32_t colour) {
    for (uint8_t i = 0; i < g_n[k]; i++) {
        if (g_cs[k][i].colour == colour) {
            return &g_cs[k][i].style;
        }
    }
    if (g_n[k] >= SLOTS) {
        return &g_cs[k][0].style; /* out of slots (cannot happen with the palette in use): a wrong colour beats a crash */
    }
    cs_t *c = &g_cs[k][g_n[k]++];
    c->colour = colour;
    lv_style_init(&c->style);
    cs_apply(k, c);
    return &c->style;
}

lv_style_t *ui_style_text(uint32_t colour) { return cs_get(K_TEXT, colour); }
lv_style_t *ui_style_bg(uint32_t colour) { return cs_get(K_BG, colour); }
lv_style_t *ui_style_border(uint32_t colour) { return cs_get(K_BORDER, colour); }

#define MAX_SUBS 4
static void (*g_subs[MAX_SUBS])(void);
static uint8_t g_nsubs;

void ui_theme_subscribe(void (*cb)(void)) {
    if (g_nsubs < MAX_SUBS) {
        g_subs[g_nsubs++] = cb;
    }
}

void ui_theme_set(const ui_theme_t *t) {
    if (t == g_theme) {
        return;
    }
    g_theme = t;
    for (int k = 0; k < K_KINDS; k++) {
        for (uint8_t i = 0; i < g_n[k]; i++) {
            if ((g_cs[k][i].colour & UI_TOKEN_FLAG) != 0) {
                cs_apply((kind_t)k, &g_cs[k][i]);
            }
        }
    }
    for (uint8_t i = 0; i < g_nsubs; i++) {
        g_subs[i]();
    }
    lv_obj_report_style_change(NULL); /* every object: its styles changed under it */
}

void ui_obj_bg(lv_obj_t *o, uint32_t colour, lv_style_selector_t part) { lv_obj_add_style(o, ui_style_bg(colour), part); }

void ui_obj_border(lv_obj_t *o, uint32_t colour, int32_t width, lv_border_side_t side) {
    lv_obj_add_style(o, ui_style_border(colour), 0);
    lv_obj_set_style_border_width(o, width, 0);
    lv_obj_set_style_border_side(o, side, 0);
}

void ui_obj_bg_swap(lv_obj_t *o, uint32_t from, uint32_t to, lv_style_selector_t part) {
    if (from == to) {
        return;
    }
    lv_obj_remove_style(o, ui_style_bg(from), part);
    lv_obj_add_style(o, ui_style_bg(to), part);
}
