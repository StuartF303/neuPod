// SPDX-License-Identifier: MPL-2.0
/*
 * Fast-scroll letter overlay (LB-2, docs/ui.md "Fast scroll"): while the wheel spins a long sorted list a dark tile at the top right shows the
 * letter of the row under the selection, and an index rail down the right edge shows where that letter sits in A to Z (every second letter, the
 * current one in the ink colour at any position). It never covers the selection bar: the tile sits above the selected row's usual place and the
 * rail is a column at the edge. It is shown from the moment the wheel passes the fast-scroll threshold (wi_letterjump_t), stays while the wheel
 * moves and fades out in 150 ms once it has rested for WI_LJ_HIDE_MS (250 ms). The Library lists and Files both use it.
 *
 * Only the tile's and the rail's own areas are redrawn: every object fades through its own opacity (no layer over the list).
 */
#include <string.h>

#include "ui_input.h"
#include "ui_internal.h"

#define RAIL_LETTERS 26
#define RAIL_SHOWN 13 /* A, C, E ... Y: every second letter (both profiles: the 16 px font does not fit 26 letters in the 306 px rail either) */
#define FADE_MS 150

struct ui_letter_overlay {
    lv_obj_t *tile, *tile_lbl;
    lv_obj_t *rail[RAIL_SHOWN]; /* the letters A, C, E ... */
    lv_obj_t *cur;              /* the current letter, in ink, at its own position */
    char shown;                 /* the letter drawn in the tile */
    bool visible, fading;
    lv_opa_t opa;
};

static bool g_tour_hold; /* tours: keep the overlay up so a snapshot can show it (the script steps are slower than the hide delay) */

void ui_letter_overlay_tour_hold(bool on) { g_tour_hold = on; }

static int32_t rail_y(int letter) {
    int32_t line = lv_font_get_line_height(UIM->font_small);
    return UIM->lr_rail_y + (UIM->lr_rail_h - line) * letter / (RAIL_LETTERS - 1);
}

static void apply_opa(ui_letter_overlay_t *o, lv_opa_t v) {
    o->opa = v;
    lv_obj_set_style_bg_opa(o->tile, v, 0);
    lv_obj_set_style_text_opa(o->tile_lbl, v, 0);
    for (int i = 0; i < RAIL_SHOWN; i++) {
        lv_obj_set_style_text_opa(o->rail[i], v, 0);
    }
    lv_obj_set_style_text_opa(o->cur, v, 0);
}

static void fade_cb(void *var, int32_t v) { apply_opa(var, (lv_opa_t)v); }

static void fade_done(lv_anim_t *a) {
    ui_letter_overlay_t *o = a->var;
    o->fading = false;
    o->visible = false;
    lv_obj_set_hidden(o->tile, true);
    lv_obj_set_hidden(o->cur, true);
    for (int i = 0; i < RAIL_SHOWN; i++) {
        lv_obj_set_hidden(o->rail[i], true);
    }
}

static void set_hidden_all(ui_letter_overlay_t *o, bool hidden) {
    lv_obj_set_hidden(o->tile, hidden);
    for (int i = 0; i < RAIL_SHOWN; i++) {
        lv_obj_set_hidden(o->rail[i], hidden);
    }
    lv_obj_set_hidden(o->cur, hidden || o->shown < 'A' || o->shown > 'Z');
}

ui_letter_overlay_t *ui_letter_overlay_create(void *parent) {
    ui_letter_overlay_t *o = lv_malloc(sizeof *o);
    if (o == NULL) {
        return NULL;
    }
    memset(o, 0, sizeof *o);
    o->opa = LV_OPA_COVER;
    int32_t tile = UIM->lr_tile;
    o->tile = lv_obj_create(parent);
    lv_obj_remove_style_all(o->tile);
    lv_obj_set_size(o->tile, tile, tile);
    lv_obj_set_pos(o->tile, UI_W - tile - UIM->lr_tile_inset, UIM->lr_tile_y);
    ui_obj_bg(o->tile, UI_C_INK, 0);
    lv_obj_set_style_radius(o->tile, UIM->lr_tile_radius, 0);
    o->tile_lbl = ui_label(o->tile, "A", UI_C_SURFACE);
    lv_obj_set_style_text_font(o->tile_lbl, UIM->font_huge, 0);
    /* the 48 px font's line box (52) is taller than the 44 px tile: a clipping label as tall as the tile shows the letter (its capital spans rows 8 to 43) */
    lv_label_set_long_mode(o->tile_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(o->tile_lbl, tile, tile);
    lv_obj_set_style_text_align(o->tile_lbl, LV_TEXT_ALIGN_CENTER, 0);
    int32_t pad = (tile - lv_font_get_line_height(UIM->font_huge)) / 2;
    lv_obj_set_style_pad_top(o->tile_lbl, pad > 0 ? pad : 0, 0);
    lv_obj_set_pos(o->tile_lbl, 0, 0);
    for (int i = 0; i < RAIL_SHOWN; i++) {
        char b[2] = {(char)('A' + 2 * i), 0};
        o->rail[i] = ui_label_small(parent, b, UI_C_INK3);
        lv_obj_align(o->rail[i], LV_ALIGN_TOP_RIGHT, -UIM->lr_rail_inset, rail_y(2 * i));
    }
    o->cur = ui_label_small(parent, "A", UI_C_INK);
    lv_obj_align(o->cur, LV_ALIGN_TOP_RIGHT, -UIM->lr_rail_inset, rail_y(0));
    set_hidden_all(o, true);
    return o;
}

// Show the letter @p c: the tile, and the rail with the current letter in ink (the letters next to it make room).
static void show_letter(ui_letter_overlay_t *o, char c) {
    o->shown = c;
    char b[2] = {c, 0};
    lv_label_set_text(o->tile_lbl, b);
    bool letter = c >= 'A' && c <= 'Z';
    lv_obj_set_hidden(o->cur, !letter);
    int idx = letter ? c - 'A' : -9;
    if (letter) {
        lv_label_set_text(o->cur, b);
        lv_obj_align(o->cur, LV_ALIGN_TOP_RIGHT, -UIM->lr_rail_inset, rail_y(idx));
    }
    for (int i = 0; i < RAIL_SHOWN; i++) {
        int at = 2 * i;
        lv_obj_set_hidden(o->rail[i], at >= idx - 1 && at <= idx + 1);
    }
}

void ui_letter_overlay_update(ui_letter_overlay_t *o, const wi_letterjump_t *j) {
    if (o == NULL) {
        return;
    }
    bool active = j->active || g_tour_hold;
    if (active) {
        if (o->fading) { /* the wheel moved again: stop the fade, show it at full strength */
            lv_anim_delete(o, fade_cb);
            o->fading = false;
        }
        if (!o->visible || o->opa != LV_OPA_COVER) {
            o->visible = true;
            apply_opa(o, LV_OPA_COVER);
            o->shown = 0; /* redraw the letter below */
            set_hidden_all(o, false);
        }
        if (j->letter != o->shown) {
            show_letter(o, j->letter);
        }
    } else if (o->visible && !o->fading) {
        o->fading = true;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, o);
        lv_anim_set_exec_cb(&a, fade_cb);
        lv_anim_set_values(&a, o->opa, 0);
        lv_anim_set_duration(&a, FADE_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_completed_cb(&a, fade_done);
        lv_anim_start(&a);
    }
}

void ui_letter_overlay_destroy(ui_letter_overlay_t *o) {
    if (o != NULL) {
        lv_anim_delete(o, fade_cb);
        lv_obj_delete(o->tile);
        lv_obj_delete(o->cur);
        for (int i = 0; i < RAIL_SHOWN; i++) {
            lv_obj_delete(o->rail[i]);
        }
        lv_free(o);
    }
}
