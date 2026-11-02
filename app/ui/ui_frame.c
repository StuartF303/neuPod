// SPDX-License-Identifier: MPL-2.0
/*
 * Screen framework: one LVGL screen holds a persistent status bar (title, play state, battery) and a
 * content area; each ui_screen_t owns a root object inside the content area. Push hides the current
 * screen and shows the new one, pop destroys the top and shows the one below. Menu = back, Select = enter
 * (screens decide what Select does). No screen-load animation: the old screen stays put, so a push costs
 * one repaint of the content area.
 */
#include <stdio.h>
#include <string.h>

#include "battery_store.h"
#include "capabilities.h"
#include "hal_display.h"
#include "haptic_service.h"
#include "player.h"
#include "settings_store.h"
#include "ui_internal.h"

ui_state_t g_ui_state = {.click_feedback = true, .backlight_pct = 100};

void ui_apply_backlight(void) { hal_display_backlight((uint8_t)((g_ui_state.backlight_pct * 255u + 50u) / 100u)); }

void ui_state_persist(void) {
    settings_set_u8(SETTING_BACKLIGHT_PCT, g_ui_state.backlight_pct);
    settings_set_u8(SETTING_CLICK_FEEDBACK, g_ui_state.click_feedback ? 1u : 0u);
}

// The settings store is loaded from the card some time after boot (storage task): when its contents are replaced, take
// the stored backlight and click feedback. Out-of-range stored values are ignored (kept at the defaults).
static uint32_t g_settings_gen_seen;
static void sync_from_settings(void) {
    uint32_t gen = settings_generation();
    if (gen == g_settings_gen_seen) {
        return;
    }
    g_settings_gen_seen = gen;
    uint8_t v;
    if (settings_get_u8(SETTING_BACKLIGHT_PCT, &v) && v >= 5 && v <= 100) {
        g_ui_state.backlight_pct = v;
        ui_apply_backlight();
    }
    if (settings_get_u8(SETTING_CLICK_FEEDBACK, &v) && v <= 1) {
        g_ui_state.click_feedback = v != 0;
    }
}

static lv_obj_t *g_content;
static lv_obj_t *g_title;
static lv_obj_t *g_play;
static lv_obj_t *g_batt;
static ui_stack_t g_stack;
static ui_accel_t g_accel;
static int g_shown_pct = -3;
static bool g_shown_charging;
static uint32_t g_batt_col = UI_L_DIM;

/* Playback state for the status bar glyph. Nothing plays yet (no decoder), so: idle. */
static const char *play_glyph(void) { return ""; }

static void set_title(const char *t) { lv_label_set_text(g_title, t); }

void ui_frame_set_title(const char *t) { set_title(t); }

void ui_frame_init(lv_obj_t *scr) {
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_L_BG), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, UI_W, UI_STATUS_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_L_BAR_TOP), 0);
    lv_obj_set_style_bg_grad_color(bar, lv_color_hex(UI_L_BAR_BOT), 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(UI_L_LINE), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);

    g_title = ui_label(bar, "", UI_L_TEXT);
    lv_obj_set_size(g_title, 190, 18);
    lv_obj_set_style_text_align(g_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_title, LV_LABEL_LONG_DOT);
    lv_obj_align(g_title, LV_ALIGN_CENTER, 0, 0);

    g_play = ui_label(bar, play_glyph(), UI_L_DIM);
    lv_obj_align(g_play, LV_ALIGN_LEFT_MID, 8, 0);

    g_batt = ui_label(bar, LV_SYMBOL_BATTERY_EMPTY " --", UI_L_DIM);
    lv_obj_align(g_batt, LV_ALIGN_RIGHT_MID, -8, 0);

    g_content = lv_obj_create(scr);
    lv_obj_remove_style_all(g_content);
    lv_obj_set_size(g_content, UI_W, UI_CONTENT_H);
    lv_obj_set_pos(g_content, 0, UI_STATUS_H);
    lv_obj_set_scrollable(g_content, false);

    ui_stack_init(&g_stack);
    ui_accel_init(&g_accel);
}

ui_screen_t *ui_screen_new(size_t size, const char *title) {
    ui_screen_t *s = lv_malloc(size);
    if (s == NULL) {
        return NULL;
    }
    memset(s, 0, size);
    s->title = title;
    s->root = lv_obj_create(g_content);
    lv_obj_remove_style_all(s->root);
    lv_obj_set_size(s->root, UI_W, UI_CONTENT_H);
    lv_obj_set_pos(s->root, 0, 0);
    lv_obj_set_scrollable(s->root, false);
    lv_obj_set_hidden(s->root, true);
    return s;
}

void ui_frame_push(ui_screen_t *s) {
    if (s == NULL) {
        return;
    }
    ui_screen_t *cur = ui_stack_top(&g_stack);
    if (!ui_stack_push(&g_stack, s)) {
        if (s->destroy != NULL) {
            s->destroy(s);
        }
        lv_obj_delete(s->root);
        lv_free(s);
        return;
    }
    if (cur != NULL) {
        lv_obj_set_hidden(cur->root, true);
    }
    lv_obj_set_hidden(s->root, false);
    set_title(s->title);
}

void ui_frame_pop(void) {
    if (g_stack.depth <= 1) {
        return; // the home screen never goes away
    }
    ui_screen_t *s = ui_stack_pop(&g_stack);
    if (s->destroy != NULL) {
        s->destroy(s);
    }
    lv_obj_delete(s->root);
    lv_free(s);
    ui_screen_t *now = ui_stack_top(&g_stack);
    lv_obj_set_hidden(now->root, false);
    set_title(now->title);
}

int ui_frame_depth(void) { return g_stack.depth; }

void ui_frame_input(const input_event_t *ev, uint32_t now_ms) {
    ui_screen_t *top = ui_stack_top(&g_stack);
    if (top == NULL) {
        return;
    }
    switch (ev->type) {
    case INPUT_SCROLL: {
        int32_t steps = ui_accel_feed(&g_accel, ev->steps, now_ms);
        if (steps != 0 && top->on_scroll != NULL) {
            top->on_scroll(top, steps);
        }
        break;
    }
    case INPUT_BUTTON_DOWN:
        if (top->on_button != NULL && top->on_button(top, ev->button)) {
            break;
        }
        if (ev->button == HAL_BTN_MENU) {
            ui_frame_pop(); // Menu = back (ignored on the home screen); playback continues
        } else if (ev->button == HAL_BTN_PLAY) {
            player_pause_toggle(); // Play/Pause works on every screen
        }
        break;
    case INPUT_BUTTON_UP:
        break;
    }
}

static void update_battery(void) {
    hal_battery_status_t b;
    bool have = battery_store_get(&b);
    int pct = have ? (int)b.soc_pct : -1;
    bool charging = have && b.rate_mpct_per_h > 2000;
    if (pct == g_shown_pct && charging == g_shown_charging) {
        return; // touch LVGL only when something changed
    }
    g_shown_pct = pct;
    g_shown_charging = charging;
    if (!have) {
        lv_label_set_text(g_batt, LV_SYMBOL_BATTERY_EMPTY " --");
        ui_label_recolour(g_batt, g_batt_col, UI_L_DIM);
        g_batt_col = UI_L_DIM;
        return;
    }
    const char *sym = charging ? LV_SYMBOL_CHARGE
                      : pct > 85 ? LV_SYMBOL_BATTERY_FULL
                      : pct > 60 ? LV_SYMBOL_BATTERY_3
                      : pct > 35 ? LV_SYMBOL_BATTERY_2
                      : pct > 10 ? LV_SYMBOL_BATTERY_1
                                 : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(g_batt, "%s %d%%", sym, pct);
    uint32_t col = charging ? UI_L_OK : pct <= 15 ? UI_L_ERR : UI_L_DIM;
    ui_label_recolour(g_batt, g_batt_col, col);
    g_batt_col = col;
}

void ui_frame_tick(uint32_t now_ms) {
    (void)now_ms;
    sync_from_settings();
    update_battery();
    ui_screen_t *top = ui_stack_top(&g_stack);
    if (top != NULL && top->on_tick != NULL) {
        top->on_tick(top);
    }
}

void ui_feedback_step(void) {
    if (g_ui_state.click_feedback && caps_available(CAP_HAPTICS)) {
        rt_haptic_click();
    }
}

// Labels share styles instead of carrying their own: every local style property costs heap in LVGL's
// 96 KB pool, and a list screen has dozens of labels. One font style, one style per colour in use.
static lv_style_t g_font14;
static bool g_font14_ready;
#define MAX_COLOUR_STYLES 16
static struct {
    uint32_t colour;
    lv_style_t style;
} g_colour[MAX_COLOUR_STYLES];
static int g_ncolour;

static lv_style_t *colour_style(uint32_t colour) {
    for (int i = 0; i < g_ncolour; i++) {
        if (g_colour[i].colour == colour) {
            return &g_colour[i].style;
        }
    }
    if (g_ncolour >= MAX_COLOUR_STYLES) {
        return &g_colour[0].style; // out of slots: fall back (cannot happen with the current palette)
    }
    g_colour[g_ncolour].colour = colour;
    lv_style_init(&g_colour[g_ncolour].style);
    lv_style_set_text_color(&g_colour[g_ncolour].style, lv_color_hex(colour));
    return &g_colour[g_ncolour++].style;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, uint32_t colour) {
    if (!g_font14_ready) {
        lv_style_init(&g_font14);
        lv_style_set_text_font(&g_font14, &lv_font_montserrat_14);
        g_font14_ready = true;
    }
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_add_style(l, &g_font14, 0);
    lv_obj_add_style(l, colour_style(colour), 0);
    return l;
}

void ui_label_recolour(lv_obj_t *l, uint32_t from, uint32_t to) {
    if (from == to) {
        return;
    }
    lv_obj_remove_style(l, colour_style(from), 0);
    lv_obj_add_style(l, colour_style(to), 0);
}

void ui_format_size(uint64_t bytes, char *out, size_t n) {
    if (bytes < 1024u) {
        snprintf(out, n, "%u B", (unsigned)bytes);
    } else if (bytes < 1024u * 1024u) {
        snprintf(out, n, "%u.%u KB", (unsigned)(bytes / 1024u), (unsigned)((bytes % 1024u) * 10u / 1024u));
    } else if (bytes < 1024ull * 1024u * 1024u) {
        uint64_t mb10 = bytes * 10u / (1024u * 1024u);
        snprintf(out, n, "%u.%u MB", (unsigned)(mb10 / 10u), (unsigned)(mb10 % 10u));
    } else {
        uint64_t gb10 = bytes * 10u / (1024ull * 1024u * 1024u);
        snprintf(out, n, "%u.%u GB", (unsigned)(gb10 / 10u), (unsigned)(gb10 % 10u));
    }
}
