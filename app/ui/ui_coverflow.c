// SPDX-License-Identifier: MPL-2.0
/*
 * Cover Flow (design review 3.3, LB-6): the Albums list as five flat album tiles (96 / 64 / 48 px at 320x240, x1.5 at 480x360; no rotation, no reflection)
 * with the title and "Artist . Year . n songs" under the centre one. The library screen owns the data (which album, which art handle); this file owns the
 * tiles, their 90 ms step and the pictures.
 *
 * Seven tile objects form a ring: the tile of album a is tile a mod 7, so a step moves nothing but geometry (and the one tile that leaves on one side comes
 * back as the tile that enters on the other). Each tile is a placeholder square (shown until its picture arrives) with an image on top that points straight at
 * the art service's thumbnail (art_service.h: pinned while the album is wanted, never copied). A step animates the tiles' size and position only (the invalidated
 * areas are the tile row), a jump of more than one album just moves them.
 *
 * Only the builds with the art cache have it (NEUPOD_ART_HIRES): the frozen Pico 2 W compiles it out.
 */
#ifdef NEUPOD_ART_HIRES

#include <stdio.h>
#include <string.h>

#include "art_service.h"
#include "ui_internal.h"

#define RING 7
#define STEP_MS 90
#define FX 256 /* offsets from the centre are fixed point, 1 = 256 */

typedef struct {
    lv_obj_t *box, *img;
    lv_image_dsc_t dsc;
    int32_t album;   /* the album this tile stands for (-1 = none) */
    uint32_t gen;    /* picture shown (art_view_t.gen), 0 = the placeholder */
    int32_t size;    /* last size set */
} tile_t;

struct ui_flow {
    lv_obj_t *root;
    lv_obj_t *title, *sub, *hint;
    tile_t tile[RING];
    ui_flow_get_fn get;
    void *ctx;
    int32_t centre, count;
    int32_t anim_dir; /* the step in flight (+1 / -1), 0 = none */
    int32_t anim_t;   /* 0..FX */
    bool shown;
    char want[ART_BROWSE_N][24];
};

static struct ui_flow *g_live; /* for ui_flow_settle() */

static int32_t lerp(int32_t a, int32_t b, int32_t frac) { return a + (b - a) * frac / FX; }

/* Size and distance of the tile centre from the screen centre for a distance (fixed point, >= 0) from the centre album. */
static void geometry(int32_t dist, int32_t *size, int32_t *dx) {
    const int32_t sz[4] = {UIM->cf_c, UIM->cf_n, UIM->cf_o, UIM->cf_e};
    const int32_t xs[4] = {0, UIM->cf_dx1, UIM->cf_dx2, UIM->cf_dx3};
    int32_t k = dist / FX, frac = dist % FX;
    if (k >= 3) {
        *size = sz[3];
        *dx = xs[3];
        return;
    }
    *size = lerp(sz[k], sz[k + 1], frac);
    *dx = lerp(xs[k], xs[k + 1], frac);
}

static void place(struct ui_flow *f, tile_t *t, int32_t off_fx) {
    int32_t dist = off_fx < 0 ? -off_fx : off_fx, size, dx;
    bool vis = f->shown && t->album >= 0 && t->album < f->count && dist < 3 * FX;
    if (lv_obj_is_hidden(t->box) == vis) {
        lv_obj_set_hidden(t->box, !vis);
    }
    if (!vis) {
        return;
    }
    geometry(dist, &size, &dx);
    int32_t x = UI_W / 2 + (off_fx < 0 ? -dx : dx) - size / 2;
    int32_t y = UIM->cf_y + (UIM->cf_c - size) / 2;
    if (size != t->size) {
        lv_obj_set_size(t->box, size, size);
        lv_obj_set_size(t->img, size, size);
        t->size = size;
    }
    lv_obj_set_pos(t->box, x, y);
}

static void layout(struct ui_flow *f) {
    int32_t shift = f->anim_dir * (FX - f->anim_t); /* the tiles are still this far from where the step leaves them */
    for (int i = 0; i < RING; i++) {
        tile_t *t = &f->tile[i];
        place(f, t, (t->album - f->centre) * FX + shift);
    }
}

/* Nearer the centre is on top (the neighbours overlap the centre tile by a few pixels, as in the mock-up). */
static void stack(struct ui_flow *f) {
    for (int d = 3; d >= 0; d--) {
        for (int i = 0; i < RING; i++) {
            int32_t o = f->tile[i].album - f->centre;
            if ((o < 0 ? -o : o) == d) {
                lv_obj_move_foreground(f->tile[i].box);
            }
        }
    }
}

static void show_picture(tile_t *t, const art_view_t *v) {
    if (v->state != ART_VIEW_READY || v->px == NULL || v->w == 0 || v->h == 0) {
        if (t->gen != 0) {
            lv_obj_set_hidden(t->img, true);
            t->gen = 0;
        }
        return;
    }
    if (t->gen == v->gen && !lv_obj_is_hidden(t->img)) {
        return;
    }
    memset(&t->dsc, 0, sizeof t->dsc);
    t->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    t->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    t->dsc.header.w = v->w;
    t->dsc.header.h = v->h;
    t->dsc.header.stride = (uint32_t)v->w * 2u;
    t->dsc.data_size = (uint32_t)v->w * v->h * 2u;
    t->dsc.data = (const uint8_t *)v->px;
    lv_image_set_src(t->img, &t->dsc);
    lv_obj_invalidate(t->img);
    lv_obj_set_hidden(t->img, false);
    t->gen = v->gen;
}

/* Which albums the art service should decode, centre first, then the neighbours (the order of the storage task's work); and what each tile shows. */
static void pictures(struct ui_flow *f, bool ask) {
    char h[RING][24];
    bool have[RING];
    for (int i = 0; i < RING; i++) {
        tile_t *t = &f->tile[i];
        have[i] = t->album >= 0 && t->album < f->count && f->get != NULL && f->get(f->ctx, t->album, h[i], sizeof h[i]);
    }
    if (ask) {
        static const int32_t order[ART_BROWSE_N] = {0, -1, 1, -2, 2};
        const char *want[ART_BROWSE_N];
        unsigned n = 0;
        for (unsigned k = 0; k < ART_BROWSE_N; k++) {
            int32_t a = f->centre + order[k];
            if (a >= 0 && a < f->count && have[((a % RING) + RING) % RING]) {
                snprintf(f->want[n], sizeof f->want[n], "%s", h[((a % RING) + RING) % RING]);
                want[n] = f->want[n];
                n++;
            }
        }
        art_browse_want_set(want, n);
    }
    for (int i = 0; i < RING; i++) {
        tile_t *t = &f->tile[i];
        art_view_t v = {0};
        if (have[i]) {
            art_browse_get_handle(h[i], &v);
        }
        show_picture(t, &v);
    }
}

static void assign(struct ui_flow *f) {
    for (int a = f->centre - 3; a <= f->centre + 3; a++) { /* the tile of album a is tile a mod 7 */
        tile_t *t = &f->tile[((a % RING) + RING) % RING];
        if (t->album != a) {
            t->album = a;
            t->gen = 0;
            lv_obj_set_hidden(t->img, true);
        }
    }
}

static void anim_cb(void *var, int32_t v) {
    struct ui_flow *f = var;
    f->anim_t = v;
    layout(f);
}

static void anim_done(lv_anim_t *a) {
    struct ui_flow *f = lv_anim_get_user_data(a);
    f->anim_dir = 0;
    f->anim_t = FX;
    layout(f);
}

void ui_flow_settle(void) {
    if (g_live != NULL && g_live->anim_dir != 0) {
        lv_anim_delete(g_live, anim_cb);
        g_live->anim_dir = 0;
        g_live->anim_t = FX;
        layout(g_live);
    }
}

ui_flow_t *ui_flow_create(lv_obj_t *parent, ui_flow_get_fn get, void *ctx) {
    struct ui_flow *f = lv_malloc(sizeof *f);
    if (f == NULL) {
        return NULL;
    }
    memset(f, 0, sizeof *f);
    f->get = get;
    f->ctx = ctx;
    f->anim_t = FX;
    f->root = lv_obj_create(parent);
    lv_obj_remove_style_all(f->root);
    lv_obj_set_size(f->root, UI_W, UI_CONTENT_H);
    lv_obj_set_pos(f->root, 0, 0);
    lv_obj_set_scrollable(f->root, false);
    ui_obj_bg(f->root, UI_C_BG, 0); /* covers the list under it */
    for (int i = 0; i < RING; i++) {
        tile_t *t = &f->tile[i];
        t->album = -1;
        t->box = lv_obj_create(f->root);
        lv_obj_remove_style_all(t->box);
        lv_obj_set_scrollable(t->box, false);
        lv_obj_set_style_radius(t->box, UIM->art_radius, 0);
        lv_obj_set_style_bg_color(t->box, lv_color_hex(0x5A6B8A), 0);
        lv_obj_set_style_bg_grad_color(t->box, lv_color_hex(0x1E2A44), 0);
        lv_obj_set_style_bg_grad_dir(t->box, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(t->box, LV_OPA_COVER, 0);
        t->img = lv_image_create(t->box);
        lv_image_set_inner_align(t->img, LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_center(t->img);
        lv_obj_set_hidden(t->img, true);
        lv_obj_set_hidden(t->box, true);
    }
    const int32_t w = UI_W - 2 * UIM->pad;
    f->title = ui_label(f->root, "", UI_C_INK);
    lv_obj_set_size(f->title, w, UIM->line_h);
    lv_label_set_long_mode(f->title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(f->title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(f->title, UIM->pad, UIM->cf_title_y);
    ui_label_ticker(f->title, true);
    const int32_t sh = lv_font_get_line_height(UIM->font_small);
    f->sub = ui_label_small(f->root, "", UI_C_INK2);
    lv_obj_set_size(f->sub, w, sh);
    lv_label_set_long_mode(f->sub, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(f->sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(f->sub, UIM->pad, UIM->cf_sub_y);
    ui_label_ticker(f->sub, true);
    f->hint = ui_label_small(f->root, "Scroll: browse \xC2\xB7 Select: open \xC2\xB7 Play: play album", UI_C_INK3);
    lv_obj_set_size(f->hint, w, sh);
    lv_obj_set_style_text_align(f->hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(f->hint, UIM->pad, UIM->cf_hint_y);
    lv_obj_set_hidden(f->root, true);
    g_live = f;
    return f;
}

void ui_flow_destroy(ui_flow_t *f) {
    if (f == NULL) {
        return;
    }
    lv_anim_delete(f, anim_cb);
    art_browse_want_set(NULL, 0); /* the pictures are no longer wanted */
    if (g_live == f) {
        g_live = NULL;
    }
    lv_free(f); /* the objects are children of the screen's root: they go with it */
}

void ui_flow_show(ui_flow_t *f, bool on) {
    if (f == NULL || f->shown == on) {
        return;
    }
    f->shown = on;
    lv_obj_set_hidden(f->root, !on);
    if (on) {
        f->anim_dir = 0;
        f->anim_t = FX;
        assign(f);
        layout(f);
        stack(f);
        pictures(f, true);
    } else {
        lv_anim_delete(f, anim_cb);
        f->anim_dir = 0;
        art_browse_want_set(NULL, 0);
        for (int i = 0; i < RING; i++) {
            f->tile[i].gen = 0;
            lv_obj_set_hidden(f->tile[i].img, true);
        }
    }
}

bool ui_flow_shown(const ui_flow_t *f) { return f != NULL && f->shown; }

void ui_flow_set_text(ui_flow_t *f, const char *title, const char *sub) {
    ui_label_set_text(f->title, title);
    ui_label_set_text(f->sub, sub);
}

void ui_flow_move(ui_flow_t *f, int32_t centre, int32_t count) {
    if (f == NULL) {
        return;
    }
    int32_t delta = centre - f->centre;
    f->count = count;
    if (f->anim_dir != 0) { /* a step in flight finishes at once */
        lv_anim_delete(f, anim_cb);
        f->anim_dir = 0;
        f->anim_t = FX;
    }
    f->centre = centre;
    assign(f);
    if (f->shown && (delta == 1 || delta == -1) && UIM->cf_c > 0) {
        f->anim_dir = delta;
        f->anim_t = 0;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, f);
        lv_anim_set_exec_cb(&a, anim_cb);
        lv_anim_set_values(&a, 0, FX);
        lv_anim_set_duration(&a, STEP_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_completed_cb(&a, anim_done);
        lv_anim_set_user_data(&a, f);
        lv_anim_start(&a);
    }
    layout(f);
    stack(f);
    pictures(f, true);
}

void ui_flow_refresh(ui_flow_t *f) {
    if (f != NULL && f->shown) {
        assign(f);
        pictures(f, true);
    }
}

void ui_flow_tick(ui_flow_t *f) {
    if (f != NULL && f->shown) {
        pictures(f, false); /* a picture that has arrived */
    }
}

#endif /* NEUPOD_ART_HIRES */
