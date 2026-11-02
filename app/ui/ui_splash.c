// SPDX-License-Identifier: MPL-2.0
/*
 * Boot splash: an original "neuPod" wordmark (plain Montserrat text, no third-party artwork), a
 * slow shimmer under it, and one icon per subsystem. Icons pulse while their check runs, go
 * solid when it passes, and get a red cross only when the subsystem is not fully correct.
 */
#include <stdio.h>

#include "hal_system.h"
#include "neupod_version.h"
#include "ui_internal.h"

#define ICON_SIZE 32
#define ICON_GAP 6
#define ICON_ROW_Y 142
#define OPA_PULSE_LO 60
#define OPA_PULSE_HI 150

typedef struct {
    lv_obj_t *box;
    lv_obj_t *cross[4]; // two dark under-strokes and two red strokes, hidden unless degraded
    cap_state_t shown;
    bool built;
} icon_t;

static icon_t g_icons[CAP_COUNT];
static lv_obj_t *g_caption;
static lv_obj_t *g_hold_bar; // thin bar along the bottom, drains over the hold
static uint32_t g_shown_reason = 0xFFFFFFFFu;
static bool g_caption_done;

// --- helpers ---------------------------------------------------------------------------------

// Set the opacity of an icon's own drawing (box and glyph parts), leaving the cross strokes solid.
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

static void fade_in_cb(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }

static void bar_x_cb(void *obj, int32_t v) { lv_obj_set_x((lv_obj_t *)obj, v); }

static lv_obj_t *glyph(lv_obj_t *parent, const char *text, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_center(l);
    return l;
}

static lv_obj_t *shape(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius, bool filled) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    if (filled) {
        lv_obj_set_style_bg_color(o, lv_color_hex(UI_COL_TEXT), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_border_color(o, lv_color_hex(UI_COL_TEXT), 0);
        lv_obj_set_style_border_width(o, 2, 0);
    }
    return o;
}

// Icons with no LVGL symbol: the click wheel (a ring with a centre button) and haptics (a body
// with vibration strokes either side).
static void draw_wheel_icon(lv_obj_t *box) {
    lv_obj_t *ring = shape(box, 22, 22, LV_RADIUS_CIRCLE, false);
    lv_obj_center(ring);
    lv_obj_t *dot = shape(box, 7, 7, LV_RADIUS_CIRCLE, true);
    lv_obj_center(dot);
}

static void draw_haptic_icon(lv_obj_t *box) {
    lv_obj_t *body = shape(box, 9, 16, 3, false);
    lv_obj_center(body);
    for (int side = -1; side <= 1; side += 2) {
        lv_obj_t *near_bar = shape(box, 2, 10, 1, true);
        lv_obj_align(near_bar, LV_ALIGN_CENTER, side * 9, 0);
        lv_obj_t *far_bar = shape(box, 2, 6, 1, true);
        lv_obj_align(far_bar, LV_ALIGN_CENTER, side * 13, 0);
    }
}

static const lv_point_precise_t k_diag_a[2] = {{7, 7}, {25, 25}};
static const lv_point_precise_t k_diag_b[2] = {{25, 7}, {7, 25}};

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
    lv_obj_set_size(box, ICON_SIZE, ICON_SIZE);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_BAR), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_DIM), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_scrollable(box, false);
    ic->box = box;

    switch (id) {
    case CAP_DISPLAY:
        glyph(box, LV_SYMBOL_IMAGE, &lv_font_montserrat_20);
        break;
    case CAP_STORAGE:
        glyph(box, LV_SYMBOL_SD_CARD, &lv_font_montserrat_20);
        break;
    case CAP_BATTERY:
        glyph(box, LV_SYMBOL_BATTERY_3, &lv_font_montserrat_20);
        break;
    case CAP_HAPTICS:
        draw_haptic_icon(box);
        break;
    case CAP_WHEEL:
        draw_wheel_icon(box);
        break;
    case CAP_AUDIO:
        glyph(box, LV_SYMBOL_AUDIO, &lv_font_montserrat_20);
        break;
    case CAP_BLUETOOTH:
        glyph(box, LV_SYMBOL_BLUETOOTH, &lv_font_montserrat_20);
        break;
    case CAP_WIFI:
        glyph(box, LV_SYMBOL_WIFI, &lv_font_montserrat_20);
        break;
    default:
        break;
    }
    // The cross: a dark outline under a red stroke, so it reads on any icon.
    ic->cross[0] = stroke(box, k_diag_a, 7, UI_COL_BG);
    ic->cross[1] = stroke(box, k_diag_b, 7, UI_COL_BG);
    ic->cross[2] = stroke(box, k_diag_a, 3, UI_COL_ERR);
    ic->cross[3] = stroke(box, k_diag_b, 3, UI_COL_ERR);

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

// --- screen ----------------------------------------------------------------------------------

void ui_splash_create(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
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
    lv_obj_align(word, LV_ALIGN_TOP_MID, 0, 30);
    static const char *const parts[2] = {"neu", "Pod"};
    static const uint32_t cols[2] = {UI_COL_TEXT, UI_COL_ACCENT};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = lv_label_create(word);
        lv_label_set_text(l, parts[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
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

    // Shimmer: a short bright bar sliding back and forth along a faint track under the wordmark.
    lv_obj_t *track = lv_obj_create(scr);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, 150, 2);
    lv_obj_set_style_bg_color(track, lv_color_hex(UI_COL_BAR), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_align(track, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_t *bar = lv_obj_create(track);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 44, 2);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_anim_t sa;
    lv_anim_init(&sa);
    lv_anim_set_var(&sa, bar);
    lv_anim_set_exec_cb(&sa, bar_x_cb);
    lv_anim_set_values(&sa, 0, 106);
    lv_anim_set_duration(&sa, 1300);
    lv_anim_set_reverse_duration(&sa, 1300);
    lv_anim_set_repeat_count(&sa, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&sa, lv_anim_path_ease_in_out);
    lv_anim_start(&sa);

    // Subsystem icon row.
    int32_t row_w = CAP_COUNT * ICON_SIZE + (CAP_COUNT - 1) * ICON_GAP;
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, row_w, ICON_SIZE);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, ICON_ROW_Y);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, ICON_GAP, 0);
    for (int i = 0; i < CAP_COUNT; i++) {
        icon_create(row, (cap_id_t)i);
    }

    g_caption = lv_label_create(scr);
    // One line, fixed size, never wraps (long text would end in "..."): the vertical layout is
    // icons 142..174, caption 182..200, version line from ~215, hold bar 238..240.
    lv_obj_set_size(g_caption, UI_W - 20, 18);
    lv_obj_set_style_text_align(g_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_caption, LV_LABEL_LONG_DOT);
    lv_label_set_text(g_caption, "Checking hardware...");
    lv_obj_set_style_text_font(g_caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_caption, lv_color_hex(UI_COL_DIM), 0);
    lv_obj_align(g_caption, LV_ALIGN_TOP_MID, 0, ICON_ROW_Y + ICON_SIZE + 8);
    g_caption_done = false;
    g_shown_reason = 0xFFFFFFFFu;

    g_hold_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(g_hold_bar);
    lv_obj_set_size(g_hold_bar, UI_W, 2);
    lv_obj_set_style_bg_color(g_hold_bar, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(g_hold_bar, LV_OPA_COVER, 0);
    lv_obj_align(g_hold_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_hidden(g_hold_bar, true);

    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text_fmt(ver, "v%s  %s  %s", NEUPOD_VERSION, NEUPOD_GIT_HASH, hal_platform_name());
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(UI_COL_DIM), 0);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_screen_load(scr);
}

void ui_splash_update(const capabilities_t *caps) {
    bool any_pending = false;
    for (int i = 0; i < CAP_COUNT; i++) {
        icon_t *ic = &g_icons[i];
        if (!ic->built) {
            continue;
        }
        cap_state_t st = caps->state[i];
        if (st == CAP_PENDING) {
            any_pending = true;
        }
        if (st == ic->shown) {
            continue;
        }
        ic->shown = st;
        lv_anim_delete(ic->box, pulse_cb); // stop pulsing; the final look is set below
        set_opa_all(ic->box, LV_OPA_COVER);
        if (st == CAP_OK) {
            lv_obj_set_style_border_color(ic->box, lv_color_hex(UI_COL_ACCENT), 0);
        } else {
            for (int k = 0; k < 4; k++) {
                lv_obj_set_hidden(ic->cross[k], false);
            }
        }
    }
    if (!any_pending && !g_caption_done) {
        g_caption_done = true;
        lv_label_set_text(g_caption, "");
    }
}

void ui_splash_hold_update(const capabilities_t *caps, uint32_t elapsed_ms, uint32_t total_ms) {
    if (total_ms == 0 || elapsed_ms > total_ms) {
        elapsed_ms = total_ms;
    }
    lv_obj_set_hidden(g_hold_bar, false);
    int32_t w = total_ms == 0 ? 0 : (int32_t)((uint64_t)UI_W * (total_ms - elapsed_ms) / total_ms);
    if (w != lv_obj_get_width(g_hold_bar)) {
        lv_obj_set_width(g_hold_bar, w);
    }
    // One failure reason at a time ("[2/5] storage: card init: NO_CARD"), 1.6 s each: all of them
    // would not fit under the icon row at this size.
    unsigned bad[CAP_COUNT], n = 0;
    for (unsigned i = 0; i < CAP_COUNT; i++) {
        if (caps->state[i] == CAP_DEGRADED) {
            bad[n++] = i;
        }
    }
    if (n == 0) {
        return;
    }
    uint32_t slot = (elapsed_ms / 1600u) % n;
    if (slot != g_shown_reason) {
        g_shown_reason = slot;
        lv_obj_set_style_text_color(g_caption, lv_color_hex(UI_COL_ERR), 0);
        lv_label_set_text_fmt(g_caption, "[%u/%u] %s: %s", (unsigned)slot + 1u, n,
                              cap_name((cap_id_t)bad[slot]), caps->brief[bad[slot]]);
    }
}
