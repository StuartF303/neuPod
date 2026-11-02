// SPDX-License-Identifier: MPL-2.0
/*
 * Home menu, Music, Extras, Now Playing and the placeholder screens (docs/ui.md mock-ups: classic split
 * menu, blue selection bar, lightly modernised; original drawings, no third-party artwork).
 */
#include <stdio.h>
#include <string.h>

#include "hal_storage.h"
#include "hal_system.h"
#include "input_events.h"
#include "os.h"
#include "ui_internal.h"

/* ---- generic menu screen: a list of entries, Select pushes the entry's screen ----------------------- */

typedef struct {
    const char *label;
    ui_screen_t *(*create)(void);
} menu_entry_t;

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    const menu_entry_t *entries;
    int32_t n;
    lv_obj_t *uptime, *storage; /* home right pane */
} menu_screen_t;

static void menu_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    const menu_screen_t *m = ctx;
    snprintf(out->label, sizeof out->label, "%s", m->entries[idx].label);
    out->chevron = true;
}

static void menu_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((menu_screen_t *)s)->list, steps); }

static bool menu_button(ui_screen_t *s, uint8_t btn) {
    menu_screen_t *m = (menu_screen_t *)s;
    if (btn == HAL_BTN_SELECT) {
        int32_t i = ui_list_selected(m->list);
        if (i >= 0 && i < m->n && m->entries[i].create != NULL) {
            ui_frame_push(m->entries[i].create());
        }
        return true;
    }
    return false;
}

static void menu_destroy(ui_screen_t *s) { ui_list_destroy(((menu_screen_t *)s)->list); }

static menu_screen_t *menu_new(const char *title, const menu_entry_t *entries, int32_t n, int32_t list_w) {
    menu_screen_t *m = (menu_screen_t *)ui_screen_new(sizeof *m, title);
    if (m == NULL) {
        return NULL;
    }
    m->entries = entries;
    m->n = n;
    m->base.on_scroll = menu_scroll;
    m->base.on_button = menu_button;
    m->base.destroy = menu_destroy;
    m->list = ui_list_create(m->base.root, list_w, &(ui_list_model_t){.count = n, .ctx = m, .get = menu_get});
    return m;
}

/* ---- shared drawing bits ---------------------------------------------------------------------------- */

static lv_obj_t *pane_new(lv_obj_t *parent, int32_t x, int32_t w) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, w, UI_CONTENT_H);
    lv_obj_set_pos(p, x, 0);
    lv_obj_set_scrollable(p, false);
    lv_obj_set_style_bg_color(p, lv_color_hex(UI_L_PANE), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(0xD9D9D4), 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_side(p, LV_BORDER_SIDE_LEFT, 0);
    return p;
}

/* Artwork placeholder: a dark gradient square with a faint ring, like the mock-ups. */
lv_obj_t *ui_artwork(lv_obj_t *parent, int32_t x, int32_t y, int32_t size) {
    lv_obj_t *a = lv_obj_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, size, size);
    lv_obj_set_pos(a, x, y);
    lv_obj_set_style_radius(a, 3, 0);
    lv_obj_set_style_bg_color(a, lv_color_hex(0x5A6B8A), 0);
    lv_obj_set_style_bg_grad_color(a, lv_color_hex(0x1E2A44), 0);
    lv_obj_set_style_bg_grad_dir(a, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(a, LV_OPA_COVER, 0);
    lv_obj_t *ring = lv_obj_create(a);
    lv_obj_remove_style_all(ring);
    int32_t rd = size * 44 / 100;
    lv_obj_set_size(ring, rd, rd);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_40, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_center(ring);
    lv_obj_t *dot = lv_obj_create(a);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, size / 10, size / 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_50, 0);
    lv_obj_center(dot);
    return a;
}

/* ---- placeholder screens -------------------------------------------------------------------------- */

typedef struct {
    ui_screen_t base;
} placeholder_t;

ui_screen_t *ui_screen_placeholder_create(const char *title, const char *line1, const char *line2) {
    placeholder_t *p = (placeholder_t *)ui_screen_new(sizeof *p, title);
    if (p == NULL) {
        return NULL;
    }
    lv_obj_t *a = ui_label(p->base.root, line1, UI_L_TEXT);
    lv_obj_set_style_text_font(a, &lv_font_montserrat_20, 0);
    lv_obj_align(a, LV_ALIGN_CENTER, 0, -14);
    lv_obj_t *b = ui_label(p->base.root, line2, UI_L_DIM);
    lv_obj_set_width(b, UI_W - 40);
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, 18);
    return &p->base;
}

static ui_screen_t *make_podcasts(void) {
    return ui_screen_placeholder_create("Podcasts", "No podcasts", "Podcasts need the library (not built yet).");
}
static ui_screen_t *make_photos(void) {
    return ui_screen_placeholder_create("Photos", "No photos", "The photo browser is not built yet.");
}
static ui_screen_t *make_shuffle(void) {
    return ui_screen_placeholder_create("Shuffle Songs", "Nothing to shuffle", "Add music to the card; the library is not built yet.");
}

/* ---- Music: sub-lists that are empty until there is a library ------------------------------------ */

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
} empty_list_t;

static void empty_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    (void)ctx;
    (void)idx;
    snprintf(out->label, sizeof out->label, "Empty - no library yet");
    out->dim = true;
}
static void empty_destroy(ui_screen_t *s) { ui_list_destroy(((empty_list_t *)s)->list); }
static void empty_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((empty_list_t *)s)->list, steps); }

static ui_screen_t *empty_list(const char *title) {
    empty_list_t *e = (empty_list_t *)ui_screen_new(sizeof *e, title);
    if (e == NULL) {
        return NULL;
    }
    e->base.destroy = empty_destroy;
    e->base.on_scroll = empty_scroll;
    e->list = ui_list_create(e->base.root, UI_W, &(ui_list_model_t){.count = 1, .get = empty_get});
    return &e->base;
}
static ui_screen_t *make_artists(void) { return empty_list("Artists"); }
static ui_screen_t *make_albums(void) { return empty_list("Albums"); }
static ui_screen_t *make_songs(void) { return empty_list("Songs"); }
static ui_screen_t *make_genres(void) { return empty_list("Genres"); }
static ui_screen_t *make_playlists(void) { return empty_list("Playlists"); }

static const menu_entry_t k_music[] = {{"Artists", make_artists},     {"Albums", make_albums}, {"Songs", make_songs},
                                       {"Genres", make_genres},       {"Playlists", make_playlists}};

ui_screen_t *ui_screen_music_create(void) {
    menu_screen_t *m = menu_new("Music", k_music, (int32_t)(sizeof k_music / sizeof k_music[0]), UI_W);
    return m != NULL ? &m->base : NULL;
}

/* ---- Extras ---------------------------------------------------------------------------------------- */

static const menu_entry_t k_extras[] = {{"Files", ui_screen_files_create}, {"Test tone", ui_screen_testtone_create}};

ui_screen_t *ui_screen_extras_create(void) {
    menu_screen_t *m = menu_new("Extras", k_extras, (int32_t)(sizeof k_extras / sizeof k_extras[0]), UI_W);
    return m != NULL ? &m->base : NULL;
}

/* ---- HOME ------------------------------------------------------------------------------------------ */

static ui_screen_t *make_nowplaying(void) { return ui_screen_nowplaying_create(); }
static ui_screen_t *make_settings(void) { return ui_screen_settings_create(); }

static const menu_entry_t k_home[] = {{"Music", ui_screen_music_create},
                                      {"Podcasts", make_podcasts},
                                      {"Photos", make_photos},
                                      {"Extras", ui_screen_extras_create},
                                      {"Settings", make_settings},
                                      {"Shuffle Songs", make_shuffle},
                                      {"Now Playing", make_nowplaying}};

static void home_tick(ui_screen_t *s) {
    menu_screen_t *m = (menu_screen_t *)s;
    static uint32_t last_ms;
    uint32_t now = os_tick_ms();
    if (now - last_ms < 500u && last_ms != 0) {
        return; /* the pane shows seconds and a card summary: twice a second is plenty */
    }
    last_ms = now;
    uint32_t sec = now / 1000u;
    char buf[40];
    snprintf(buf, sizeof buf, "Up %u:%02u:%02u", (unsigned)(sec / 3600u), (unsigned)(sec / 60u % 60u), (unsigned)(sec % 60u));
    if (strcmp(lv_label_get_text(m->uptime), buf) != 0) {
        lv_label_set_text(m->uptime, buf);
    }
    // Storage summary: what we really know, nothing invented (no RTC, no track).
    if (hal_storage_ready()) {
        hal_card_info_t info;
        if (hal_storage_get_info(&info) == HAL_STORAGE_OK) {
            char sz[16];
            ui_format_size(info.capacity_bytes, sz, sizeof sz);
            snprintf(buf, sizeof buf, "%s %s", hal_storage_card_type_str(info.type), sz);
        } else {
            snprintf(buf, sizeof buf, "Card present");
        }
    } else {
        snprintf(buf, sizeof buf, "No card");
    }
    if (strcmp(lv_label_get_text(m->storage), buf) != 0) {
        lv_label_set_text(m->storage, buf);
    }
}

ui_screen_t *ui_screen_home_create(void) {
    menu_screen_t *m = menu_new("neuPod", k_home, (int32_t)(sizeof k_home / sizeof k_home[0]), 160);
    if (m == NULL) {
        return NULL;
    }
    lv_obj_t *pane = pane_new(m->base.root, 160, 160);
    ui_artwork(pane, 32, 32, 96);
    lv_obj_t *np = ui_label(pane, "Nothing playing", UI_L_DIM);
    lv_obj_set_width(np, 160);
    lv_obj_set_style_text_align(np, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(np, 0, 140);
    m->uptime = ui_label(pane, "", UI_L_TEXT);
    lv_obj_set_width(m->uptime, 160);
    lv_obj_set_style_text_align(m->uptime, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(m->uptime, 0, 162);
    m->storage = ui_label(pane, "", UI_L_DIM);
    lv_obj_set_width(m->storage, 160);
    lv_obj_set_style_text_align(m->storage, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(m->storage, 0, 182);
    m->base.on_tick = home_tick;
    home_tick(&m->base);
    return &m->base;
}
