// SPDX-License-Identifier: MPL-2.0
/*
 * Layout audit for hosted builds (NEUPOD_UI_HIRES; not compiled for the RP2350 / ESP32-S3 product build). Walks the active LVGL
 * screen and reports every visible widget that
 *   - is not completely inside the panel,
 *   - is not completely inside its parent (the parent's edge would cut it; one pixel over the parent's far edge is tolerated,
 *     which is what a container's own hairline border does to its content, e.g. the Home pane's left border),
 *   - is a label whose text does not fit its box: a one-line label wider than its box, or a wrapping label that needs more
 *     lines than its box is tall. Labels in "dots" or scrolling long modes shorten or scroll their text on purpose and are
 *     not measured.
 * The one exemption is the list's pool rows (flagged UI_OBJ_FLAG_POOL_ROW in ui_list.c): rows outside the window are kept
 * as clipped-away objects, so a pool row not completely inside its parent is skipped (with its children). Any other widget
 * that lies wholly outside its parent or the panel is a violation. The walk continues after a violation, so all are listed.
 * Enabled by NEUPOD_UI_AUDIT=1 in the environment; every call prints
 *   "ui: audit '<screen>' WxH objects=N violations=V"
 * which the host tests require (PASS_REGULAR_EXPRESSION) together with the absence of "ui: AUDIT FAIL" (tests/CMakeLists.txt).
 */
#include <stdio.h>
#include <string.h>

#include "src/misc/lv_text_private.h" /* lv_text_get_size_attributes: LVGL private API, fine for a host-only check */
#include "src/widgets/label/lv_label_private.h"
#include "ui_internal.h"

typedef struct {
    const char *screen;
    int objects;
    int violations;
} audit_t;

#define FAR_EDGE_SLACK 1

static bool inside(const lv_area_t *a, const lv_area_t *outer) {
    return a->x1 >= outer->x1 && a->y1 >= outer->y1 && a->x2 <= outer->x2 + FAR_EDGE_SLACK && a->y2 <= outer->y2 + FAR_EDGE_SLACK;
}

static void fail(audit_t *au, const lv_area_t *a, const char *what) {
    au->violations++;
    printf("ui: AUDIT FAIL screen '%s': widget (%d,%d)-(%d,%d) %s\n", au->screen, (int)a->x1, (int)a->y1, (int)a->x2, (int)a->y2, what);
}

static void check_label(lv_obj_t *o, const lv_area_t *a, audit_t *au) {
    lv_label_t *lb = (lv_label_t *)o;
    if (lb->long_mode == LV_LABEL_LONG_DOT || lb->long_mode == LV_LABEL_LONG_SCROLL_CIRCULAR) {
        /* Dots only work on a box of one line: LVGL wraps a dotted label whose height follows its content onto a second line (the Home track title
         * once ran into the artist line). So a dotted label as tall as two lines or more, holding text wider than it, is a violation. */
        const char *t = lv_label_get_text(o);
        const lv_font_t *f = lv_obj_get_style_text_font(o, LV_PART_MAIN);
        int32_t bw = lv_area_get_width(a) - lv_obj_get_style_pad_left(o, LV_PART_MAIN) - lv_obj_get_style_pad_right(o, LV_PART_MAIN);
        if (t != NULL && t[0] != '\0' && lv_area_get_height(a) >= 2 * lv_font_get_line_height(f)) {
            lv_text_attributes_t at = {.letter_space = lv_obj_get_style_text_letter_space(o, LV_PART_MAIN), .line_space = 0, .max_width = LV_COORD_MAX, .text_flags = LV_TEXT_FLAG_NONE};
            lv_point_t one;
            lv_text_get_size_attributes(&one, t, f, &at);
            if (one.x > bw) {
                char what[120];
                snprintf(what, sizeof what, "dotted label '%.40s' is %d px tall (must be one line: dots or ticker)", t, (int)lv_area_get_height(a));
                fail(au, a, what);
            }
        }
        return;
    }
    if (lb->long_mode == LV_LABEL_LONG_SCROLL || lb->long_mode == LV_LABEL_LONG_SCROLL_CIRCULAR) {
        return;
    }
    const char *text = lv_label_get_text(o);
    if (text == NULL || text[0] == '\0') {
        return;
    }
    int32_t box_w = lv_area_get_width(a) - lv_obj_get_style_pad_left(o, LV_PART_MAIN) - lv_obj_get_style_pad_right(o, LV_PART_MAIN);
    int32_t box_h = lv_area_get_height(a) - lv_obj_get_style_pad_top(o, LV_PART_MAIN) - lv_obj_get_style_pad_bottom(o, LV_PART_MAIN);
    bool wrap = lb->long_mode == LV_LABEL_LONG_WRAP;
    lv_text_attributes_t attr = {
        .letter_space = lv_obj_get_style_text_letter_space(o, LV_PART_MAIN),
        .line_space = lv_obj_get_style_text_line_space(o, LV_PART_MAIN),
        .max_width = wrap ? box_w : LV_COORD_MAX,
        .text_flags = LV_TEXT_FLAG_NONE,
    };
    lv_point_t sz;
    lv_text_get_size_attributes(&sz, text, lv_obj_get_style_text_font(o, LV_PART_MAIN), &attr);
    if (wrap ? sz.y > box_h : sz.x > box_w) {
        char what[160];
        snprintf(what, sizeof what, "label text '%.40s' needs %dx%d but the box is %dx%d (clipped)", text, (int)sz.x, (int)sz.y, (int)box_w, (int)box_h);
        fail(au, a, what);
    }
}

static void walk(lv_obj_t *o, const lv_area_t *parent_area, const lv_area_t *panel, audit_t *au) {
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(o, (int32_t)i);
        if (lv_obj_is_hidden(c)) {
            continue;
        }
        lv_area_t a;
        lv_obj_get_coords(c, &a);
        bool pool_row = lv_obj_has_flag(c, UI_OBJ_FLAG_POOL_ROW);
        if (pool_row && !(inside(&a, parent_area) && inside(&a, panel))) {
            continue; /* a row outside the list window: clipped away by design */
        }
        au->objects++;
        if (!inside(&a, panel)) {
            fail(au, &a, "is not inside the panel");
        } else if (!inside(&a, parent_area)) {
            fail(au, &a, "is cut by its container");
        }
        if (lv_obj_check_type(c, &lv_label_class)) {
            check_label(c, &a, au);
        }
        walk(c, &a, panel, au);
    }
}

int ui_layout_audit(const char *screen) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_update_layout(scr);
    lv_area_t panel = {0, 0, UI_W - 1, UI_H - 1};
    audit_t au = {screen, 0, 0};
    walk(scr, &panel, &panel, &au);
    printf("ui: audit '%s' %dx%d objects=%d violations=%d\n", screen, (int)UI_W, (int)UI_H, au.objects, au.violations);
    return au.violations;
}
