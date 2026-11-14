// SPDX-License-Identifier: MPL-2.0
/*
 * Boot splash (design review 3.9, SP-1). The release splash is the wordmark ("neu" white, "Pod" in the accent colour, plain Montserrat text, no third-party
 * artwork), a slow shimmer along a faint line under it, one Lucide icon per boot check (Display, Storage, Battery gauge, Haptics, Click wheel, Audio, Bluetooth,
 * Wi-Fi) and the version in the dim ink. The icons pulse while their check runs, go solid with an accent border when it passes and get a red cross when the
 * subsystem is not fully correct. The icon row was replaced by text in the first SP-1 cut and reinstated at the owner's request on 2026-11-13.
 * When a boot check failed, or Menu is held while the unit starts, the debug splash adds a short text list under the icons ("Battery gauge - no response",
 * one failed check a line, then how many checks passed) and the hold bar along the bottom edge drains over the hold. The splash is not themed: literal
 * colours on a deep blue-black ground.
 */
#include <stdio.h>

#include "hal_system.h"
#include "neupod_version.h"
#include "ui_internal.h"

#define LIST_MAX 8
#define OPA_PULSE_LO 60
#define OPA_PULSE_HI 150

typedef struct {
    lv_obj_t *box;
    lv_obj_t *cross[4]; // two dark under-strokes and two red strokes, hidden unless degraded
    cap_state_t shown;
    bool built;
} icon_t;

static icon_t g_icons[CAP_COUNT];
static lv_obj_t *g_scr;
static lv_obj_t *g_ver;
static lv_obj_t *g_hold_bar; // thin bar along the bottom, drains over the hold
static lv_obj_t *g_list[LIST_MAX];
static bool g_debug_shown;

// What each check is called on the debug list (capabilities.h order).
static const char *const k_check_name[CAP_COUNT] = {"Display", "Storage", "Battery gauge", "Haptics", "Click wheel", "Audio", "Bluetooth", "Wi-Fi"};

// The icon of each check (capabilities.h order): Lucide glyphs, ui_icons.h.
static const char *const k_check_icon[CAP_COUNT] = {UI_ICON_MONITOR, UI_ICON_HARD_DRIVE, UI_ICON_BATTERY, UI_ICON_VIBRATE,
                                                    UI_ICON_CIRCLE_DOT, UI_ICON_VOLUME, UI_ICON_BLUETOOTH, UI_ICON_WIFI};

// Set the opacity of an icon's own drawing (box and glyph), leaving the cross strokes solid.
static bool is_cross(const lv_obj_t *obj) {
    for (int i = 0; i < CAP_COUNT; i++) {
        for (int k = 0; k < 4; k++) {
            if (g_icons[i].cross[k] == obj) {
                return true;
            }
        }
    }
    return false;
}

static void set_opa_all(lv_obj_t *box, lv_opa_t opa) {
    lv_obj_set_style_opa(box, opa, 0);
    uint32_t n = lv_obj_get_child_count(box);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(box, (int32_t)i);
        if (!is_cross(c)) {
            lv_obj_set_style_opa(c, opa, 0);
        }
    }
}

static void pulse_cb(void *box, int32_t v) { set_opa_all((lv_obj_t *)box, (lv_opa_t)v); }

static lv_obj_t *stroke(lv_obj_t *box, const lv_point_precise_t *pts, int32_t width, uint32_t colour) {
    lv_obj_t *ln = lv_line_create(box);
    lv_line_set_points(ln, pts, 2);
    lv_obj_set_pos(ln, 0, 0);
    lv_obj_set_style_line_width(ln, width, 0);
    lv_obj_set_style_line_color(ln, lv_color_hex(colour), 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    lv_obj_set_hidden(ln, true);
    return ln;
}

static void icon_create(lv_obj_t *row, cap_id_t id) {
    icon_t *ic = &g_icons[id];
    lv_obj_t *box = lv_obj_create(row);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, UIM->sp_icon, UIM->sp_icon);
    lv_obj_set_style_radius(box, UIM->sp_icon_radius, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_BAR), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_DIM), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_scrollable(box, false);
    ic->box = box;

    lv_obj_t *g = lv_label_create(box);
    lv_label_set_text_static(g, k_check_icon[id]);
    lv_obj_set_style_text_font(g, UIM->font_icon_lg, 0);
    lv_obj_set_style_text_color(g, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_center(g);
    // The cross: a dark outline under a red stroke, so it reads on any icon.
    ic->cross[0] = stroke(box, UIM->sp_diag_a, UIM->sp_cross_under_w, UI_COL_BG);
    ic->cross[1] = stroke(box, UIM->sp_diag_b, UIM->sp_cross_under_w, UI_COL_BG);
    ic->cross[2] = stroke(box, UIM->sp_diag_a, UIM->sp_cross_w, UI_COL_ERR);
    ic->cross[3] = stroke(box, UIM->sp_diag_b, UIM->sp_cross_w, UI_COL_ERR);

    ic->shown = CAP_PENDING;
    ic->built = true;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, box);
    lv_anim_set_exec_cb(&a, pulse_cb);
    lv_anim_set_values(&a, OPA_PULSE_LO, OPA_PULSE_HI);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_reverse_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void fade_in_cb(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }

static void bar_x_cb(void *obj, int32_t v) { lv_obj_set_x((lv_obj_t *)obj, v); }

void ui_splash_create(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    g_scr = scr;
    g_debug_shown = false;
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(UI_COL_BG2), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Wordmark: "neu" in white, "Pod" in the accent colour.
    lv_obj_t *word = lv_obj_create(scr);
    lv_obj_remove_style_all(word);
    lv_obj_set_size(word, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(word, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(word, 0, 0);
    lv_obj_align(word, LV_ALIGN_TOP_MID, 0, UIM->sp_wordmark_y);
    static const char *const parts[2] = {"neu", "Pod"};
    static const uint32_t cols[2] = {UI_COL_TEXT, UI_COL_ACCENT};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = lv_label_create(word);
        lv_label_set_text(l, parts[i]);
        lv_obj_set_style_text_font(l, UIM->font_huge, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(cols[i]), 0);
        lv_obj_set_style_opa(l, LV_OPA_TRANSP, 0);
        lv_anim_t fa;
        lv_anim_init(&fa);
        lv_anim_set_var(&fa, l);
        lv_anim_set_exec_cb(&fa, fade_in_cb);
        lv_anim_set_values(&fa, 0, 255);
        lv_anim_set_duration(&fa, 500);
        lv_anim_set_delay(&fa, (uint32_t)i * 150u);
        lv_anim_start(&fa);
    }

    // The progress line: a short bright bar sliding back and forth along a faint track under the wordmark.
    lv_obj_t *track = lv_obj_create(scr);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, UIM->sp_track_w, UIM->sp_track_h);
    lv_obj_set_style_bg_color(track, lv_color_hex(UI_COL_BAR), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_align(track, LV_ALIGN_TOP_MID, 0, UIM->sp_track_y);
    lv_obj_t *bar = lv_obj_create(track);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, UIM->sp_shimmer_w, UIM->sp_track_h);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_anim_t sa;
    lv_anim_init(&sa);
    lv_anim_set_var(&sa, bar);
    lv_anim_set_exec_cb(&sa, bar_x_cb);
    lv_anim_set_values(&sa, 0, UIM->sp_track_w - UIM->sp_shimmer_w);
    lv_anim_set_duration(&sa, 1300);
    lv_anim_set_reverse_duration(&sa, 1300);
    lv_anim_set_repeat_count(&sa, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&sa, lv_anim_path_ease_in_out);
    lv_anim_start(&sa);

    // Check icon row: one icon per boot check.
    const int32_t row_w = CAP_COUNT * UIM->sp_icon + (CAP_COUNT - 1) * UIM->sp_icon_gap;
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, row_w, UIM->sp_icon);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, UIM->sp_icon_row_y);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, UIM->sp_icon_gap, 0);
    for (int i = 0; i < CAP_COUNT; i++) {
        icon_create(row, (cap_id_t)i);
    }

    g_hold_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(g_hold_bar);
    lv_obj_set_size(g_hold_bar, UI_W, UIM->sp_hold_h);
    lv_obj_set_style_bg_color(g_hold_bar, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(g_hold_bar, LV_OPA_COVER, 0);
    lv_obj_align(g_hold_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_hidden(g_hold_bar, true);

    g_ver = lv_label_create(scr);
    lv_label_set_text_fmt(g_ver, "v%s  %s  %s", NEUPOD_VERSION, NEUPOD_GIT_HASH, hal_platform_name());
    lv_obj_set_style_text_font(g_ver, UIM->font_small, 0);
    lv_obj_set_style_text_color(g_ver, lv_color_hex(UI_COL_DIM), 0);
    lv_obj_align(g_ver, LV_ALIGN_BOTTOM_MID, 0, -UIM->sp_ver_inset);
    for (int i = 0; i < LIST_MAX; i++) {
        g_list[i] = NULL;
    }

    lv_screen_load(scr);
}

void ui_splash_update(const capabilities_t *caps) {
    for (int i = 0; i < CAP_COUNT; i++) {
        icon_t *ic = &g_icons[i];
        if (!ic->built || caps->state[i] == ic->shown) {
            continue;
        }
        ic->shown = caps->state[i];
        if (ic->shown == CAP_PENDING) {
            continue;
        }
        lv_anim_delete(ic->box, pulse_cb); // stop pulsing; the final look is set below
        set_opa_all(ic->box, LV_OPA_COVER);
        if (ic->shown == CAP_OK) {
            lv_obj_set_style_border_color(ic->box, lv_color_hex(UI_COL_ACCENT), 0);
        } else {
            for (int k = 0; k < 4; k++) {
                lv_obj_set_hidden(ic->cross[k], false);
            }
        }
    }
}

// One line of the debug list, centred, one line high.
static lv_obj_t *list_line(int row, int32_t y, const char *text, uint32_t colour) {
    lv_obj_t *l = lv_label_create(g_scr);
    lv_obj_set_size(l, UI_W - 2 * UIM->pad, lv_font_get_line_height(UIM->font_body));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, UIM->font_body, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, UIM->pad, y);
    g_list[row] = l;
    return l;
}

void ui_splash_show_debug(const capabilities_t *caps) {
    if (g_debug_shown || g_scr == NULL) {
        return;
    }
    g_debug_shown = true;
    const int32_t pitch = lv_font_get_line_height(UIM->font_body) + UIM->sp_caption_gap / 2;
    const int32_t top = UIM->sp_icon_row_y + UIM->sp_icon + UIM->sp_caption_gap;
    const int32_t room = (UI_H - UIM->sp_ver_inset - lv_font_get_line_height(UIM->font_small) - top) / pitch; /* lines above the version */
    unsigned bad = 0, ok = 0;
    for (int i = 0; i < CAP_COUNT; i++) {
        bad += caps->state[i] == CAP_DEGRADED;
        ok += caps->state[i] == CAP_OK;
    }
    int row = 0;
    char b[96];
    for (int i = 0; i < CAP_COUNT && row < LIST_MAX; i++) {
        if (caps->state[i] != CAP_DEGRADED) {
            continue;
        }
        if (row >= room - 1 && bad > (unsigned)row + 1u) { // not room for all of them: say how many more
            snprintf(b, sizeof b, "+%u more", bad - (unsigned)row);
            list_line(row, top + row * pitch, b, UI_COL_ERR);
            row++;
            break;
        }
        snprintf(b, sizeof b, "%s \xE2\x80\x94 %s", k_check_name[i], caps->brief[i][0] != '\0' ? caps->brief[i] : "failed");
        list_line(row, top + row * pitch, b, UI_COL_ERR);
        row++;
    }
    if (bad == 0) {
        snprintf(b, sizeof b, "All %u checks passed", (unsigned)CAP_COUNT);
    } else {
        snprintf(b, sizeof b, "%u of %u checks passed", ok, (unsigned)CAP_COUNT);
    }
    list_line(row, top + row * pitch + (bad != 0 ? UIM->sp_caption_gap / 2 : 0), b, UI_COL_DIM);
}

void ui_splash_hold_update(uint32_t elapsed_ms, uint32_t total_ms) {
    if (total_ms == 0 || elapsed_ms > total_ms) {
        elapsed_ms = total_ms;
    }
    lv_obj_set_hidden(g_hold_bar, false);
    int32_t w = total_ms == 0 ? 0 : (int32_t)((uint64_t)UI_W * (total_ms - elapsed_ms) / total_ms);
    if (w != lv_obj_get_width(g_hold_bar)) {
        lv_obj_set_width(g_hold_bar, w);
    }
}
