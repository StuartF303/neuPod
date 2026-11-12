// SPDX-License-Identifier: MPL-2.0
/*
 * The art box: the artwork placeholder (ui_artwork) with a picture on top once there is one (Now Playing, its second page and the library's album
 * header). The picture is the art service's thumbnail (art_service.h), 160 x 160 at most on the hosted builds: it is never copied, the image
 * descriptor points at it, and LVGL draws it scaled DOWN into the box (the box is never larger than the thumbnail, bar the 168 px of Now Playing at
 * 480x360).
 */
#include <string.h>

#include "ui_internal.h"

struct ui_artbox {
    lv_obj_t *box, *img, *note;
    lv_image_dsc_t dsc;
    uint32_t gen;
    bool shown;
    int32_t size;
};

ui_artbox_t *ui_artbox_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t size) {
    ui_artbox_t *a = lv_malloc(sizeof *a);
    if (a == NULL) {
        return NULL;
    }
    memset(a, 0, sizeof *a);
    a->size = size;
    a->box = ui_artwork(parent, x, y, size);
    a->img = lv_image_create(a->box);
    lv_obj_set_size(a->img, size, size);
    lv_image_set_inner_align(a->img, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_center(a->img);
    lv_obj_set_hidden(a->img, true);
    return a;
}

void ui_artbox_show(ui_artbox_t *a, const uint16_t *px, uint16_t w, uint16_t h, uint32_t gen) {
    if (a == NULL) {
        return;
    }
    if (px == NULL || w == 0 || h == 0) { /* no picture: the placeholder (ring and dot) shows */
        if (a->shown) {
            lv_obj_set_hidden(a->img, true);
            a->shown = false;
        }
        return;
    }
    if (a->shown && a->gen == gen) {
        return;
    }
    memset(&a->dsc, 0, sizeof a->dsc);
    a->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    a->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    a->dsc.header.w = w;
    a->dsc.header.h = h;
    a->dsc.header.stride = (uint32_t)w * 2u;
    a->dsc.data_size = (uint32_t)w * h * 2u;
    a->dsc.data = (const uint8_t *)px;
    lv_image_set_src(a->img, &a->dsc);
    lv_obj_set_size(a->img, a->size, a->size);
    lv_obj_invalidate(a->img);
    lv_obj_set_hidden(a->img, false);
    a->gen = gen;
    a->shown = true;
}

void ui_artbox_destroy(ui_artbox_t *a) {
    if (a != NULL) {
        lv_free(a); /* the box is a child of the screen's root: it goes with it */
    }
}
