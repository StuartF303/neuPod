// SPDX-License-Identifier: MPL-2.0
// LVGL view of the wheel input widgets (models: wheel_input.c). One modal at a time, objects only while it is open.
#include <stdio.h>
#include <string.h>

#include "ui_input.h"
#include "ui_internal.h"

#define CELLS 5

typedef struct {
    ui_screen_t base;
    wi_request_t rq;
    union {
        wi_text_t text;
        wi_number_t num;
        wi_picker_t pick;
        wi_datetime_t dt;
    } m;
    lv_obj_t *value;
    lv_obj_t *cell[CELLS];
    lv_obj_t *hint;
    lv_obj_t *bar;
    lv_obj_t *field[5];
    ui_list_t *list;
} in_screen_t;

static bool g_open;

static void wipe(void *p, size_t n) { /* passwords: do not leave them in freed LVGL memory or on the stack */
    volatile uint8_t *v = p;
    while (n-- > 0) {
        *v++ = 0;
    }
}

bool ui_input_active(void) { return g_open; }

/* ---- value out ---- */
static void build_value(const in_screen_t *s, wi_value_t *v) {
    memset(v, 0, sizeof *v);
    switch (s->rq.kind) {
    case WI_K_TEXT: memcpy(v->text, s->m.text.text, sizeof v->text); break;
    case WI_K_NUMBER:
    case WI_K_SLIDER: v->number = s->m.num.value; break;
    case WI_K_PICKER: v->pick = s->m.pick.sel; v->mask = s->m.pick.mask; break;
    case WI_K_DATETIME: v->dt = s->m.dt; break;
    }
}

/* ---- drawing ---- */
static void draw_text(in_screen_t *s) {
    const wi_text_t *t = &s->m.text;
    char line[2 * WI_TEXT_MAX + 4];
    size_t o = 0;
    for (uint32_t i = 0; i <= t->len && o < sizeof line - 3; i++) {
        if (i == t->cursor) {
            line[o++] = '|';
        }
        if (i == t->len) {
            break;
        }
        uint8_t c = (uint8_t)t->text[i];
        if (t->masked && !t->show) {
            if ((c & 0xC0u) != 0x80u) {
                line[o++] = '*';
            }
        } else {
            line[o++] = (char)c;
        }
    }
    line[o] = '\0';
    lv_label_set_text(s->value, line);
    uint32_t n = wi_text_strip_len(t);
    for (int i = 0; i < CELLS; i++) {
        char k[8];
        uint32_t idx = (t->sel + (uint32_t)(i - CELLS / 2) + n * 4u) % n; /* wraps visually, as the wheel does */
        wi_text_strip_key(t, idx, k);
        lv_label_set_text(s->cell[i], k);
    }
    char h[64];
    snprintf(h, sizeof h, "Select: type   Menu: delete   Play: done");
    lv_label_set_text(s->hint, h);
}

static void draw_number(in_screen_t *s) {
    char b[32];
    wi_number_format(&s->m.num, b, sizeof b);
    lv_label_set_text(s->value, b);
    if (s->bar != NULL) {
        lv_bar_set_value(s->bar, s->m.num.value, LV_ANIM_OFF);
    }
}

static void draw_picker(in_screen_t *s) {
    ui_list_set_selected(s->list, (int32_t)s->m.pick.sel);
    ui_list_refresh(s->list);
}

static void draw_dt(in_screen_t *s) {
    const wi_datetime_t *t = &s->m.dt;
    char b[5][8];
    snprintf(b[0], 8, "%02d", (int)t->d);
    snprintf(b[1], 8, "%02d", (int)t->mo);
    snprintf(b[2], 8, "%04d", (int)t->y);
    snprintf(b[3], 8, "%02d", (int)t->h);
    snprintf(b[4], 8, "%02d", (int)t->mi);
    for (int i = 0; i < 5; i++) {
        if (s->field[i] != NULL) {
            lv_label_set_text(s->field[i], b[i]);
            lv_obj_set_style_text_decor(s->field[i], t->field == i ? LV_TEXT_DECOR_UNDERLINE : LV_TEXT_DECOR_NONE, 0);
        }
    }
}

static void draw(in_screen_t *s) {
    switch (s->rq.kind) {
    case WI_K_TEXT: draw_text(s); break;
    case WI_K_NUMBER:
    case WI_K_SLIDER: draw_number(s); break;
    case WI_K_PICKER: draw_picker(s); break;
    case WI_K_DATETIME: draw_dt(s); break;
    }
}

/* ---- events ---- */
static void feed(in_screen_t *s, wi_ev_t ev, int32_t steps) {
    wi_result_t r = WI_RUNNING;
    bool changed = false;
    switch (s->rq.kind) {
    case WI_K_TEXT: r = wi_text_event(&s->m.text, ev, steps); changed = s->m.text.changed && s->m.text.search; s->m.text.changed = false; break;
    case WI_K_NUMBER: r = wi_number_event(&s->m.num, ev, steps); s->m.num.changed = false; break;
    case WI_K_SLIDER: r = wi_slider_event(&s->m.num, ev, steps); changed = s->m.num.changed; s->m.num.changed = false; break;
    case WI_K_PICKER: r = wi_picker_event(&s->m.pick, ev, steps); break;
    case WI_K_DATETIME: r = wi_datetime_event(&s->m.dt, ev, steps); break;
    }
    wi_value_t v;
    build_value(s, &v);
    if (changed && s->rq.change != NULL) {
        s->rq.change(s->rq.ctx, &v);
    }
    if (r == WI_RUNNING) {
        draw(s);
        return;
    }
    wi_done_fn done = s->rq.done;
    void *ctx = s->rq.ctx;
    /* g_open stays true until the modal is really gone: a modal opened from inside done() fails (returns false) instead of being
     * popped with this one. Chain steps from the owning screen's tick. */
    if (done != NULL) {
        done(ctx, r == WI_OK, &v);
    }
    wipe(&v, sizeof v);
    ui_frame_pop(); /* destroys this screen (in_destroy clears g_open): nothing may touch it afterwards */
}

static void in_scroll(ui_screen_t *b, int32_t steps) { feed((in_screen_t *)b, WI_EV_SCROLL, steps); }

static bool in_button(ui_screen_t *b, uint8_t btn) {
    in_screen_t *s = (in_screen_t *)b;
    switch (btn) {
    case HAL_BTN_SELECT: feed(s, WI_EV_SELECT, 0); break;
    case HAL_BTN_MENU: feed(s, WI_EV_MENU, 0); break;
    case HAL_BTN_PLAY: feed(s, WI_EV_PLAY, 0); break;
    case HAL_BTN_LEFT: feed(s, WI_EV_LEFT, 0); break;
    case HAL_BTN_RIGHT: feed(s, WI_EV_RIGHT, 0); break;
    default: break;
    }
    return true; /* a modal swallows every button: Menu is a model decision here, never "pop" */
}

static void in_destroy(ui_screen_t *b) {
    in_screen_t *s = (in_screen_t *)b;
    wipe(&s->m, sizeof s->m);
    wipe(&s->rq, sizeof s->rq);
    if (s->list != NULL) {
        ui_list_destroy(s->list);
    }
    g_open = false;
}

static void pick_get(void *ctx, int32_t i, ui_list_item_t *o) {
    const in_screen_t *s = ctx;
    snprintf(o->label, sizeof o->label, "%s%s", s->rq.multi ? ((s->m.pick.mask >> i) & 1u ? "[x] " : "[ ] ") : "", s->rq.items[i]);
}

bool ui_input_open(const wi_request_t *r) {
    if (g_open || r == NULL) {
        return false;
    }
    ui_frame_suspend_top(); /* free the covered list's rows first: the pool peak matters more than a rebuild */
    in_screen_t *s = (in_screen_t *)ui_screen_new(sizeof *s, r->title != NULL ? r->title : "");
    if (s == NULL) {
        ui_frame_resume_top(); /* the covered screen was suspended above: give it its rows back */
        return false;
    }
    s->rq = *r;
    s->base.on_scroll = in_scroll;
    s->base.on_button = in_button;
    s->base.destroy = in_destroy;
    lv_obj_t *root = s->base.root;
    switch (r->kind) {
    case WI_K_TEXT:
        wi_text_init(&s->m.text, r->initial, r->max_chars, r->charset, r->masked, r->search);
        wi_text_set_byte_limit(&s->m.text, r->max_bytes);
        s->value = ui_label(root, "", UI_L_TEXT);
        lv_obj_set_style_text_font(s->value, UIM->font_large, 0);
        lv_label_set_long_mode(s->value, LV_LABEL_LONG_DOT);
        lv_obj_set_width(s->value, UI_W - 2 * UIM->pad);
        lv_obj_set_pos(s->value, UIM->pad, UIM->in_text_value_y);
        for (int i = 0; i < CELLS; i++) {
            s->cell[i] = ui_label(root, "", i == CELLS / 2 ? UI_C_ACCENT : UI_L_DIM);
            lv_obj_set_style_text_font(s->cell[i], i == CELLS / 2 ? UIM->font_large : UIM->font_body, 0);
            lv_obj_set_width(s->cell[i], UIM->in_cell_w);
            lv_obj_set_style_text_align(s->cell[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(s->cell[i], UI_W / 2 - UIM->in_cell_w / 2 + (i - CELLS / 2) * UIM->in_cell_w, UIM->in_cell_y);
        }
        s->hint = ui_label(root, "", UI_L_FAINT);
        lv_obj_set_width(s->hint, UI_W - 2 * UIM->in_text_hint_x);
        lv_obj_set_style_text_align(s->hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s->hint, UIM->in_text_hint_x, UIM->in_hint_y);
        break;
    case WI_K_NUMBER:
    case WI_K_SLIDER:
        wi_number_init(&s->m.num, r->value, r->min, r->max, r->step, r->decimals, r->unit);
        s->value = ui_label(root, "", UI_L_TEXT);
        lv_obj_set_style_text_font(s->value, UIM->font_large, 0);
        lv_obj_set_width(s->value, UI_W);
        lv_obj_set_style_text_align(s->value, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s->value, 0, UIM->in_num_value_y);
        if (r->kind == WI_K_SLIDER) {
            s->bar = lv_bar_create(root);
            lv_obj_set_size(s->bar, UI_W - 2 * UIM->in_bar_x, UIM->in_bar_h);
            lv_obj_set_pos(s->bar, UIM->in_bar_x, UIM->in_bar_y);
            lv_bar_set_range(s->bar, r->min, r->max);
            ui_obj_bg(s->bar, UI_C_LINE, 0);
            ui_obj_bg(s->bar, UI_C_ACCENT, LV_PART_INDICATOR);
        }
        s->hint = ui_label(root, "Scroll: adjust  Play: ok  Menu: cancel", UI_L_FAINT);
        lv_obj_set_pos(s->hint, UIM->in_hint_x, UIM->in_hint_y);
        break;
    case WI_K_PICKER: {
        wi_picker_init(&s->m.pick, r->count, r->sel, r->multi, false);
        s->list = ui_list_create(root, UI_W, &(ui_list_model_t){.count = (int32_t)s->m.pick.count, .ctx = s, .get = pick_get});
        break;
    }
    case WI_K_DATETIME: {
        wi_datetime_init(&s->m.dt, r->has_date, r->has_time, r->y, r->mo, r->d, r->h, r->mi);
        for (int i = 0; i < 5; i++) {
            if ((i < 3 && r->has_date) || (i >= 3 && r->has_time)) {
                s->field[i] = ui_label(root, "", UI_L_TEXT);
                lv_obj_set_style_text_font(s->field[i], UIM->font_large, 0);
                lv_obj_set_pos(s->field[i], r->has_date ? UIM->in_dt_x[i] : UIM->in_dt_x[i] + UIM->in_dt_time_shift, UIM->in_dt_y);
            }
        }
        s->hint = ui_label(root, "Left/Right: field   Scroll: change   Play: ok", UI_L_FAINT);
        lv_obj_set_pos(s->hint, UIM->in_hint_x, UIM->in_hint_y);
        break;
    }
    }
    g_open = true;
    ui_frame_push(&s->base);
    draw(s);
    return true;
}

/* ---- letter overlay ---- */
struct ui_letter_overlay {
    lv_obj_t *box;
    lv_obj_t *lbl;
    char shown;
    bool visible;
};

ui_letter_overlay_t *ui_letter_overlay_create(void *parent) {
    ui_letter_overlay_t *o = lv_malloc(sizeof *o);
    if (o == NULL) {
        return NULL;
    }
    memset(o, 0, sizeof *o);
    o->box = lv_obj_create(parent);
    lv_obj_remove_style_all(o->box);
    lv_obj_set_size(o->box, UIM->in_overlay, UIM->in_overlay);
    lv_obj_align(o->box, LV_ALIGN_CENTER, 0, 0);
    ui_obj_bg(o->box, UI_C_INK, 0);
    lv_obj_set_style_bg_opa(o->box, LV_OPA_80, 0);
    lv_obj_set_style_radius(o->box, UIM->in_overlay_radius, 0);
    lv_obj_set_hidden(o->box, true);
    o->lbl = ui_label(o->box, "A", UI_C_SURFACE);
    lv_obj_set_style_text_font(o->lbl, UIM->font_large, 0);
    lv_obj_center(o->lbl);
    return o;
}

void ui_letter_overlay_update(ui_letter_overlay_t *o, const wi_letterjump_t *j) {
    if (o == NULL) {
        return;
    }
    if (j->active != o->visible) {
        o->visible = j->active;
        lv_obj_set_hidden(o->box, !j->active);
    }
    if (j->active && j->letter != o->shown) {
        o->shown = j->letter;
        char b[2] = {j->letter, 0};
        lv_label_set_text(o->lbl, b);
        lv_obj_center(o->lbl);
    }
}

void ui_letter_overlay_destroy(ui_letter_overlay_t *o) {
    if (o != NULL) {
        lv_obj_delete(o->box);
        lv_free(o);
    }
}
