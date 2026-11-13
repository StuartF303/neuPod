// SPDX-License-Identifier: MPL-2.0
// LVGL view of the wheel input widgets (models: wheel_input.c). One modal at a time, objects only while it is open.
#include <stdio.h>
#include <string.h>

#include "os.h"
#include "ui_input.h"
#include "ui_internal.h"

#define RING_N 9 /* glyphs in the ring: the current one and four either side */
#define CHIPS WI_CHIP_COUNT
#define REVEAL_MS 800u /* a typed password character stays visible this long, then it becomes a dot */
#define CLEAR_HOLD_MS 600u /* Menu held this long clears the text */

typedef struct {
    ui_screen_t base;
    wi_request_t rq;
    union {
        wi_text_t text;
        wi_number_t num;
        wi_picker_t pick;
        wi_datetime_t dt;
    } m;
    lv_obj_t *value;         /* text: the field's text; number / slider: the Big value */
    lv_obj_t *box, *caret;   /* text: the field box and its caret */
    lv_obj_t *ring[RING_N];  /* text: the glyph ring, index 4 is the current one */
    lv_obj_t *chips, *chip[CHIPS], *chip_l[CHIPS];
    bool chip_on[CHIPS];
    lv_obj_t *hint;
    lv_obj_t *bar, *bar_min, *bar_max;
    lv_obj_t *field[5], *fname[5];
    uint32_t fcol[5];        /* the colour each date / time field has now */
    ui_list_t *list;
    uint32_t reveal_until;   /* password: the typed character is shown until this tick (0 = none) */
    uint32_t menu_down;      /* tick of the Menu press being held (0 = none) */
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

static const char *const k_chip_name[CHIPS] = {"abc", "ABC", "123", "#!?", "space", "del"};
static const char *const k_dt_name[5] = {"Day", "Month", "Year", "Hour", "Min"};

/* The text of the field. A password is dots, except the character just typed while it is new (REVEAL_MS). */
static void field_text(const in_screen_t *s, char *out, size_t cap) {
    const wi_text_t *t = &s->m.text;
    size_t o = 0;
    for (uint32_t i = 0; i < t->len && o + 4 < cap;) {
        uint32_t l = 1;
        while (i + l < t->len && ((uint8_t)t->text[i + l] & 0xC0u) == 0x80u) {
            l++;
        }
        bool last = i + l >= t->len;
        if (t->masked && !(last && s->reveal_until != 0)) {
            memcpy(out + o, "\xE2\x80\xA2", 3); /* the bullet */
            o += 3;
        } else {
            for (uint32_t k = 0; k < l && o + 1 < cap; k++) {
                out[o++] = t->text[i + k];
            }
        }
        i += l;
    }
    out[o] = '\0';
}

static void set_chip_look(in_screen_t *s, int c, bool on) {
    if (s->chip_on[c] == on) {
        return;
    }
    s->chip_on[c] = on;
    ui_obj_bg_swap(s->chip[c], on ? UI_C_SURFACE : UI_C_ACCENT_SOFT, on ? UI_C_ACCENT_SOFT : UI_C_SURFACE, 0);
    ui_label_recolour(s->chip_l[c], on ? UI_C_INK2 : UI_C_ACCENT, on ? UI_C_ACCENT : UI_C_INK2);
}

static void draw_text(in_screen_t *s) {
    const wi_text_t *t = &s->m.text;
    char line[2 * WI_TEXT_MAX + 4];
    field_text(s, line, sizeof line);
    ui_label_set_text(s->value, line);
    lv_obj_update_layout(s->value); /* its width, to put the caret after it and keep the end of a long text in view */
    const int32_t inner = lv_obj_get_width(s->box) - 2 * UIM->pad, lw = lv_obj_get_width(s->value), cw = 2;
    const int32_t x = lw + cw + 1 > inner ? inner - lw - cw - 1 : 0;
    lv_obj_set_x(s->value, UIM->pad + x);
    lv_obj_set_x(s->caret, UIM->pad + x + lw + 1);
    /* the ring */
    uint32_t n = wi_text_strip_len(t);
    char k[8];
    const bool action = t->chip >= WI_CHIP_SPACE;
    for (int i = 0; i < RING_N; i++) {
        int o = i - RING_N / 2;
        if (action && o != 0) {
            lv_obj_set_hidden(s->ring[i], true);
            continue;
        }
        lv_obj_set_hidden(s->ring[i], false);
        uint32_t idx = (t->sel + (uint32_t)(o + (int)n * 4)) % n; /* wraps visually, as the wheel does */
        wi_text_strip_key(t, idx, k);
        lv_label_set_text(s->ring[i], k);
    }
    lv_obj_set_width(s->ring[RING_N / 2], action ? UI_W : UIM->in_cw0);
    lv_obj_set_x(s->ring[RING_N / 2], action ? 0 : UI_W / 2 - UIM->in_cw0 / 2);
    /* the chips: the ones this entry offers, the one in use lit */
    wi_chip_t offered[CHIPS];
    uint32_t cn = wi_text_chips(t, offered);
    for (int c = 0; c < CHIPS; c++) {
        bool has = false;
        for (uint32_t j = 0; j < cn; j++) {
            has = has || offered[j] == (wi_chip_t)c;
        }
        if (lv_obj_is_hidden(s->chip[c]) == has) {
            lv_obj_set_hidden(s->chip[c], !has);
        }
        set_chip_look(s, c, has && t->chip == (wi_chip_t)c);
    }
}

static void draw_number(in_screen_t *s) {
    char b[32];
    wi_number_format(&s->m.num, b, sizeof b);
    ui_label_set_text(s->value, b);
    lv_bar_set_value(s->bar, s->m.num.value, LV_ANIM_OFF);
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
            uint32_t col = t->field == i ? UI_C_ACCENT : UI_C_INK;
            ui_label_recolour(s->field[i], s->fcol[i], col);
            s->fcol[i] = col;
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
    case WI_K_TEXT:
        r = wi_text_event(&s->m.text, ev, steps);
        changed = s->m.text.changed && s->m.text.search;
        s->m.text.changed = false;
        s->reveal_until = s->m.text.typed && s->m.text.masked ? os_tick_ms() + REVEAL_MS : 0; /* any other event hides it again */
        break;
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
    case HAL_BTN_MENU:
        s->menu_down = os_tick_ms(); /* a long press clears the text (in_tick); set before feed(): a cancel pops this screen */
        feed(s, WI_EV_MENU, 0);
        break;
    case HAL_BTN_PLAY: feed(s, WI_EV_PLAY, 0); break;
    case HAL_BTN_LEFT: feed(s, WI_EV_LEFT, 0); break;
    case HAL_BTN_RIGHT: feed(s, WI_EV_RIGHT, 0); break;
    default: break;
    }
    return true; /* a modal swallows every button: Menu is a model decision here, never "pop" */
}

static void in_button_up(ui_screen_t *b, uint8_t btn) {
    if (btn == HAL_BTN_MENU) {
        ((in_screen_t *)b)->menu_down = 0;
    }
}

static void in_tick(ui_screen_t *b) {
    in_screen_t *s = (in_screen_t *)b;
    if (s->rq.kind != WI_K_TEXT) {
        return;
    }
    uint32_t now = os_tick_ms();
    if (s->menu_down != 0 && now - s->menu_down >= CLEAR_HOLD_MS) { /* Menu held: clear the whole text */
        s->menu_down = 0;
        wi_text_clear(&s->m.text);
        if (s->m.text.changed && s->m.text.search && s->rq.change != NULL) {
            wi_value_t v;
            build_value(s, &v);
            s->rq.change(s->rq.ctx, &v);
            wipe(&v, sizeof v);
        }
        s->m.text.changed = false;
        s->reveal_until = 0;
        draw(s);
    }
    if (s->reveal_until != 0 && (int32_t)(now - s->reveal_until) >= 0) { /* the typed character becomes a dot */
        s->reveal_until = 0;
        draw(s);
    }
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

/* ---- building ---- */

static lv_obj_t *hint_label(lv_obj_t *root, const char *text) {
    lv_obj_t *h = ui_label_small(root, text, UI_C_INK3);
    lv_obj_set_size(h, UI_W - 2 * UIM->in_hint_x, lv_font_get_line_height(UIM->font_small));
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(h, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(h, UIM->in_hint_x, UIM->in_hint_y);
    return h;
}

static void build_text(in_screen_t *s) {
    lv_obj_t *root = s->base.root;
    s->box = lv_obj_create(root);
    lv_obj_remove_style_all(s->box);
    lv_obj_set_size(s->box, UI_W - 2 * UIM->pad, UIM->in_field_h);
    lv_obj_set_pos(s->box, UIM->pad, UIM->in_field_y);
    lv_obj_set_scrollable(s->box, false);
    lv_obj_set_style_radius(s->box, UIM->in_field_radius, 0);
    ui_obj_bg(s->box, UI_C_SURFACE, 0);
    ui_obj_border(s->box, UI_C_LINE, 1, LV_BORDER_SIDE_FULL);
    s->value = ui_label(s->box, "", UI_C_INK);
    lv_label_set_long_mode(s->value, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(s->value, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_y(s->value, (UIM->in_field_h - lv_font_get_line_height(UIM->font_body)) / 2 - 1);
    s->caret = lv_obj_create(s->box);
    lv_obj_remove_style_all(s->caret);
    const int32_t ch = UIM->in_field_h - UIM->in_field_h * 3 / 8;
    lv_obj_set_size(s->caret, 2, ch);
    lv_obj_set_y(s->caret, (UIM->in_field_h - ch) / 2 - 1);
    ui_obj_bg(s->caret, UI_C_ACCENT, 0);
    /* the ring: the current glyph Big in the accent, its neighbours smaller and greyer; cells are as wide as their glyphs */
    const int32_t cw[5] = {UIM->in_cw0, UIM->in_cw1, UIM->in_cw2, UIM->in_cw3, UIM->in_cw4};
    const lv_font_t *fonts[5] = {UIM->font_huge, UIM->font_large, UIM->font_body, UIM->font_small, UIM->font_small};
    const uint32_t cols[5] = {UI_C_ACCENT, UI_C_INK, UI_C_INK2, UI_C_INK3, UI_C_INK3};
    const int32_t big_h = lv_font_get_line_height(UIM->font_huge);
    for (int i = 0; i < RING_N; i++) {
        int o = i - RING_N / 2, d = o < 0 ? -o : o;
        s->ring[i] = ui_label(root, "", cols[d]);
        lv_obj_set_style_text_font(s->ring[i], fonts[d], 0);
        lv_label_set_long_mode(s->ring[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(s->ring[i], cw[d]);
        lv_obj_set_style_text_align(s->ring[i], LV_TEXT_ALIGN_CENTER, 0);
        int32_t off = cw[0] / 2; /* distance of this cell's centre from the screen centre */
        for (int k = 1; k < d; k++) {
            off += cw[k];
        }
        if (d > 0) {
            off += cw[d] / 2;
        }
        int32_t cx = d == 0 ? UI_W / 2 : UI_W / 2 + (o < 0 ? -off : off);
        lv_obj_set_pos(s->ring[i], cx - cw[d] / 2, UIM->in_ring_y + (big_h - lv_font_get_line_height(fonts[d])) / 2);
    }
    /* the chips */
    s->chips = lv_obj_create(root);
    lv_obj_remove_style_all(s->chips);
    lv_obj_set_size(s->chips, UI_W, UIM->in_chip_h);
    lv_obj_set_pos(s->chips, 0, UIM->in_chip_y);
    lv_obj_set_scrollable(s->chips, false);
    lv_obj_set_flex_flow(s->chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s->chips, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s->chips, UIM->in_chip_gap, 0);
    for (int c = 0; c < CHIPS; c++) {
        s->chip[c] = lv_obj_create(s->chips);
        lv_obj_remove_style_all(s->chip[c]);
        lv_obj_set_size(s->chip[c], LV_SIZE_CONTENT, UIM->in_chip_h);
        lv_obj_set_scrollable(s->chip[c], false);
        lv_obj_set_style_radius(s->chip[c], UIM->in_chip_h / 2, 0);
        lv_obj_set_style_pad_hor(s->chip[c], UIM->in_chip_pad, 0);
        ui_obj_bg(s->chip[c], UI_C_SURFACE, 0);
        ui_obj_border(s->chip[c], UI_C_LINE, 1, LV_BORDER_SIDE_FULL);
        s->chip_l[c] = ui_label_small(s->chip[c], k_chip_name[c], UI_C_INK2);
        lv_obj_center(s->chip_l[c]);
        lv_obj_set_hidden(s->chip[c], true);
    }
    s->hint = hint_label(root, "Select: type \xC2\xB7 Menu: delete \xC2\xB7 Play: done");
}

static void build_number(in_screen_t *s) {
    lv_obj_t *root = s->base.root;
    wi_number_init(&s->m.num, s->rq.value, s->rq.min, s->rq.max, s->rq.step, s->rq.decimals, s->rq.unit);
    s->value = ui_label(root, "", UI_C_INK);
    lv_obj_set_style_text_font(s->value, UIM->font_huge, 0);
    lv_obj_set_size(s->value, UI_W, lv_font_get_line_height(UIM->font_huge));
    lv_obj_set_style_text_align(s->value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s->value, 0, UIM->in_num_value_y);
    s->bar = lv_bar_create(root);
    lv_obj_set_size(s->bar, UI_W - 2 * UIM->in_bar_x, UIM->in_bar_h);
    lv_obj_set_pos(s->bar, UIM->in_bar_x, UIM->in_bar_y);
    lv_bar_set_range(s->bar, s->rq.min, s->rq.max > s->rq.min ? s->rq.max : s->rq.min + 1);
    lv_obj_set_style_radius(s->bar, UIM->in_bar_h / 2, 0);
    lv_obj_set_style_radius(s->bar, UIM->in_bar_h / 2, LV_PART_INDICATOR);
    ui_obj_bg(s->bar, UI_C_LINE, 0);
    ui_obj_bg(s->bar, UI_C_ACCENT, LV_PART_INDICATOR);
    /* min and max under the ends of the bar */
    wi_number_t e = s->m.num;
    char b[32];
    const int32_t sh = lv_font_get_line_height(UIM->font_small);
    e.value = s->rq.min;
    wi_number_format(&e, b, sizeof b);
    s->bar_min = ui_label_small(root, b, UI_C_INK3);
    lv_obj_set_size(s->bar_min, (UI_W - 2 * UIM->in_bar_x) / 2, sh);
    lv_obj_set_pos(s->bar_min, UIM->in_bar_x, UIM->in_bar_label_y);
    e.value = s->rq.max;
    wi_number_format(&e, b, sizeof b);
    s->bar_max = ui_label_small(root, b, UI_C_INK3);
    lv_obj_set_size(s->bar_max, (UI_W - 2 * UIM->in_bar_x) / 2, sh);
    lv_obj_set_style_text_align(s->bar_max, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s->bar_max, UI_W / 2, UIM->in_bar_label_y);
    s->hint = hint_label(root, "Scroll: adjust \xC2\xB7 Play: ok \xC2\xB7 Menu: cancel");
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
    s->base.on_button_up = in_button_up;
    s->base.on_tick = in_tick;
    s->base.destroy = in_destroy;
    lv_obj_t *root = s->base.root;
    switch (r->kind) {
    case WI_K_TEXT:
        wi_text_init(&s->m.text, r->initial, r->max_chars, r->charset, r->masked, r->search);
        wi_text_set_byte_limit(&s->m.text, r->max_bytes);
        build_text(s);
        break;
    case WI_K_NUMBER:
    case WI_K_SLIDER:
        build_number(s);
        break;
    case WI_K_PICKER: {
        wi_picker_init(&s->m.pick, r->count, r->sel, r->multi, false);
        s->list = ui_list_create(root, UI_W, &(ui_list_model_t){.count = (int32_t)s->m.pick.count, .ctx = s, .get = pick_get});
        break;
    }
    case WI_K_DATETIME: {
        wi_datetime_init(&s->m.dt, r->has_date, r->has_time, r->y, r->mo, r->d, r->h, r->mi);
        const int32_t sh = lv_font_get_line_height(UIM->font_small);
        for (int i = 0; i < 5; i++) {
            if ((i < 3 && r->has_date) || (i >= 3 && r->has_time)) {
                int32_t x = r->has_date ? UIM->in_dt_x[i] : UIM->in_dt_x[i] + UIM->in_dt_time_shift;
                s->fcol[i] = UI_C_INK;
                s->field[i] = ui_label(root, "", UI_C_INK);
                lv_obj_set_style_text_font(s->field[i], UIM->font_large, 0);
                lv_obj_set_pos(s->field[i], x, UIM->in_dt_y);
                s->fname[i] = ui_label_small(root, k_dt_name[i], UI_C_INK3);
                lv_obj_set_size(s->fname[i], UIM->in_dt_x[1] - UIM->in_dt_x[0] + 4, sh);
                lv_label_set_long_mode(s->fname[i], LV_LABEL_LONG_CLIP);
                lv_obj_set_pos(s->fname[i], x, UIM->in_dt_name_y);
            }
        }
        s->hint = hint_label(root, "Left / Right: field \xC2\xB7 Scroll: change \xC2\xB7 Play: ok");
        break;
    }
    }
    g_open = true;
    ui_frame_push(&s->base);
    draw(s);
    return true;
}
