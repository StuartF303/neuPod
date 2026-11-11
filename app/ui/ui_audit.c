// SPDX-License-Identifier: MPL-2.0
/*
 * Layout audit for hosted builds (NEUPOD_UI_HIRES; not compiled for the RP2350 / ESP32-S3 product build). Walks the active LVGL
 * screen and reports every visible widget that is not completely inside the panel, or that sticks out of its parent so that
 * the parent's edge cuts it. A widget whose whole area is outside a parent's area is clipped away on purpose (the list keeps
 * its off-window pool rows there) and is skipped. Enabled by NEUPOD_UI_AUDIT=1 in the environment; the host tests grep the
 * output for "ui: AUDIT FAIL" (tests/CMakeLists.txt, hires_* and audit_320 tests).
 */
#include <stdio.h>

#include "ui_internal.h"

typedef struct {
    const char *screen;
    int objects;
    int violations;
} audit_t;

/* One pixel of slack: a container's own hairline border pushes its content a pixel over its far edge (the Home pane's left
 * border does that at every size), which is not a layout fault. */
#define SLACK 1

static bool inside(const lv_area_t *a, const lv_area_t *outer) {
    return a->x1 >= outer->x1 - SLACK && a->y1 >= outer->y1 - SLACK && a->x2 <= outer->x2 + SLACK && a->y2 <= outer->y2 + SLACK;
}

static bool overlaps(const lv_area_t *a, const lv_area_t *b) {
    return a->x1 <= b->x2 && b->x1 <= a->x2 && a->y1 <= b->y2 && b->y1 <= a->y2;
}

static void walk(lv_obj_t *o, const lv_area_t *clip, audit_t *au) {
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(o, (int32_t)i);
        if (lv_obj_is_hidden(c)) {
            continue;
        }
        lv_area_t a;
        lv_obj_get_coords(c, &a);
        if (!overlaps(&a, clip)) {
            continue; /* clipped away completely by an ancestor: pool rows and the like */
        }
        au->objects++;
        if (!inside(&a, clip)) {
            au->violations++;
            printf("ui: AUDIT FAIL screen '%s': widget (%d,%d)-(%d,%d) is cut by its container (%d,%d)-(%d,%d)\n", au->screen, (int)a.x1,
                   (int)a.y1, (int)a.x2, (int)a.y2, (int)clip->x1, (int)clip->y1, (int)clip->x2, (int)clip->y2);
            continue;
        }
        walk(c, &a, au);
    }
}

int ui_layout_audit(const char *screen) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_update_layout(scr);
    lv_area_t panel = {0, 0, UI_W - 1, UI_H - 1};
    audit_t au = {screen, 0, 0};
    walk(scr, &panel, &au);
    printf("ui: audit '%s' %dx%d objects=%d violations=%d\n", screen, (int)UI_W, (int)UI_H, au.objects, au.violations);
    return au.violations;
}
