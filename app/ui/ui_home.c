// SPDX-License-Identifier: MPL-2.0
/*
 * Placeholder home screen: a status bar with the battery level (proving the gauge -> monitor ->
 * UI path) over a body that lists degraded features and echoes the last input event (proving
 * the wheel -> input task -> UI path). Stage 2 replaces the body with the real menu.
 */
#include <stdio.h>

#include "ui_internal.h"

#define BAR_H 28

static lv_obj_t *g_battery;
static lv_obj_t *g_input;
static int g_shown_pct = -2; // -2 = never drawn, -1 = "--"

lv_obj_t *ui_home_create(const capabilities_t *caps) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, UI_W, BAR_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COL_BAR), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "neuPod");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_center(title);

    g_battery = lv_label_create(bar);
    lv_obj_set_style_text_font(g_battery, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_battery, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_align(g_battery, LV_ALIGN_RIGHT_MID, -8, 0);
    g_shown_pct = -2;
    ui_home_set_battery(false, NULL);

    // Body: which features are unavailable (from the boot self-check).
    char list[160];
    size_t n = 0;
    list[0] = '\0';
    unsigned bad = 0;
    for (int i = 0; i < CAP_COUNT; i++) {
        if (caps->state[i] != CAP_OK) {
            bad++;
            n += (size_t)snprintf(list + n, sizeof list - n, "%s%s", n ? ", " : "", cap_name((cap_id_t)i));
            if (n >= sizeof list) {
                n = sizeof list - 1;
            }
        }
    }
    lv_obj_t *head = lv_label_create(scr);
    lv_label_set_text(head, "Home");
    lv_obj_set_style_text_font(head, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(head, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *sub = lv_label_create(scr);
    lv_obj_set_width(sub, UI_W - 24);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(bad ? UI_COL_ERR : UI_COL_DIM), 0);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    if (bad) {
        lv_label_set_text_fmt(sub, "Unavailable: %s", list);
    } else {
        lv_label_set_text(sub, "All hardware checks passed");
    }
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 116);

    g_input = lv_label_create(scr);
    lv_label_set_text(g_input, "");
    lv_obj_set_style_text_font(g_input, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_input, lv_color_hex(UI_COL_DIM), 0);
    lv_obj_align(g_input, LV_ALIGN_BOTTOM_MID, 0, -10);
    return scr;
}

void ui_home_set_battery(bool have, const hal_battery_status_t *bat) {
    int pct = have ? (int)bat->soc_pct : -1;
    if (pct == g_shown_pct) {
        return; // only touch LVGL when the text changes: no needless redraw
    }
    g_shown_pct = pct;
    const char *sym = LV_SYMBOL_BATTERY_EMPTY;
    if (have) {
        sym = pct > 85   ? LV_SYMBOL_BATTERY_FULL
              : pct > 60 ? LV_SYMBOL_BATTERY_3
              : pct > 35 ? LV_SYMBOL_BATTERY_2
              : pct > 10 ? LV_SYMBOL_BATTERY_1
                         : LV_SYMBOL_BATTERY_EMPTY;
        lv_label_set_text_fmt(g_battery, "%s %d%%", sym, pct);
    } else {
        lv_label_set_text_fmt(g_battery, "%s --", sym);
    }
}

void ui_home_set_input(const input_event_t *ev) {
    static const char *const names[] = {"select", "right", "left", "play", "menu"};
    const char *name = "?";
    for (unsigned i = 0; i < 5; i++) {
        if (ev->button == (1u << i)) {
            name = names[i];
        }
    }
    switch (ev->type) {
    case INPUT_BUTTON_DOWN:
        lv_label_set_text_fmt(g_input, "Input: %s pressed", name);
        break;
    case INPUT_BUTTON_UP:
        lv_label_set_text_fmt(g_input, "Input: %s released", name);
        break;
    case INPUT_SCROLL:
        lv_label_set_text_fmt(g_input, "Input: scroll %d", (int)ev->steps);
        break;
    }
}
