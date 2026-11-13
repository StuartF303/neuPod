// SPDX-License-Identifier: MPL-2.0
/*
 * Full-pane states (design review 3.3, LB-4): "Building library", "No music yet", "Library needs a writable card", "Library updated".
 * A centred column (heading, up to two Small lines, a bar with its caption, an accent button) and a hint line at the bottom, instead of
 * fake list rows. The button is not a focus target: it names what Select does.
 */
#include <string.h>

#include "ui_internal.h"

struct ui_pane {
    lv_obj_t *col;                         /* the centred flex column (full content area, transparent) */
    lv_obj_t *head, *l1, *l2;              /* heading (Title font), Small lines */
    lv_obj_t *track, *fill, *caption;      /* the bar and what it counts */
    lv_obj_t *btn, *btn_l;                 /* the accent pill */
    lv_obj_t *hint;                        /* Small, ink3, centred at the bottom (outside the column) */
    int32_t pct;
    uint32_t sweep_ms;
    uint32_t sweep_pos;
};

static void show(lv_obj_t *o, bool on) {
    if (lv_obj_is_hidden(o) == on) {
        lv_obj_set_hidden(o, !on);
    }
}

static lv_obj_t *wrap_label(lv_obj_t *parent, bool small, uint32_t colour, const lv_font_t *font) {
    lv_obj_t *l = small ? ui_label_small(parent, "", colour) : ui_label(parent, "", colour);
    if (font != NULL) {
        lv_obj_set_style_text_font(l, font, 0);
    }
    lv_obj_set_width(l, UI_W - 2 * UIM->pad);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    show(l, false);
    return l;
}

ui_pane_t *ui_pane_create(lv_obj_t *parent) {
    ui_pane_t *p = lv_malloc(sizeof *p);
    if (p == NULL) {
        return NULL;
    }
    memset(p, 0, sizeof *p);
    p->pct = UI_PANE_BAR_NONE;
    p->col = lv_obj_create(parent);
    lv_obj_remove_style_all(p->col);
    lv_obj_set_size(p->col, UI_W, UI_CONTENT_H);
    lv_obj_set_pos(p->col, 0, 0);
    lv_obj_set_scrollable(p->col, false);
    lv_obj_set_flex_flow(p->col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p->col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p->col, UIM->st_gap, 0);
    lv_obj_set_style_pad_bottom(p->col, UIM->st_hint_inset, 0); /* the block sits a little above the middle, clear of the hint line */

    p->head = wrap_label(p->col, false, UI_C_INK, UIM->font_large);
    p->l1 = wrap_label(p->col, true, UI_C_INK2, NULL);
    p->l2 = wrap_label(p->col, true, UI_C_INK2, NULL);

    p->track = lv_obj_create(p->col);
    lv_obj_remove_style_all(p->track);
    lv_obj_set_size(p->track, UIM->st_bar_w, UIM->st_bar_h);
    lv_obj_set_scrollable(p->track, false);
    lv_obj_set_style_radius(p->track, UIM->st_bar_h / 2, 0);
    lv_obj_set_style_clip_corner(p->track, true, 0);
    ui_obj_bg(p->track, UI_C_LINE, 0);
    p->fill = lv_obj_create(p->track);
    lv_obj_remove_style_all(p->fill);
    lv_obj_set_size(p->fill, 1, UIM->st_bar_h);
    lv_obj_set_pos(p->fill, 0, 0);
    ui_obj_bg(p->fill, UI_C_ACCENT, 0);
    show(p->track, false);

    p->caption = ui_label_small(p->col, "", UI_C_INK2);
    lv_obj_set_width(p->caption, UIM->st_bar_w + 2 * UIM->pad);
    lv_label_set_long_mode(p->caption, LV_LABEL_LONG_DOT);
    lv_obj_set_height(p->caption, lv_font_get_line_height(UIM->font_small));
    lv_obj_set_style_text_align(p->caption, LV_TEXT_ALIGN_CENTER, 0);
    show(p->caption, false);

    p->btn = lv_obj_create(p->col);
    lv_obj_remove_style_all(p->btn);
    lv_obj_set_height(p->btn, UIM->st_btn_h);
    lv_obj_set_width(p->btn, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(p->btn, false);
    lv_obj_set_style_radius(p->btn, UIM->st_btn_h / 2, 0);
    lv_obj_set_style_pad_hor(p->btn, UIM->st_btn_pad, 0);
    lv_obj_set_flex_flow(p->btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p->btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_obj_bg(p->btn, UI_C_ACCENT, 0);
    p->btn_l = ui_label(p->btn, "", UI_C_ON_ACCENT);
    show(p->btn, false);

    p->hint = ui_label_small(parent, "", UI_C_INK3);
    lv_obj_set_width(p->hint, UI_W - 2 * UIM->pad);
    lv_label_set_long_mode(p->hint, LV_LABEL_LONG_DOT);
    lv_obj_set_height(p->hint, lv_font_get_line_height(UIM->font_small));
    lv_obj_set_style_text_align(p->hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(p->hint, UIM->pad, UI_CONTENT_H - UIM->st_hint_inset - lv_font_get_line_height(UIM->font_small) / 2);
    show(p->hint, false);
    return p;
}

void ui_pane_destroy(ui_pane_t *p) {
    if (p == NULL) {
        return;
    }
    lv_obj_delete(p->hint);
    lv_obj_delete(p->col);
    lv_free(p);
}

static void put(lv_obj_t *l, const char *text) {
    bool on = text != NULL && text[0] != '\0';
    if (on) {
        ui_label_set_text(l, text);
    }
    show(l, on);
}

void ui_pane_set(ui_pane_t *p, const char *heading, const char *l1, const char *l2, const char *button, const char *hint) {
    if (p == NULL) {
        return;
    }
    put(p->head, heading);
    put(p->l1, l1);
    put(p->l2, l2);
    put(p->btn_l, button);
    show(p->btn, button != NULL && button[0] != '\0');
    put(p->hint, hint);
}

void ui_pane_set_bar(ui_pane_t *p, int32_t pct, const char *caption) {
    if (p == NULL) {
        return;
    }
    show(p->track, pct != UI_PANE_BAR_NONE);
    put(p->caption, pct != UI_PANE_BAR_NONE ? caption : NULL);
    if (pct == p->pct && pct >= 0 && pct <= 100) {
        return;
    }
    p->pct = pct;
    if (pct >= 0) {
        int32_t w = UIM->st_bar_w * (pct > 100 ? 100 : pct) / 100;
        lv_obj_set_pos(p->fill, 0, 0);
        lv_obj_set_width(p->fill, w < 1 ? 1 : w);
    } else if (pct == UI_PANE_BAR_SWEEP) {
        lv_obj_set_width(p->fill, UIM->st_bar_w / 4);
    }
}

void ui_pane_show(ui_pane_t *p, bool on) {
    if (p == NULL) {
        return;
    }
    show(p->col, on);
    if (!on) {
        show(p->hint, false);
    }
}

void ui_pane_tick(ui_pane_t *p, uint32_t now_ms) {
    if (p == NULL || p->pct != UI_PANE_BAR_SWEEP || now_ms - p->sweep_ms < 120u) {
        return;
    }
    p->sweep_ms = now_ms;
    const int32_t seg = UIM->st_bar_w / 4;
    p->sweep_pos = (p->sweep_pos + 1u) % 16u;
    lv_obj_set_x(p->fill, (int32_t)p->sweep_pos * (UIM->st_bar_w + seg) / 16 - seg);
}
