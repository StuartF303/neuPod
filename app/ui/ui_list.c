// SPDX-License-Identifier: MPL-2.0
/*
 * Virtualised list widget: a fixed pool of row objects is re-bound to whatever items are in view, so a
 * 500-entry directory costs the same as a 7-entry menu. The selection bar jumps to the new row at once;
 * the list itself scrolls smoothly (one short animation) when the selection leaves the window, as on the
 * classic click-wheel menus. Selection clamps at both ends (no wrap).
 */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define POOL (UI_ROWS_VISIBLE + 2)
#define SCROLL_ANIM_MS 110

typedef struct {
    lv_obj_t *obj, *label, *detail, *chev;
    int32_t idx; /* bound item, -1 = unbound/hidden */
    bool sel;
    bool bar;                          /* selection bar style currently applied */
    uint32_t c_label, c_detail, c_chev; /* colours currently applied (shared styles, see ui_label) */
} row_t;

static lv_style_t g_bar_style; /* the blue selection bar, shared by every row */
static bool g_bar_ready;

struct ui_list {
    lv_obj_t *box;
    int32_t w;
    ui_list_model_t model;
    ui_nav_t nav;
    int32_t scroll_px;
    bool focus;
    row_t row[POOL];
    lv_obj_t *sb; /* scroll bar */
    lv_obj_t *parent;
};

static void set_text(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void style_row(ui_list_t *l, row_t *r, bool dim) {
    bool on = r->sel && l->focus;
    if (on != r->bar) {
        if (on) {
            lv_obj_add_style(r->obj, &g_bar_style, 0);
        } else {
            lv_obj_remove_style(r->obj, &g_bar_style, 0);
        }
        r->bar = on;
    }
    uint32_t text = on ? 0xFFFFFF : dim ? UI_L_FAINT : UI_L_TEXT;
    uint32_t sub = on ? 0xFFFFFF : UI_L_DIM;
    ui_label_recolour(r->label, r->c_label, text);
    ui_label_recolour(r->detail, r->c_detail, sub);
    ui_label_recolour(r->chev, r->c_chev, sub);
    r->c_label = text;
    r->c_detail = sub;
    r->c_chev = sub;
}

static void bind_row(ui_list_t *l, row_t *r, int32_t idx) {
    r->idx = idx;
    if (idx < 0 || idx >= l->model.count) {
        r->idx = -1;
        lv_obj_set_hidden(r->obj, true);
        return;
    }
    ui_list_item_t it;
    memset(&it, 0, sizeof it);
    l->model.get(l->model.ctx, idx, &it);
    lv_obj_set_hidden(r->obj, false);
    r->sel = idx == l->nav.sel;
    set_text(r->label, it.label);
    set_text(r->detail, it.detail);
    set_text(r->chev, it.chevron ? LV_SYMBOL_RIGHT : "");
    int32_t detail_w = it.detail[0] ? (int32_t)strlen(it.detail) * 8 + 10 : 0; /* about 7.5 px per character */
    int32_t label_w = l->w - 10 - (it.chevron ? 22 : 8) - detail_w;
    lv_obj_set_width(r->label, label_w);
    lv_obj_align(r->detail, LV_ALIGN_RIGHT_MID, it.chevron ? -22 : -8, 0);
    style_row(l, r, it.dim);
}

static void update_scrollbar(ui_list_t *l) {
    int32_t total = l->model.count * UI_ROW_H;
    int32_t view = UI_CONTENT_H;
    if (total <= view) {
        lv_obj_set_hidden(l->sb, true);
        return;
    }
    lv_obj_set_hidden(l->sb, false);
    int32_t h = view * view / total;
    if (h < 14) {
        h = 14;
    }
    int32_t y = (view - h) * l->scroll_px / (total - view);
    lv_obj_set_height(l->sb, h);
    lv_obj_set_y(l->sb, y);
}

static void layout(ui_list_t *l, bool rebind_all) {
    if (l->box == NULL) {
        return; /* suspended */
    }
    int32_t first = l->scroll_px / UI_ROW_H;
    for (int i = 0; i < POOL; i++) {
        row_t *r = &l->row[i];
        int32_t idx = first + i;
        if (rebind_all || r->idx != idx) {
            bind_row(l, r, idx);
        }
        if (r->idx >= 0) {
            lv_obj_set_y(r->obj, idx * UI_ROW_H - l->scroll_px);
        }
    }
    update_scrollbar(l);
}

static void scroll_anim_cb(void *var, int32_t v) {
    ui_list_t *l = var;
    l->scroll_px = v;
    layout(l, false);
}

static void scroll_to_window(ui_list_t *l, bool animate) {
    int32_t target = l->nav.top * UI_ROW_H;
    lv_anim_delete(l, scroll_anim_cb);
    if (!animate || target == l->scroll_px) {
        l->scroll_px = target;
        layout(l, false);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_exec_cb(&a, scroll_anim_cb);
    lv_anim_set_values(&a, l->scroll_px, target);
    lv_anim_set_duration(&a, SCROLL_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void build(ui_list_t *l) {
    l->box = lv_obj_create(l->parent);
    lv_obj_remove_style_all(l->box);
    lv_obj_set_size(l->box, l->w, UI_CONTENT_H);
    lv_obj_set_pos(l->box, 0, 0);
    lv_obj_set_scrollable(l->box, false);

    for (int i = 0; i < POOL; i++) {
        row_t *r = &l->row[i];
        r->idx = -1;
        r->obj = lv_obj_create(l->box);
        lv_obj_remove_style_all(r->obj);
        lv_obj_set_size(r->obj, l->w, UI_ROW_H);
        lv_obj_set_scrollable(r->obj, false);
        r->c_label = UI_L_TEXT;
        r->c_detail = r->c_chev = UI_L_DIM;
        r->label = ui_label(r->obj, "", UI_L_TEXT);
        lv_label_set_long_mode(r->label, LV_LABEL_LONG_DOT);
        lv_obj_set_height(r->label, 18); /* fixed height: "..." instead of wrapping onto a second line */
        lv_obj_align(r->label, LV_ALIGN_LEFT_MID, 10, 0);
        r->detail = ui_label(r->obj, "", UI_L_DIM);
        r->chev = ui_label(r->obj, "", UI_L_DIM);
        lv_obj_align(r->chev, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_hidden(r->obj, true);
    }
    l->sb = lv_obj_create(l->box);
    lv_obj_remove_style_all(l->sb);
    lv_obj_set_size(l->sb, 3, 20);
    lv_obj_set_x(l->sb, l->w - 5);
    lv_obj_set_style_radius(l->sb, 2, 0);
    lv_obj_set_style_bg_color(l->sb, lv_color_hex(UI_L_FAINT), 0);
    lv_obj_set_style_bg_opa(l->sb, LV_OPA_COVER, 0);
    lv_obj_set_hidden(l->sb, true);

}

ui_list_t *ui_list_create(lv_obj_t *parent, int32_t w, const ui_list_model_t *model) {
    ui_list_t *l = lv_malloc(sizeof *l);
    if (l == NULL) {
        return NULL;
    }
    memset(l, 0, sizeof *l);
    if (!g_bar_ready) {
        lv_style_init(&g_bar_style);
        lv_style_set_bg_color(&g_bar_style, lv_color_hex(UI_L_SEL_TOP));
        lv_style_set_bg_grad_color(&g_bar_style, lv_color_hex(UI_L_SEL_BOT));
        lv_style_set_bg_grad_dir(&g_bar_style, LV_GRAD_DIR_VER);
        lv_style_set_bg_opa(&g_bar_style, LV_OPA_COVER);
        g_bar_ready = true;
    }
    l->w = w;
    l->model = *model;
    l->focus = true;
    ui_nav_init(&l->nav, model->count, UI_ROWS_VISIBLE);

    l->parent = parent;
    build(l);
    layout(l, true);
    return l;
}

void ui_list_destroy(ui_list_t *l) {
    if (l == NULL) {
        return;
    }
    lv_anim_delete(l, scroll_anim_cb);
    if (l->box != NULL) {
        lv_obj_delete(l->box);
    }
    lv_free(l);
}

static void restyle_selected(ui_list_t *l, int32_t old_sel) {
    for (int i = 0; i < POOL; i++) {
        row_t *r = &l->row[i];
        if (r->idx == old_sel || r->idx == l->nav.sel) {
            bind_row(l, r, r->idx);
        }
    }
}

bool ui_list_move(ui_list_t *l, int32_t steps) {
    int32_t old = l->nav.sel;
    if (!ui_nav_move(&l->nav, steps)) {
        return false;
    }
    restyle_selected(l, old);
    scroll_to_window(l, true);
    ui_feedback_step();
    if (l->model.get != NULL && l->nav.sel >= 0 && l->nav.sel < l->model.count) { /* one line per step, for blind bench navigation */
        ui_list_item_t it;
        memset(&it, 0, sizeof it);
        l->model.get(l->model.ctx, l->nav.sel, &it);
        printf("ui: sel %s %d '%s'\n", ui_frame_top_title(), (int)l->nav.sel, it.label);
    }
    return true;
}

int32_t ui_list_selected(const ui_list_t *l) { return l->nav.sel; }

void ui_list_set_selected(ui_list_t *l, int32_t sel) {
    ui_nav_set(&l->nav, sel);
    scroll_to_window(l, false);
    layout(l, true);
}

void ui_list_refresh(ui_list_t *l) { layout(l, true); }

void ui_list_set_count(ui_list_t *l, int32_t count) {
    l->model.count = count;
    ui_nav_set_count(&l->nav, count);
    lv_anim_delete(l, scroll_anim_cb);
    l->scroll_px = l->nav.top * UI_ROW_H;
    layout(l, true);
}

void ui_list_set_focus(ui_list_t *l, bool focus) {
    l->focus = focus;
    layout(l, true);
}

void ui_list_suspend(ui_list_t *l) {
    if (l == NULL || l->box == NULL) {
        return;
    }
    lv_anim_delete(l, scroll_anim_cb);
    lv_obj_delete(l->box); /* frees every row object: the pool is small (docs/ui-input.md) */
    l->box = NULL;
}

void ui_list_resume(ui_list_t *l) {
    if (l == NULL || l->box != NULL) {
        return;
    }
    build(l);
    layout(l, true);
}
