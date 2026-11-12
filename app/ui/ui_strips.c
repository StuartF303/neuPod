// SPDX-License-Identifier: MPL-2.0
/*
 * The feedback strips of the design system (DS-5, HL-1; docs/ui.md "Design system"): the toast strip under the status bar, the HUD strip at the
 * bottom, the lock pill and the Power-hold progress line. All are small areas that slide or change size, never a full-screen blend, so
 * LVGL's partial redraw only repaints the strip itself. They are children of the frame's screen, created once by ui_strips_init() and
 * shown or hidden afterwards; the toast and HUD slide with the motion tokens of the review (toast in 150 ms / out 200 ms, HUD in 120 ms,
 * hold 1200 ms, out 200 ms). The toast sits under the status bar in stacking order, so it slides down from beneath it.
 */
#include <string.h>

#include "os.h"
#include "ui_internal.h"
#include "ui_strips.h"

#define TOAST_IN_MS 150
#define TOAST_OUT_MS 200
#define HUD_IN_MS 120
#define HUD_OUT_MS 200
#define HUD_HOLD_MS 1200u
#define HUD_BAR_ANIM_MS 60

static lv_obj_t *g_scr;
static lv_obj_t *g_toast, *g_toast_lbl;
static uint32_t g_toast_until; /* 0 = none showing */
static uint32_t g_toast_col = UI_C_AMBER_SOFT;
static bool g_toast_up;        /* in (or sliding in); false = hidden or sliding out */
static lv_obj_t *g_hud, *g_hud_lbl, *g_hud_bar;
static uint32_t g_hud_until;
static bool g_hud_up;
static lv_obj_t *g_pill, *g_pill_lbl;
static lv_obj_t *g_line;
static uint32_t g_line_col = UI_C_ACCENT;

static void set_y(void *o, int32_t y) { lv_obj_set_y(o, y); }

static void slide(lv_obj_t *o, int32_t to_y, int32_t ms, lv_anim_path_cb_t path, lv_anim_completed_cb_t done) {
    lv_anim_delete(o, set_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, set_y);
    lv_anim_set_values(&a, lv_obj_get_y(o), to_y);
    lv_anim_set_duration(&a, (uint32_t)ms);
    lv_anim_set_path_cb(&a, path);
    if (done != NULL) {
        lv_anim_set_completed_cb(&a, done);
    }
    lv_anim_start(&a);
}

/* ---- toast ---- */

static int32_t toast_hidden_y(void) { return UI_STATUS_H - UIM->toast_h; }

static void toast_gone(lv_anim_t *a) {
    (void)a;
    if (!g_toast_up) {
        lv_obj_set_hidden(g_toast, true);
    }
}

void ui_toast_show(const char *text, ui_toast_kind_t kind, uint32_t ms) {
    if (g_toast == NULL) {
        return;
    }
    uint32_t col = kind == UI_TOAST_INFO ? UI_C_ACCENT_SOFT : UI_C_AMBER_SOFT;
    ui_obj_bg_swap(g_toast, g_toast_col, col, 0);
    g_toast_col = col;
    ui_label_set_text(g_toast_lbl, text);
    bool was_up = g_toast_up;
    g_toast_up = true;
    g_toast_until = os_tick_ms() + (ms != 0 ? ms : UI_TOAST_MS);
    if (g_toast_until == 0) {
        g_toast_until = 1;
    }
    lv_obj_set_hidden(g_toast, false);
    if (!was_up) {
        lv_obj_set_y(g_toast, toast_hidden_y());
    }
    if (lv_obj_get_y(g_toast) != UI_STATUS_H) {
        slide(g_toast, UI_STATUS_H, TOAST_IN_MS, lv_anim_path_ease_out, NULL);
    }
}

void ui_toast_hide(void) {
    if (g_toast == NULL || !g_toast_up) {
        return;
    }
    g_toast_up = false;
    g_toast_until = 0;
    slide(g_toast, toast_hidden_y(), TOAST_OUT_MS, lv_anim_path_ease_in, toast_gone);
}

/* ---- HUD ---- */

static int32_t hud_shown_y(void) { return UI_H - UIM->hud_h; }

static void hud_gone(lv_anim_t *a) {
    (void)a;
    if (!g_hud_up) {
        lv_obj_set_hidden(g_hud, true);
    }
}

void ui_hud_show(const char *label, int pct, uint32_t ms) {
    if (g_hud == NULL) {
        return;
    }
    ui_label_set_text(g_hud_lbl, label);
    lv_obj_set_hidden(g_hud_bar, pct < 0);
    bool was_up = g_hud_up;
    g_hud_up = true;
    g_hud_until = os_tick_ms() + (ms != 0 ? ms : HUD_HOLD_MS);
    if (g_hud_until == 0) {
        g_hud_until = 1;
    }
    lv_obj_set_hidden(g_hud, false);
    if (pct >= 0) {
        lv_bar_set_value(g_hud_bar, pct, was_up ? LV_ANIM_ON : LV_ANIM_OFF); /* the fill follows a change in 60 ms; it starts at its value when the strip appears */
    }
    if (!was_up) {
        lv_obj_set_y(g_hud, UI_H);
    }
    if (lv_obj_get_y(g_hud) != hud_shown_y()) {
        slide(g_hud, hud_shown_y(), HUD_IN_MS, lv_anim_path_ease_out, NULL);
    }
}

void ui_hud_hide(void) {
    if (!g_hud_up) {
        return;
    }
    g_hud_up = false;
    g_hud_until = 0;
    slide(g_hud, UI_H, HUD_OUT_MS, lv_anim_path_ease_in, hud_gone);
}

/* ---- lock pill and the Power-hold line ---- */

void ui_pill_show(const char *text) {
    if (g_pill == NULL) {
        return;
    }
    if (!lv_obj_is_hidden(g_pill) && strcmp(lv_label_get_text(g_pill_lbl), text) == 0) {
        return; /* called every UI pass while a press is held: touch LVGL only when the text changes */
    }
    ui_label_set_text(g_pill_lbl, text);
    lv_obj_set_hidden(g_pill, false);
    lv_obj_align(g_pill, LV_ALIGN_BOTTOM_MID, 0, -UIM->pill_margin); /* the width follows the text */
}

void ui_pill_hide(void) {
    if (g_pill != NULL) {
        lv_obj_set_hidden(g_pill, true);
    }
}

void ui_hold_line_set(int pct, bool red) {
    if (g_line == NULL) {
        return;
    }
    if (pct < 0) {
        lv_obj_set_hidden(g_line, true);
        return;
    }
    uint32_t col = red ? UI_C_RED : UI_C_ACCENT;
    ui_obj_bg_swap(g_line, g_line_col, col, 0);
    g_line_col = col;
    int32_t w = UI_W * (pct > 100 ? 100 : pct) / 100;
    if (w != lv_obj_get_width(g_line)) {
        lv_obj_set_width(g_line, w);
    }
    lv_obj_set_hidden(g_line, false);
}

/* ---- housekeeping ---- */

void ui_strips_tick(uint32_t now_ms) {
    if (g_toast_until != 0 && (int32_t)(now_ms - g_toast_until) >= 0) {
        ui_toast_hide();
    }
    if (g_hud_until != 0 && (int32_t)(now_ms - g_hud_until) >= 0) {
        ui_hud_hide();
    }
}

void ui_strips_settle(void) {
    if (g_toast != NULL) {
        lv_anim_delete(g_toast, set_y);
        lv_obj_set_y(g_toast, g_toast_up ? UI_STATUS_H : toast_hidden_y());
        lv_obj_set_hidden(g_toast, !g_toast_up);
    }
    if (g_hud != NULL) {
        lv_anim_delete(g_hud, set_y);
        lv_obj_set_y(g_hud, g_hud_up ? hud_shown_y() : UI_H);
        lv_obj_set_hidden(g_hud, !g_hud_up);
        lv_anim_delete(g_hud_bar, NULL);
    }
}

static lv_obj_t *strip_new(lv_obj_t *scr, int32_t h, int32_t y, uint32_t bg) {
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, UI_W, h);
    lv_obj_set_pos(o, 0, y);
    lv_obj_set_scrollable(o, false);
    ui_obj_bg(o, bg, 0);
    lv_obj_set_hidden(o, true);
    return o;
}

/* Stacking order matters: the toast must slide out from UNDER the status bar, so the frame calls this before it creates the bar. */
void ui_strips_init(lv_obj_t *scr) {
    g_scr = scr;
    g_toast_up = g_hud_up = false;
    g_toast_until = g_hud_until = 0;
    g_toast_col = UI_C_AMBER_SOFT;

    g_toast = strip_new(scr, UIM->toast_h, UI_STATUS_H - UIM->toast_h, g_toast_col);
    ui_obj_border(g_toast, UI_C_LINE, 1, LV_BORDER_SIDE_BOTTOM);
    g_toast_lbl = ui_label_small(g_toast, "", UI_C_INK);
    lv_label_set_long_mode(g_toast_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_size(g_toast_lbl, UI_W - 2 * UIM->pad, lv_font_get_line_height(UIM->font_small)); /* one line: dots, never a second line */
    lv_obj_set_style_text_align(g_toast_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_toast_lbl, LV_ALIGN_CENTER, 0, 0);

    g_hud = strip_new(scr, UIM->hud_h, UI_H, UI_C_INK);
    g_hud_lbl = ui_label_small(g_hud, "", UI_C_SURFACE);
    lv_label_set_long_mode(g_hud_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_size(g_hud_lbl, UI_W * 45 / 100 - UIM->pad, lv_font_get_line_height(UIM->font_small));
    lv_obj_align(g_hud_lbl, LV_ALIGN_LEFT_MID, UIM->pad, 0);
    g_hud_bar = lv_bar_create(g_hud);
    lv_obj_remove_style_all(g_hud_bar);
    int32_t bx = UI_W * 45 / 100;
    lv_obj_set_size(g_hud_bar, UI_W - UIM->pad - bx, UIM->hud_bar_h);
    lv_obj_set_pos(g_hud_bar, bx, (UIM->hud_h - UIM->hud_bar_h) / 2);
    lv_bar_set_range(g_hud_bar, 0, 100);
    lv_obj_set_style_radius(g_hud_bar, UIM->hud_bar_h / 2, 0);
    lv_obj_set_style_radius(g_hud_bar, UIM->hud_bar_h / 2, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(g_hud_bar, HUD_BAR_ANIM_MS, 0);
    ui_obj_bg(g_hud_bar, UI_C_INK2, 0);
    ui_obj_bg(g_hud_bar, UI_C_SURFACE, LV_PART_INDICATOR);

    g_pill = lv_obj_create(scr);
    lv_obj_remove_style_all(g_pill);
    lv_obj_set_size(g_pill, LV_SIZE_CONTENT, UIM->pill_h);
    lv_obj_set_scrollable(g_pill, false);
    lv_obj_set_style_radius(g_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(g_pill, UIM->pill_pad, 0);
    ui_obj_bg(g_pill, UI_C_INK, 0);
    g_pill_lbl = ui_label_small(g_pill, "", UI_C_SURFACE);
    lv_obj_center(g_pill_lbl);
    lv_obj_set_hidden(g_pill, true);
}

/* The Power-hold line sits ABOVE the status bar (it is drawn over its lower edge), so the frame creates it after the bar. */
void ui_strips_init_line(lv_obj_t *scr) {
    g_line = lv_obj_create(scr);
    lv_obj_remove_style_all(g_line);
    lv_obj_set_size(g_line, 0, UIM->hold_line_h);
    lv_obj_set_pos(g_line, 0, UI_STATUS_H - UIM->hold_line_h);
    g_line_col = UI_C_ACCENT;
    ui_obj_bg(g_line, g_line_col, 0);
    lv_obj_set_hidden(g_line, true);
}
