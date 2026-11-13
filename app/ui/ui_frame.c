// SPDX-License-Identifier: MPL-2.0
/*
 * Screen framework: one LVGL screen holds a persistent status bar (left context slot, title, Bluetooth / Wi-Fi / battery) and a
 * content area; each ui_screen_t owns a root object inside the content area. Push hides the current
 * screen and shows the new one, pop destroys the top and shows the one below. Menu = back, Select = enter
 * (screens decide what Select does). A push slides the new screen in 24 px from the right over 120 ms, a pop slides the revealed one in
 * from the left over 100 ms (DS-7): the content area is repainted a few times, nothing is blended. The toast and HUD strips and the
 * Hold pill are in ui_strips.c.
 */
#include <stdio.h>
#include <string.h>

#include "audio_route.h"
#include "battery_store.h"
#include "bt_saved.h"
#include "bt_control.h"
#include "bt_service.h"
#include "capabilities.h"
#include "hal_display.h"
#include "hal_radio.h"
#include "haptic_logic.h"
#include "haptic_service.h"
#include "hal_system.h"
#include "os.h"
#include "player.h"
#include "runtime_config.h"
#include "settings_store.h"
#include "ui_internal.h"
#include "ui_strips.h"
#include "wifi_service.h"

ui_state_t g_ui_state = {.click_mode = CLICK_HAPTIC,
                         .backlight_pct = 100,
                         .autodim = true,
                         .dim_idx = UI_AUTODIM_DEFAULT_IDX,
                         .off_idx = UI_SCREENOFF_DEFAULT_IDX,
                         .autolock = true,
                         .theme = UI_THEME_LIGHT};

static ui_idle_t g_idle;
static ui_hold_t g_hold;
static bool g_idle_inhibit;

void ui_idle_inhibit(bool on) { g_idle_inhibit = on; }

void ui_apply_backlight(void) {
    hal_display_backlight((uint8_t)((ui_backlight_level(g_ui_state.backlight_pct, g_idle.dimmed, g_idle.off) * 255u + 50u) / 100u));
}

static void backlight_transition(void) {
    ui_apply_backlight();
    ui_ticker_set_active(!g_idle.off); /* nothing scrolls on a dark panel */
    printf("ui: backlight %s %u%%\n", g_idle.off ? "off" : g_idle.dimmed ? "dim" : "wake",
           (unsigned)ui_backlight_level(g_ui_state.backlight_pct, g_idle.dimmed, g_idle.off));
}

void ui_state_persist(void) {
    settings_set_u8(SETTING_BACKLIGHT_PCT, g_ui_state.backlight_pct);
    settings_set_u8(SETTING_CLICK_FEEDBACK, (uint8_t)g_ui_state.click_mode);
    settings_set_u8(SETTING_AUTODIM, g_ui_state.autodim ? 1u : 0u);
    settings_set_u8(SETTING_AUTODIM_IDX, g_ui_state.dim_idx);
    settings_set_u8(SETTING_SCREENOFF_IDX, g_ui_state.off_idx);
    settings_set_u8(SETTING_AUTOLOCK, g_ui_state.autolock ? 1u : 0u);
    settings_set_u8(SETTING_THEME, (uint8_t)g_ui_state.theme);
}

// Light or dark: the stored mode, and for Auto whether the backlight has dimmed (or gone off). The switch repaints the screen once, before a wake-up
// raises the backlight (ui_frame_input) and as the dim starts, so nobody sees the old theme at the wrong brightness.
void ui_apply_theme(void) {
    const ui_theme_t *want = ui_theme_is_dark(g_ui_state.theme, g_idle.dimmed || g_idle.off) ? &k_ui_theme_dark : &k_ui_theme_light;
    if (want != ui_theme()) {
        ui_theme_set(want);
        lv_refr_now(NULL);
    }
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
    click_mode_t m;
    if (settings_get_u8(SETTING_CLICK_FEEDBACK, &v) && click_mode_from_stored(v, &m)) {
        g_ui_state.click_mode = m; /* an old stored 0 / 1 is Off / Haptic, as before */
    }
    if (settings_get_u8(SETTING_AUTODIM, &v) && v <= 1) {
        g_ui_state.autodim = v == 1; /* the next tick dims or wakes as needed */
    }
    if (settings_get_u8(SETTING_AUTODIM_IDX, &v) && v < UI_AUTODIM_STEPS) {
        g_ui_state.dim_idx = v;
    }
    if (settings_get_u8(SETTING_SCREENOFF_IDX, &v) && v < UI_SCREENOFF_STEPS) {
        g_ui_state.off_idx = v;
    }
    if (settings_get_u8(SETTING_AUTOLOCK, &v) && v <= 1) {
        g_ui_state.autolock = v == 1;
    }
    ui_theme_mode_t tm;
    if (settings_get_u8(SETTING_THEME, &v) && ui_theme_mode_from_stored(v, &tm)) {
        g_ui_state.theme = tm;
        ui_apply_theme();
    }
}

static lv_obj_t *g_content;
static lv_obj_t *g_title;
static lv_obj_t *g_left;     /* the left context slot (Small, ink2): clock, "n of m", a count, a path */
static lv_obj_t *g_dots[3];  /* three busy dots that take the slot's place while the library builds in the background (LB-4) */
static int g_dots_phase = -1; /* -1: not showing */
static uint32_t g_dots_col[3];
static lv_obj_t *g_right;    /* the right-hand group, a flex row: Wi-Fi, Bluetooth, lock, battery percentage, battery */
static lv_obj_t *g_wifi, *g_bt, *g_lock, *g_pct;
static lv_obj_t *g_batt, *g_batt_fill; /* the drawn battery: outline with a terminal, and the fill inside it */
static lv_obj_t *g_frame_scr;          /* the screen the frame lives on (the strips are children of it) */
static uint32_t g_notice_seen, g_skip_seen;
static int g_bt_shown = -1;
static uint32_t g_bt_col = UI_C_INK3;
static hal_wifi_status g_wifi_shown = (hal_wifi_status)99;
static uint32_t g_wifi_col = UI_C_INK2;
static ui_stack_t g_stack;
static ui_accel_t g_accel;
static int g_shown_pct = -3;
static bool g_shown_charging;
static uint32_t g_batt_col = UI_C_INK;
static uint32_t g_pct_col = UI_C_INK2;
static uint32_t g_htoast_until; /* a confirmation or the "Locked" reminder goes away at this time (0 = none showing) */
static lv_obj_t *g_slide_root; /* the screen root that is sliding in (DS-7), NULL when none */

static void apply_left(const ui_screen_t *s) { ui_label_set_text(g_left, s != NULL ? s->left : ""); }

void ui_screen_set_left(ui_screen_t *s, const char *text) {
    if (s == NULL || strcmp(s->left, text) == 0) {
        return;
    }
    snprintf(s->left, sizeof s->left, "%s", text);
    if (s == ui_stack_top(&g_stack)) {
        apply_left(s);
    }
}

static void set_title(const char *t) { ui_label_set_text(g_title, t); }

void ui_frame_set_title(const char *t) { set_title(t); }

void ui_frame_init(lv_obj_t *scr) {
    lv_obj_set_scrollable(scr, false);
    ui_obj_bg(scr, UI_C_BG, 0);

    // Stacking order, bottom to top: content, the toast / HUD / Hold strips (the toast slides out from under the bar), the status bar, the Hold line.
    g_content = lv_obj_create(scr);
    lv_obj_remove_style_all(g_content);
    lv_obj_set_size(g_content, UI_W, UI_CONTENT_H);
    lv_obj_set_pos(g_content, 0, UI_STATUS_H);
    lv_obj_set_scrollable(g_content, false);

    ui_strips_init(scr);

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, UI_W, UI_STATUS_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_scrollable(bar, false);
    ui_obj_bg(bar, UI_C_BAR, 0);
    ui_obj_border(bar, UI_C_LINE, 1, LV_BORDER_SIDE_BOTTOM);

    g_title = ui_label(bar, "", UI_C_INK);
    lv_obj_set_size(g_title, UIM->title_w, UIM->title_h);
    lv_obj_set_style_text_align(g_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_title, LV_LABEL_LONG_DOT);
    ui_label_ticker(g_title, true); /* a long title scrolls */
    lv_obj_align(g_title, LV_ALIGN_CENTER, 0, 0);

    g_left = ui_label_small(bar, "", UI_C_INK2);
    lv_obj_set_size(g_left, UIM->slot_w, lv_font_get_line_height(UIM->font_small)); /* one line: a long path ends in dots instead of wrapping */
    lv_label_set_long_mode(g_left, LV_LABEL_LONG_DOT);
    lv_obj_align(g_left, LV_ALIGN_LEFT_MID, UIM->bar_inset, 0);

    for (int i = 0; i < 3; i++) { /* busy dots: one bright, the others dim, moving along (the same dots the list rows use) */
        g_dots[i] = lv_obj_create(bar);
        lv_obj_remove_style_all(g_dots[i]);
        lv_obj_set_size(g_dots[i], UIM->glyph_bar_w, UIM->glyph_bar_w);
        lv_obj_set_style_radius(g_dots[i], UIM->glyph_bar_w / 2, 0);
        lv_obj_align(g_dots[i], LV_ALIGN_LEFT_MID, UIM->bar_inset + i * UIM->glyph_step * 2, 0);
        g_dots_col[i] = UI_C_INK3;
        ui_obj_bg(g_dots[i], g_dots_col[i], 0);
        lv_obj_set_hidden(g_dots[i], true);
    }
    g_dots_phase = -1;

    // Right-hand group: hidden members take no room, so the rest closes up (flex row, right-aligned).
    g_right = lv_obj_create(bar);
    lv_obj_remove_style_all(g_right);
    lv_obj_set_size(g_right, LV_SIZE_CONTENT, UI_STATUS_H - 2);
    lv_obj_set_scrollable(g_right, false);
    lv_obj_set_flex_flow(g_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_right, UIM->bar_gap, 0);
    lv_obj_align(g_right, LV_ALIGN_RIGHT_MID, -UIM->bar_inset, 0);
    g_wifi = ui_icon(g_right, UI_ICON_WIFI, false, UI_C_INK2); /* Wi-Fi state: hidden when the station is off, dim while joining, green when up */
    lv_obj_set_hidden(g_wifi, true);
    g_bt = ui_icon(g_right, UI_ICON_BLUETOOTH, false, UI_C_INK3);
    lv_obj_set_hidden(g_bt, true);
    g_lock = ui_icon(g_right, UI_ICON_LOCK, false, UI_C_INK); /* Hold is on */
    lv_obj_set_hidden(g_lock, true);
    g_pct = ui_label_small(g_right, "", UI_C_INK2); /* the battery percentage: only below 20 % or while charging */
    lv_obj_set_hidden(g_pct, true);

    g_batt = lv_obj_create(g_right); /* battery: outline, terminal, fill (drawn shapes, not a glyph) */
    lv_obj_remove_style_all(g_batt);
    lv_obj_set_size(g_batt, UIM->batt_w + UIM->batt_nub_w, UIM->batt_h);
    lv_obj_set_scrollable(g_batt, false);
    lv_obj_t *body = lv_obj_create(g_batt);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, UIM->batt_w, UIM->batt_h);
    lv_obj_set_pos(body, 0, 0);
    lv_obj_set_style_radius(body, 2, 0);
    ui_obj_border(body, UI_C_INK2, 1, LV_BORDER_SIDE_FULL);
    lv_obj_t *nub = lv_obj_create(g_batt);
    lv_obj_remove_style_all(nub);
    lv_obj_set_size(nub, UIM->batt_nub_w, UIM->batt_nub_h);
    lv_obj_set_pos(nub, UIM->batt_w, (UIM->batt_h - UIM->batt_nub_h) / 2);
    ui_obj_bg(nub, UI_C_INK2, 0);
    g_batt_fill = lv_obj_create(g_batt);
    lv_obj_remove_style_all(g_batt_fill);
    lv_obj_set_size(g_batt_fill, 1, UIM->batt_h - 4);
    lv_obj_set_pos(g_batt_fill, 2, 2);
    g_batt_col = UI_C_INK;
    ui_obj_bg(g_batt_fill, g_batt_col, 0);
    lv_obj_set_hidden(g_batt_fill, true);

    ui_strips_init_line(scr);

    g_frame_scr = scr;
    g_slide_root = NULL;
    g_shown_pct = -3; /* the bar of an earlier init (tests) is gone: draw the battery again */
    g_bt_shown = -1;
    g_wifi_shown = (hal_wifi_status)99;
    g_bt_col = UI_C_INK3; /* the colours the new objects were created with */
    g_wifi_col = UI_C_INK2;
    g_pct_col = UI_C_INK2;
    g_htoast_until = 0;
    ui_stack_init(&g_stack);
    ui_accel_init(&g_accel);
    ui_idle_init(&g_idle, os_tick_ms());
    ui_hold_init(&g_hold);
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

void ui_frame_suspend_top(void) {
    ui_screen_t *t = ui_stack_top(&g_stack);
    if (t != NULL && !t->suspended && t->suspend != NULL) {
        t->suspend(t);
        t->suspended = true;
    }
}

void ui_frame_resume_top(void) {
    ui_screen_t *t = ui_stack_top(&g_stack);
    if (t != NULL && t->suspended && t->resume != NULL) {
        t->resume(t);
    }
    if (t != NULL) {
        t->suspended = false;
    }
}

static void slide_x_cb(void *o, int32_t x) { lv_obj_set_x(o, x); }

static void slide_done(lv_anim_t *a) {
    if (g_slide_root == a->var) {
        g_slide_root = NULL;
    }
}

static void slide_settle(void) {
    if (g_slide_root != NULL) {
        lv_anim_delete(g_slide_root, slide_x_cb);
        lv_obj_set_x(g_slide_root, 0);
        g_slide_root = NULL;
    }
}

// DS-7: the screen that appears slides 24 px into place; one repaint of the content area per frame, no alpha. Not while the screen is off.
static void slide_in(lv_obj_t *root, int32_t from_x, uint32_t ms) {
    slide_settle();
    if (g_idle.off) {
        return;
    }
    g_slide_root = root;
    lv_obj_set_x(root, from_x);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root);
    lv_anim_set_exec_cb(&a, slide_x_cb);
    lv_anim_set_values(&a, from_x, 0);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, slide_done);
    lv_anim_start(&a);
}

void ui_frame_settle(void) {
    ui_ticker_restart(); /* a snapshot shows every ticker at its start */
    slide_settle();
    ui_strips_settle();
}

bool ui_frame_push(ui_screen_t *s) {
    if (s == NULL) {
        return false;
    }
    ui_screen_t *cur = ui_stack_top(&g_stack);
    if (!ui_stack_push(&g_stack, s)) {
        if (s->destroy != NULL) {
            s->destroy(s);
        }
        lv_obj_delete(s->root);
        lv_free(s);
        return false;
    }
    if (cur != NULL) {
        lv_obj_set_hidden(cur->root, true);
        if (!cur->suspended && cur->suspend != NULL) {
            cur->suspend(cur);
            cur->suspended = true;
        }
    }
    lv_obj_set_hidden(s->root, false);
    set_title(s->title);
    apply_left(s);
    if (cur != NULL) {
        slide_in(s->root, 24, 120); /* the first screen (Home) just appears */
    }
    ui_log_screen(s->title, "push", NULL);
    return true;
}

void ui_frame_pop(void) {
    if (g_stack.depth <= 1) {
        return; // the home screen never goes away
    }
    ui_screen_t *s = ui_stack_pop(&g_stack);
    char popped[48]; /* the title may point into the screen being freed */
    snprintf(popped, sizeof popped, "%s", s->title != NULL ? s->title : "");
    if (s->destroy != NULL) {
        s->destroy(s);
    }
    slide_settle();
    lv_obj_delete(s->root);
    lv_free(s);
    ui_screen_t *now = ui_stack_top(&g_stack);
    if (now->suspended && now->resume != NULL) {
        now->resume(now);
    }
    now->suspended = false;
    lv_obj_set_hidden(now->root, false);
    set_title(now->title);
    apply_left(now);
    slide_in(now->root, -24, 100); /* the revealed screen slides in from the left (mirror of the push; the popped one is already gone) */
    ui_log_screen(popped, "pop", now->title);
}

int ui_frame_depth(void) { return g_stack.depth; }

const char *ui_frame_top_title(void) {
    ui_screen_t *t = ui_stack_top(&g_stack);
    return t != NULL && t->title != NULL ? t->title : "";
}

// A folder queue is playing (or opening / paused): Left / Right skip tracks on every screen that leaves them alone.
static bool queue_active(void) {
    player_info_t pi;
    player_get(&pi);
    return pi.src == PLAYER_SRC_FILE && pi.queue_len != 0 &&
           (pi.state == PLAYER_PLAYING || pi.state == PLAYER_PAUSED || pi.state == PLAYER_OPENING);
}

// ---- Hold (docs/adr/0020-hold.md): the lock pill, the progress line, the gestures and the input filter --------------------------------------
// HL-1: a dark pill at the bottom says what Hold is doing, and a line under the status bar shows a Power press filling towards the toggle (accent) or
// the power-off (red). The content behind is never dimmed: only the pill and the line are drawn.

static uint8_t g_swallowed;     /* buttons whose press was swallowed: their release is too */
static uint8_t g_delivered;     /* buttons whose press reached a screen: their release does too, even if Hold came on in between */

bool ui_frame_locked(void) { return g_hold.locked; }
bool ui_locked(void) { return __atomic_load_n(&g_hold.locked, __ATOMIC_RELAXED); }

static void hold_overlay_hide(void) {
    ui_pill_hide();
    ui_hold_line_set(-1, false);
    g_htoast_until = 0;
}

static void hold_toast(const char *text, uint32_t now_ms) {
    ui_hold_line_set(-1, false);
    ui_pill_show(text);
    g_htoast_until = now_ms + UI_HOLD_TOAST_MS;
    if (g_htoast_until == 0) {
        g_htoast_until = 1;
    }
}

// How to get Hold off again, for the reminder: platforms without a Power key are driven from the console.
static const char *unlock_hint(void) { return hal_input_has_power_key() ? "Locked \xC2\xB7 hold Power 2 s to unlock" : "Locked \xC2\xB7 console key l unlocks"; }

static void hold_reminder(uint32_t now_ms) { hold_toast(unlock_hint(), now_ms); }

// Carry out what ui_hold said happened. Silent (no pill, no click) while the screen is off: nobody is looking.
static void hold_apply(uint32_t r, uint32_t now_ms) {
    if ((r & UI_HOLD_CANCELLED) != 0 && g_htoast_until == 0) {
        hold_overlay_hide(); /* a ring or countdown was up; a confirmation that follows is shown below */
    }
    if ((r & (UI_HOLD_LOCKED | UI_HOLD_UNLOCKED)) != 0) {
        bool locked = (r & UI_HOLD_LOCKED) != 0;
        printf("ui: hold %s\n", locked ? "on" : "off");
        if (g_lock != NULL) {
            lv_obj_set_hidden(g_lock, !locked);
        }
        if (!g_idle.off) {
            hold_toast(locked ? "Locked" : "Unlocked", now_ms);
            ui_feedback_step(); /* the click of the slide switch */
        }
    }
    if ((r & UI_HOLD_SLEEP) != 0 && ui_idle_force_off(&g_idle)) {
        hold_overlay_hide();
        backlight_transition();
        if (g_ui_state.autolock) {
            hold_apply(ui_hold_set(&g_hold, true), now_ms);
        }
    }
    if ((r & UI_HOLD_POWEROFF) != 0) {
        printf("ui: power-off requested\n");
        ui_hold_line_set(-1, false);
        ui_pill_show("Powering off");
        g_htoast_until = 0;
        if (!hal_system_poweroff()) {
            hold_overlay_hide();
        }
    }
}

// The progress of a Power press that is still down, drawn once per UI pass.
static void hold_draw_progress(uint32_t now_ms) {
    ui_hold_progress_t p = ui_hold_progress(&g_hold, now_ms, hal_system_can_poweroff());
    char t[48];
    switch (p.phase) {
    case UI_HOLD_P_RING:
        if (hal_system_can_poweroff()) {
            snprintf(t, sizeof t, "Keep holding to %s \xC2\xB7 10 s = off", p.to_lock ? "lock" : "unlock");
        } else {
            snprintf(t, sizeof t, "Keep holding to %s", p.to_lock ? "lock" : "unlock");
        }
        ui_pill_show(t);
        ui_hold_line_set(p.pct, false);
        g_htoast_until = 0;
        break;
    case UI_HOLD_P_COUNTDOWN:
        snprintf(t, sizeof t, "Release to cancel \xC2\xB7 shutting down in %u", (unsigned)p.secs);
        ui_pill_show(t);
        ui_hold_line_set(100 - p.pct, true);
        g_htoast_until = 0;
        break;
    case UI_HOLD_P_POWEROFF:
    case UI_HOLD_P_NONE:
        break;
    }
}

// Is this event one the Hold filter judges (wheel and buttons)? The Power key, the switch and the console toggle are Hold's own.
static bool is_user_input(const input_event_t *ev) { return ev->type == INPUT_SCROLL || ev->type == INPUT_BUTTON_DOWN || ev->type == INPUT_BUTTON_UP; }

void ui_frame_input(const input_event_t *ev, uint32_t now_ms) {
    bool was_off = g_idle.off;
    if (ui_idle_activity(&g_idle, now_ms)) {
        ui_apply_theme(); /* Auto: back to light before the backlight comes up */
        backlight_transition(); /* before the screen sees the event: a slider handler re-applies the level */
    }
    switch (ev->type) {
    case INPUT_POWER_DOWN:
        hold_apply(ui_hold_power_down(&g_hold, now_ms, was_off), now_ms);
        return;
    case INPUT_POWER_UP:
        hold_apply(ui_hold_power_up(&g_hold, now_ms), now_ms);
        return;
    case INPUT_HOLD_SWITCH:
        hold_apply(ui_hold_set(&g_hold, ev->steps != 0), now_ms);
        return;
    case INPUT_HOLD_TOGGLE:
        hold_apply(ui_hold_toggle(&g_hold), now_ms);
        return;
    default:
        break;
    }
    if (is_user_input(ev)) {
        // The screen was off: this input only wakes it. Locked: it only reminds. Neither reaches a screen, so neither can act or click.
        bool swallow = was_off || g_hold.locked;
        if (ev->type == INPUT_BUTTON_UP && (g_swallowed & ev->button) != 0) {
            g_swallowed = (uint8_t)(g_swallowed & ~ev->button);
            return;
        }
        if (ev->type == INPUT_BUTTON_UP && (g_delivered & ev->button) != 0) {
            g_delivered = (uint8_t)(g_delivered & ~ev->button);
            swallow = false; /* the screen saw this press: it must see the release (no stuck press-and-hold) */
        }
        if (swallow) {
            if (ev->type == INPUT_BUTTON_DOWN) {
                g_swallowed = (uint8_t)(g_swallowed | ev->button);
            }
            if (g_hold.locked && ev->type != INPUT_BUTTON_UP) {
                hold_reminder(now_ms);
            }
            return;
        }
        if (ev->type == INPUT_BUTTON_DOWN) {
            g_delivered = (uint8_t)(g_delivered | ev->button);
        }
    }
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
        switch (ui_global_key(ev->button, (ev->button == HAL_BTN_LEFT || ev->button == HAL_BTN_RIGHT) && queue_active())) {
        case UI_KEY_BACK: ui_frame_pop(); break; // Menu = back (ignored on the home screen); playback continues
        case UI_KEY_PAUSE: player_pause_toggle(); break; // Play/Pause works on every screen
        case UI_KEY_NEXT: player_next(); break;          // Left / Right skip on every screen that did not use them
        case UI_KEY_PREV: player_prev(); break;
        case UI_KEY_NONE: break;
        }
        break;
    case INPUT_BUTTON_UP:
        if (top->on_button_up != NULL) {
            top->on_button_up(top, ev->button);
        }
        break;
    default:
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
    if (!have) { // no gauge: an empty outline, no number (the review: never "--")
        lv_obj_set_hidden(g_batt_fill, true);
        lv_obj_set_hidden(g_pct, true);
        return;
    }
    int32_t fw = (UIM->batt_w - 4) * (pct > 100 ? 100 : pct) / 100;
    lv_obj_set_width(g_batt_fill, fw < 1 && pct > 0 ? 1 : fw);
    lv_obj_set_hidden(g_batt_fill, pct <= 0);
    uint32_t col = charging ? UI_C_GREEN : pct < 15 ? UI_C_RED : UI_C_INK;
    ui_obj_bg_swap(g_batt_fill, g_batt_col, col, 0);
    g_batt_col = col;
    // The number only when it matters: below 20 %, or while charging.
    bool show = charging || pct < 20;
    if (show) {
        lv_label_set_text_fmt(g_pct, "%d%%", pct);
        uint32_t pc = pct < 15 && !charging ? UI_C_RED : UI_C_INK2;
        ui_label_recolour(g_pct, g_pct_col, pc);
        g_pct_col = pc;
    }
    lv_obj_set_hidden(g_pct, !show);
}

static int g_wifi_bars; /* bars last shown for the joined network (0 = not joined), with hysteresis */

int ui_wifi_bars(void) { return g_wifi_bars; }

static void update_wifi(void) {
    hal_wifi_status st = hal_wifi_state();
    int bars = 0;
    if (st == HAL_WIFI_UP) {
        wifi_info_t wi;
        wifi_get_info(&wi);
        bars = ui_signal_bars(wi.rssi, &k_ui_signal_wifi, g_wifi_bars, UI_SIG_HYST_DB);
    }
    if (st == g_wifi_shown && bars == g_wifi_bars) {
        return;
    }
    g_wifi_shown = st;
    g_wifi_bars = bars;
    int cls = ui_signal_class(bars);
    uint32_t col = st == HAL_WIFI_UP ? (bars == 0 ? UI_C_GREEN : cls == 3 ? UI_C_GREEN : cls == 2 ? UI_C_AMBER : UI_C_RED)
                   : st == HAL_WIFI_FAILED || st == HAL_WIFI_BAD_AUTH ? UI_C_RED : UI_C_INK3;
    lv_obj_set_hidden(g_wifi, st == HAL_WIFI_DOWN);
    ui_label_recolour(g_wifi, g_wifi_col, col);
    g_wifi_col = col;
}

/* Bluetooth glyph of the status bar. 0 hidden (no radio, or nothing remembered and no link), 1 remembered, not connected, 2 connected, 3 streaming. */
static int bt_level(void) {
    hal_bt_state_t st = hal_bt_state();
    if (st != HAL_BT_ON && st != HAL_BT_SCANNING) {
        return 0;
    }
    if (hal_bt_stream_state() == HAL_BT_STREAM_STREAMING) {
        return 3;
    }
    hal_bt_link_state_t l = hal_bt_link_state();
    if (l == HAL_BT_LINK_CONNECTED) {
        return 2;
    }
    return bt_saved_get(NULL) || l != HAL_BT_LINK_DISCONNECTED ? 1 : 0;
}

// DS-3: ink3 = a saved headset that is not connected, ink = connected, accent = streaming; nothing without a radio or a headset.
static void update_bt(void) {
    int lv = bt_level();
    if (lv == g_bt_shown) {
        return;
    }
    g_bt_shown = lv;
    uint32_t col = lv == 3 ? UI_C_ACCENT : lv == 2 ? UI_C_INK : UI_C_INK3;
    lv_obj_set_hidden(g_bt, lv == 0);
    ui_label_recolour(g_bt, g_bt_col, col);
    g_bt_col = col;
}

// BT-2: the link's life as toasts (owner decision 2: playback pauses and stays on Bluetooth; the firmware never switches the output by itself).
// "<name> disconnected . reconnecting..." when a connected headset goes away (warning), "<name> connected" for 1.2 s (info) when the automatic
// reconnect succeeds, "Couldn't reconnect - paused" when its schedule runs out (warning). The name is the remembered headset's, cut so the one-row strip holds it.
#define BT_NAME_CHARS 16u
#define BT_BACK_TOAST_MS 1200u
static uint32_t g_lost_seen, g_back_seen, g_gaveup_seen;

static void bt_toast_name(char *out, size_t n) {
    hal_bt_device_t d;
    char raw[HAL_BT_NAME_MAX + 8];
    raw[0] = '\0';
    if (bt_saved_get(&d)) {
        ui_text_display(raw, sizeof raw, d.name);
    }
    if (raw[0] == '\0') {
        snprintf(out, n, "Headset");
        return;
    }
    size_t i = 0, chars = 0;
    while (raw[i] != '\0' && (chars < BT_NAME_CHARS || ((unsigned char)raw[i] & 0xC0u) == 0x80u)) { /* whole characters only */
        chars += ((unsigned char)raw[i] & 0xC0u) != 0x80u;
        i++;
    }
    const bool cut = raw[i] != '\0';
    snprintf(out, n, "%.*s%s", (int)i, raw, cut ? "\xE2\x80\xA6" : "");
}

static void update_banner(void) {
    char nm[HAL_BT_NAME_MAX + 8], txt[HAL_BT_NAME_MAX + 48];
    uint32_t v = bt_service_lost_seq();
    if (v != g_lost_seen) {
        g_lost_seen = v;
        bt_toast_name(nm, sizeof nm);
        snprintf(txt, sizeof txt, "%s disconnected \xC2\xB7 reconnecting\xE2\x80\xA6", nm);
        ui_toast_show(txt, UI_TOAST_WARN, AUDIO_ROUTE_BANNER_MS);
    }
    v = bt_service_reconnected_seq();
    if (v != g_back_seen) {
        g_back_seen = v;
        bt_toast_name(nm, sizeof nm);
        snprintf(txt, sizeof txt, "%s connected", nm);
        ui_toast_show(txt, UI_TOAST_INFO, BT_BACK_TOAST_MS);
    }
    v = bt_service_gave_up_seq(); // the reconnect schedule ran out (owner decision: playback stays paused, on Bluetooth)
    if (v != g_gaveup_seen) {
        g_gaveup_seen = v;
        ui_toast_show("Couldn't reconnect \xE2\x80\x94 paused", UI_TOAST_WARN, AUDIO_ROUTE_BANNER_MS);
    }
    char why[80];
    v = player_skip_notice(why, sizeof why); // LB-5: the queue skipped a track it cannot play
    if (v != g_skip_seen) {
        g_skip_seen = v;
        const char *r = strncmp(why, "Can't play: ", 12) == 0 ? why + 12 : why;
        snprintf(txt, sizeof txt, "Skipped: %s", r);
        ui_toast_show(txt, UI_TOAST_WARN, 0);
    }
    uint32_t seq;
    audio_route_notice_t n = audio_route_notice(&seq);
    if (seq != g_notice_seen) {
        g_notice_seen = seq;
        if (n == ROUTE_NOTICE_NOT_CONNECTED) {
            ui_toast_show("Headset not connected", UI_TOAST_WARN, AUDIO_ROUTE_BANNER_MS);
        } else if (n == ROUTE_NOTICE_HEADSET_DROPPED && !bt_service_reconnecting()) { /* with a reconnect running, the loss toast already said it */
            bt_toast_name(nm, sizeof nm);
            snprintf(txt, sizeof txt, "%s disconnected", nm);
            ui_toast_show(txt, UI_TOAST_WARN, AUDIO_ROUTE_BANNER_MS);
        }
    }
}

// NP-4: a volume change while another screen is on top (the Y2's side keys, the headset's own volume, the wheel on a screen that does not set it)
// shows the volume HUD for 1.2 s. Screens that draw the volume themselves (Now Playing page 1, Test tone) say so with shows_volume. A change of
// which volume is in use (a headset connecting takes over) is not a volume change and shows nothing; nor does anything while the screen is off or Hold is on.
static int32_t g_vol_att = -1;
static int g_vol_hv = -2;
static uint32_t g_vol_gen = UINT32_MAX, g_vol_quiet_until;
static bool g_vol_quiet;
#define VOL_QUIET_MS 1500u /* after a settings load (the saved volume is restored a moment later) or a change of volume mode (the software volume follows the headset's) */

static void update_volume_hud(void) {
    uint32_t att = player_volume_att();
    int hv = bt_volume_get();
    const bool first = g_vol_att < 0;
    const bool mode_same = (hv >= 0) == (g_vol_hv >= 0);
    const bool changed = hv >= 0 ? hv != g_vol_hv : (int32_t)att != g_vol_att;
    g_vol_att = (int32_t)att;
    g_vol_hv = hv;
    const uint32_t now = os_tick_ms();
    const uint32_t gen = settings_generation();
    if (gen != g_vol_gen || !mode_same) {
        g_vol_gen = gen;
        g_vol_quiet = true;
        g_vol_quiet_until = now + VOL_QUIET_MS;
    }
    if (g_vol_quiet && (int32_t)(now - g_vol_quiet_until) >= 0) {
        g_vol_quiet = false;
    }
    if (first || g_vol_quiet || !mode_same || !changed) {
        return;
    }
    ui_screen_t *top = ui_stack_top(&g_stack);
    if (top == NULL || top->shows_volume || g_hold.locked || g_idle.off) {
        return;
    }
    char label[32];
    int pct;
    if (hv >= 0) {
        pct = (int)bt_vol_percent((uint8_t)hv);
        snprintf(label, sizeof label, "Headset %d%%", pct);
    } else {
        pct = att >= AUDIO_VOL_OFF_DB ? 0 : (int)((AUDIO_VOL_OFF_DB - att) * 100u / AUDIO_VOL_OFF_DB);
        if (att >= AUDIO_VOL_OFF_DB) {
            snprintf(label, sizeof label, "Volume off");
        } else {
            snprintf(label, sizeof label, att == 0 ? "Volume 0 dB" : "Volume -%u dB", (unsigned)att);
        }
    }
    ui_hud_show(label, pct, 0);
}

// The haptic task applies the stored actuator and calibration once the settings are known: when they are (re)loaded,
// and once more when the boot checks have all resolved (a boot with no card never loads them, but storage is then settled).
static void kick_haptics(void) {
    static uint32_t kicked_key = UINT32_MAX;
    if (!caps_available(CAP_HAPTICS)) {
        return;
    }
    uint32_t key = settings_generation() * 2u + (caps_all_resolved() ? 1u : 0u);
    if (key != kicked_key) {
        kicked_key = key;
        rt_haptic_settings_loaded();
    }
}

// LB-4: while the library builds with its screen left, three busy dots replace the left slot on every screen.
static void update_busy_dots(uint32_t now_ms) {
    const bool on = ui_library_background() && !g_idle.off;
    if (!on && g_dots_phase < 0) {
        return;
    }
    if (!on) {
        g_dots_phase = -1;
        for (int i = 0; i < 3; i++) {
            lv_obj_set_hidden(g_dots[i], true);
        }
        lv_obj_set_hidden(g_left, false);
        return;
    }
    int phase = (int)((now_ms / 250u) % 3u);
    if (phase == g_dots_phase) {
        return;
    }
    if (g_dots_phase < 0) {
        lv_obj_set_hidden(g_left, true);
    }
    g_dots_phase = phase;
    for (int i = 0; i < 3; i++) {
        uint32_t c = i == phase ? UI_C_INK : UI_C_INK3;
        ui_obj_bg_swap(g_dots[i], g_dots_col[i], c, 0);
        g_dots_col[i] = c;
        lv_obj_set_hidden(g_dots[i], false);
    }
}

void ui_frame_tick(uint32_t now_ms) {
    sync_from_settings();
    bool dim_changed = ui_idle_tick(&g_idle, now_ms, g_ui_state.autodim, g_idle_inhibit, ui_autodim_seconds(g_ui_state.dim_idx) * 1000u);
    bool was_off = g_idle.off;
    bool off_changed = ui_idle_off_tick(&g_idle, now_ms, g_ui_state.autodim, g_idle_inhibit, ui_autodim_seconds(g_ui_state.dim_idx) * 1000u,
                                        ui_screenoff_seconds(g_ui_state.off_idx) * 1000u);
    if (dim_changed || off_changed) {
        ui_apply_theme(); /* Auto: dark as the backlight dims */
        backlight_transition();
    }
    if (!was_off && g_idle.off) { /* the screen just went off by itself: lock now (docs/adr/0020-hold.md) */
        hold_overlay_hide();
        if (g_ui_state.autolock) {
            hold_apply(ui_hold_set(&g_hold, true), now_ms);
        }
    }
    hold_apply(ui_hold_tick(&g_hold, now_ms, hal_system_can_poweroff()), now_ms);
    hold_draw_progress(now_ms);
    if (g_htoast_until != 0 && (int32_t)(now_ms - g_htoast_until) >= 0 && !g_hold.down) {
        hold_overlay_hide();
    }
    kick_haptics();
    update_wifi();
    update_bt();
    update_banner();
    update_volume_hud();
    update_busy_dots(now_ms);
    update_battery();
    ui_strips_tick(now_ms);
    bt_service_tick(); /* the radio simulator on the host; stale pairing keys; cheap */
    ui_screen_t *top = ui_stack_top(&g_stack);
    if (top != NULL && top->on_tick != NULL) {
        top->on_tick(top);
    }
}

void ui_feedback_step(void) {
    if (click_mode_uses_haptic(g_ui_state.click_mode) && caps_available(CAP_HAPTICS)) {
        rt_haptic_click();
    }
    if (click_mode_uses_piezo(g_ui_state.click_mode)) {
        rt_piezo_click();
    }
}

// Labels share styles instead of carrying their own: every local style property costs heap in LVGL's
// 96 KB pool, and a list screen has dozens of labels. One font style per size, one style per colour in use (ui_theme.c).
static lv_style_t g_font_body; /* Montserrat 14 on the 320x240 profile */
static lv_style_t g_font_small; /* 12 */
static lv_style_t g_font_icon[2]; /* Lucide: small and large */
static bool g_fonts_ready;

static void fonts_init(void) {
    lv_style_init(&g_font_body);
    lv_style_set_text_font(&g_font_body, UIM->font_body);
    lv_style_init(&g_font_small);
    lv_style_set_text_font(&g_font_small, UIM->font_small);
    lv_style_init(&g_font_icon[0]);
    lv_style_set_text_font(&g_font_icon[0], UIM->font_icon_sm);
    lv_style_init(&g_font_icon[1]);
    lv_style_set_text_font(&g_font_icon[1], UIM->font_icon_lg);
    g_fonts_ready = true;
}

// ---- tickers ----
// Text wider than its one-line box scrolls round (LV_LABEL_LONG_SCROLL_CIRCULAR) instead of ending in dots: the focused list row, the Home track
// lines, the status-bar title. LVGL's own label animation does it: it moves only the label's text offset, so only the label's own box is invalidated,
// and a label whose text fits has no animation at all. One shared style carries the speed and the pause (the start delay is the animation template's
// negative time). Unfocused rows keep their dots. The registry lets the screen-off stage and the tours freeze them.
#define TICKERS_MAX 8
#define TICKER_PAUSE_MS 1000
static lv_style_t g_ticker_style;
static lv_anim_t g_ticker_anim;
static bool g_ticker_ready;
static lv_obj_t *g_tickers[TICKERS_MAX];
static bool g_ticker_active = true;

static void ticker_init(void) {
    lv_anim_init(&g_ticker_anim);
    lv_anim_set_delay(&g_ticker_anim, TICKER_PAUSE_MS); /* the first start waits for it; repeats wait for the repeat delay below */
    lv_anim_set_repeat_count(&g_ticker_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&g_ticker_anim, TICKER_PAUSE_MS);
    lv_style_init(&g_ticker_style);
    lv_style_set_anim(&g_ticker_style, &g_ticker_anim);
    lv_style_set_anim_duration(&g_ticker_style, lv_anim_speed_clamped((uint32_t)UIM->ticker_px_s, 300, 10000));
    g_ticker_ready = true;
}

static int ticker_find(const lv_obj_t *l) {
    for (int i = 0; i < TICKERS_MAX; i++) {
        if (g_tickers[i] == l) {
            return i;
        }
    }
    return -1;
}

static void ticker_deleted(lv_event_t *e) {
    int i = ticker_find(lv_event_get_target(e));
    if (i >= 0) {
        g_tickers[i] = NULL;
    }
}

static void ticker_apply(lv_obj_t *l, bool scroll) {
    lv_label_long_mode_t want = scroll ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT;
    if (lv_label_get_long_mode(l) != want || scroll) {
        lv_label_set_long_mode(l, want); /* also rewinds: the old animation is deleted and the offset reset */
    }
}

void ui_label_ticker(lv_obj_t *l, bool on) {
    if (!g_ticker_ready) {
        ticker_init();
    }
    int i = ticker_find(l);
    if (on == (i >= 0)) {
        return; /* no change: never restart a scroll that is running */
    }
    if (on) {
        int slot = ticker_find(NULL);
        if (slot < 0) {
            return; /* registry full: this label keeps its dots */
        }
        g_tickers[slot] = l;
        lv_obj_add_style(l, &g_ticker_style, 0);
        lv_obj_add_event_cb(l, ticker_deleted, LV_EVENT_DELETE, NULL);
        ticker_apply(l, g_ticker_active);
    } else {
        g_tickers[i] = NULL;
        lv_obj_remove_event_cb(l, ticker_deleted);
        lv_obj_remove_style(l, &g_ticker_style, 0);
        ticker_apply(l, false);
    }
}

void ui_ticker_set_active(bool active) {
    if (active == g_ticker_active) {
        return;
    }
    g_ticker_active = active;
    for (int i = 0; i < TICKERS_MAX; i++) {
        if (g_tickers[i] != NULL) {
            ticker_apply(g_tickers[i], active);
        }
    }
}

void ui_ticker_restart(void) {
    for (int i = 0; i < TICKERS_MAX; i++) {
        if (g_tickers[i] != NULL) {
            ticker_apply(g_tickers[i], g_ticker_active);
        }
    }
}

void ui_label_set_text(lv_obj_t *l, const char *raw) {
    const char *shown = raw;
    char small[96];
    char *big = NULL;
    if (!ui_text_is_clean(raw)) {
        size_t need = strlen(raw) + 1; // the filtered text is never longer
        char *buf = small;
        if (need > sizeof small) {
            big = lv_malloc(need);
            buf = big;
        }
        if (buf != NULL) { // out of memory: show the raw text (empty boxes beat a blank row)
            ui_text_display(buf, need, raw);
            shown = buf;
        }
    }
    if (strcmp(lv_label_get_text(l), shown) != 0) {
        lv_label_set_text(l, shown);
    }
    if (big != NULL) {
        lv_free(big);
    }
}

static lv_obj_t *label_with(lv_obj_t *parent, const char *text, uint32_t colour, lv_style_t *font) {
    if (!g_fonts_ready) {
        fonts_init();
    }
    lv_obj_t *l = lv_label_create(parent);
    ui_label_set_text(l, text);
    lv_obj_add_style(l, font, 0);
    lv_obj_add_style(l, ui_style_text(colour), 0);
    return l;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, uint32_t colour) { return label_with(parent, text, colour, &g_font_body); }

lv_obj_t *ui_label_small(lv_obj_t *parent, const char *text, uint32_t colour) { return label_with(parent, text, colour, &g_font_small); }

// Icon glyphs are private-use code points of the icon fonts: they never come from a file name, so they skip the display-text filter.
lv_obj_t *ui_icon(lv_obj_t *parent, const char *glyph, bool large, uint32_t colour) {
    if (!g_fonts_ready) {
        fonts_init();
    }
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text_static(l, glyph);
    lv_obj_add_style(l, &g_font_icon[large ? 1 : 0], 0);
    lv_obj_add_style(l, ui_style_text(colour), 0);
    return l;
}

void ui_label_recolour(lv_obj_t *l, uint32_t from, uint32_t to) {
    if (from == to) {
        return;
    }
    lv_obj_remove_style(l, ui_style_text(from), 0);
    lv_obj_add_style(l, ui_style_text(to), 0);
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
