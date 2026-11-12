// SPDX-License-Identifier: MPL-2.0
/*
 * Settings > About > Licences: the third-party components in this firmware and their licences (ui_licence_data.c, kept in step with LICENSE.md).
 * A list of components (name, licence); Select opens one: its licence as the headline and a short plain notice. The notices are short enough to
 * fit without scrolling at every UI profile (checked in tests/test_licences.c), so the screen is just labels.
 */
#include <stdio.h>

#include "hal_system.h"
#include "ui_internal.h"
#include "ui_licence_data.h"

/* ---- one component ---- */

static ui_screen_t *make_notice(const ui_licence_t *l) {
    ui_screen_t *s = ui_screen_new(sizeof *s, l->name);
    if (s == NULL) {
        return NULL;
    }
    lv_obj_t *head = ui_label(s->root, l->licence, UI_L_SEL_BOT);
    lv_obj_set_style_text_font(head, UIM->font_large, 0);
    lv_obj_set_width(head, UI_W - 2 * UIM->pad);
    lv_label_set_long_mode(head, LV_LABEL_LONG_DOT);
    lv_obj_set_height(head, lv_font_get_line_height(UIM->font_large));
    lv_obj_set_pos(head, UIM->pad, UIM->pad);
    lv_obj_t *body = ui_label(s->root, l->notice, UI_L_TEXT);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, UI_W - 2 * UIM->pad);
    lv_obj_set_pos(body, UIM->pad, UIM->pad * 2 + lv_font_get_line_height(UIM->font_large));
    return s;
}

/* ---- the list ---- */

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
} licences_t;

static void licences_get(void *ctx, int32_t i, ui_list_item_t *o) {
    (void)ctx;
    const ui_licence_t *l = ui_licence_get(hal_platform_name(), (size_t)i);
    if (l == NULL) {
        return;
    }
    snprintf(o->label, sizeof o->label, "%s", l->name);
    snprintf(o->detail, sizeof o->detail, "%s", l->licence);
}

static void licences_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((licences_t *)s)->list, steps); }

static bool licences_button(ui_screen_t *b, uint8_t btn) {
    licences_t *s = (licences_t *)b;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    const ui_licence_t *l = ui_licence_get(hal_platform_name(), (size_t)ui_list_selected(s->list));
    if (l != NULL) {
        ui_frame_push(make_notice(l));
    }
    return true;
}

static void licences_destroy(ui_screen_t *b) { ui_list_destroy(((licences_t *)b)->list); }
static void licences_suspend(ui_screen_t *b) { ui_list_suspend(((licences_t *)b)->list); }
static void licences_resume(ui_screen_t *b) {
    ui_list_resume(((licences_t *)b)->list);
    ui_list_refresh(((licences_t *)b)->list);
}

ui_screen_t *ui_screen_licences_create(void) {
    licences_t *s = (licences_t *)ui_screen_new(sizeof *s, "Licences");
    if (s == NULL) {
        return NULL;
    }
    s->base.on_scroll = licences_scroll;
    s->base.on_button = licences_button;
    s->base.destroy = licences_destroy;
    s->base.suspend = licences_suspend;
    s->base.resume = licences_resume;
    s->list = ui_list_create(s->base.root, UI_W, &(ui_list_model_t){.count = (int32_t)ui_licence_count(hal_platform_name()), .ctx = s, .get = licences_get});
    return &s->base;
}
