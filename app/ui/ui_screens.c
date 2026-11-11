// SPDX-License-Identifier: MPL-2.0
/*
 * Home menu, Music, Extras, Now Playing and the placeholder screens (docs/ui.md mock-ups: classic split
 * menu, blue selection bar, lightly modernised; original drawings, no third-party artwork).
 */
#include <stdio.h>
#include <string.h>

#include "art_service.h"
#include "hal_storage.h"
#include "hal_system.h"
#include "input_events.h"
#include "os.h"
#include "player.h"
#include "sys_clock.h"
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
    lv_obj_t *uptime, *storage; /* home right pane: lines 2 and 3 (idle: clock, card summary) */
    lv_obj_t *line1;            /* home right pane: line 1 (idle: "Nothing playing") */
    lv_obj_t *art_box, *art_img, *art_note; /* home right pane: art box, thumbnail inside it, note shown without a thumbnail */
    lv_image_dsc_t art_dsc;     /* points at the art service's thumbnail; nothing is copied */
    uint32_t art_gen, last_ms;
    bool art_shown, playing, refresh; /* refresh: the next tick skips the rate limit and re-sets the thumbnail (after resume) */
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
    lv_obj_set_style_radius(a, UIM->art_radius, 0);
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
    lv_obj_set_style_border_width(ring, UIM->art_ring_w, 0);
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
    lv_obj_set_style_text_font(a, UIM->font_large, 0);
    lv_obj_align(a, LV_ALIGN_CENTER, 0, UIM->ph_dy1);
    lv_obj_t *b = ui_label(p->base.root, line2, UI_L_DIM);
    lv_obj_set_width(b, UI_W - 2 * UIM->ph_margin);
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, UIM->ph_dy2);
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

static const menu_entry_t k_extras[] = {{"Files", ui_screen_files_create}, {"Test tone", ui_screen_testtone_create}, {"Input widgets", ui_screen_inputdemo_create}, {"Network test", ui_screen_nettest_create}};

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

static void home_set(lv_obj_t *l, const char *t) {
    if (strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

static void home_hide(lv_obj_t *o, bool hidden) {
    if (lv_obj_is_hidden(o) != hidden) {
        lv_obj_set_hidden(o, hidden);
    }
}

/* The art box while a file plays: the art service's thumbnail when it has one for the track, the note otherwise (same rule as Now
 * Playing, which leaves OPENING out because the view may still hold the previous album). The ack tells the decoder this screen no
 * longer points at the old pixels. */
static void home_art(menu_screen_t *m, bool live) {
    art_view_t v;
    art_view_get(&v);
    bool show = live && v.state == ART_VIEW_READY && v.w != 0 && v.h != 0;
    if (show) {
        if (!m->art_shown || m->art_gen != v.gen || m->refresh) {
            memset(&m->art_dsc, 0, sizeof m->art_dsc);
            m->art_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            m->art_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            m->art_dsc.header.w = v.w;
            m->art_dsc.header.h = v.h;
            m->art_dsc.header.stride = (uint32_t)v.w * 2u;
            m->art_dsc.data_size = (uint32_t)v.w * v.h * 2u;
            m->art_dsc.data = (const uint8_t *)v.px;
            lv_image_set_src(m->art_img, &m->art_dsc);
            lv_obj_invalidate(m->art_img);
            m->art_gen = v.gen;
            m->art_shown = true;
        }
    } else {
        m->art_shown = false;
    }
    home_hide(m->art_img, !m->art_shown);
    home_hide(m->art_note, !m->playing || m->art_shown);
    art_view_ack(v.gen);
}

/* Switch the pane between "idle" (placeholder ring, clock) and "playing" (note / thumbnail, track text). */
static void home_mode(menu_screen_t *m, bool playing) {
    if (playing == m->playing) {
        return;
    }
    m->playing = playing;
    home_hide(lv_obj_get_child(m->art_box, 0), playing); /* ring and dot of the placeholder */
    home_hide(lv_obj_get_child(m->art_box, 1), playing);
    lv_obj_set_style_text_color(m->line1, lv_color_hex(playing ? UI_L_TEXT : UI_L_DIM), 0);
    lv_obj_set_style_text_color(m->uptime, lv_color_hex(playing ? UI_L_DIM : UI_L_TEXT), 0);
}

/* Nothing to free: it only exists so the frame calls resume (it only does when suspend ran first). */
static void home_suspend(ui_screen_t *s) { (void)s; }

static void home_resume(ui_screen_t *s) {
    menu_screen_t *m = (menu_screen_t *)s;
    m->refresh = true; /* the art generation may have moved while Now Playing was on top: tick at once, re-set the thumbnail */
}

static void home_tick(ui_screen_t *s) {
    menu_screen_t *m = (menu_screen_t *)s;
    uint32_t now = os_tick_ms();
    /* The player and the art view are polled on every tick: the art decoder only waits ~300 ms for our ack before it overwrites the
     * thumbnail we point at. Label writes are strcmp-guarded, so nothing is redrawn needlessly. */
    player_info_t n;
    player_get(&n);
    char buf[96];
    bool live = n.state == PLAYER_PLAYING || n.state == PLAYER_PAUSED || n.state == PLAYER_ENDED;
    bool file = n.src == PLAYER_SRC_FILE && (live || n.state == PLAYER_OPENING || n.state == PLAYER_ERROR || n.state == PLAYER_VERIFYING ||
                                             n.state == PLAYER_VERIFIED);
    bool tone = n.src == PLAYER_SRC_TONE;
    bool changed = (file || tone) != m->playing;
    home_mode(m, file || tone);
    home_art(m, file && live);
    bool force = m->refresh || changed;
    m->refresh = false;
    if (file || tone) {
        const char *sym = n.state == PLAYER_PAUSED ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
        if (file) {
            home_set(m->line1, n.tags.title[0] != '\0' ? n.tags.title : n.name);
            home_set(m->uptime, n.tags.artist);
            if (n.state == PLAYER_ENDED) {
                snprintf(buf, sizeof buf, "Finished");
            } else if (n.state == PLAYER_ERROR || n.state == PLAYER_VERIFYING || n.state == PLAYER_VERIFIED) {
                snprintf(buf, sizeof buf, "%s", n.state == PLAYER_ERROR ? "Error" : n.state == PLAYER_VERIFYING ? "Verifying" : "Verified");
            } else if (n.queue_len > 1) {
                snprintf(buf, sizeof buf, "%s %u of %u", sym, (unsigned)n.queue_pos, (unsigned)n.queue_len);
            } else {
                snprintf(buf, sizeof buf, "%s %s", sym, n.state == PLAYER_PAUSED ? "Paused" : n.state == PLAYER_OPENING ? "Opening" : "Playing");
            }
        } else {
            home_set(m->line1, n.name);
            home_set(m->uptime, "Test signal");
            snprintf(buf, sizeof buf, "%s", sym);
        }
        home_set(m->storage, buf);
        return;
    }
    if (!force && now - m->last_ms < 500u && m->last_ms != 0) {
        return; /* idle pane: a clock and a card summary, twice a second is plenty */
    }
    m->last_ms = now;
    home_set(m->line1, "Nothing playing");
    clock_tm_t lt;
    if (clock_local_now(&lt)) { // a real clock once NTP (or a manual set) has told us the time
        static const char *const wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char *const mn[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        snprintf(buf, sizeof buf, "%02d:%02d  %s %d %s", (int)lt.h, (int)lt.mi, wd[lt.wday % 7], (int)lt.d, mn[(lt.mo + 11) % 12]);
    } else {
        snprintf(buf, sizeof buf, "--:--");
    }
    home_set(m->uptime, buf);
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
    home_set(m->storage, buf);
}

ui_screen_t *ui_screen_home_create(void) {
    const int32_t pane_w = UI_W - UIM->home_list_w;
    menu_screen_t *m = menu_new("neuPod", k_home, (int32_t)(sizeof k_home / sizeof k_home[0]), UIM->home_list_w);
    if (m == NULL) {
        return NULL;
    }
    lv_obj_t *pane = pane_new(m->base.root, UIM->home_list_w, pane_w);
    m->art_box = ui_artwork(pane, UIM->home_art_off, UIM->home_art_off, UIM->home_art); /* keeps its ring and dot (children 0, 1) while idle */
    m->art_img = lv_image_create(m->art_box);
    lv_obj_set_size(m->art_img, UIM->home_art, UIM->home_art);
    lv_image_set_inner_align(m->art_img, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_center(m->art_img);
    lv_obj_set_hidden(m->art_img, true);
    m->art_note = ui_label(m->art_box, LV_SYMBOL_AUDIO, 0xFFFFFF);
    lv_obj_set_style_text_font(m->art_note, UIM->font_huge, 0);
    lv_obj_set_style_text_opa(m->art_note, LV_OPA_50, 0);
    lv_obj_center(m->art_note);
    lv_obj_set_hidden(m->art_note, true);
    m->line1 = ui_label(pane, "Nothing playing", UI_L_DIM);
    m->uptime = ui_label(pane, "", UI_L_TEXT);
    m->storage = ui_label(pane, "", UI_L_DIM);
    lv_obj_t *lines[3] = {m->line1, m->uptime, m->storage};
    for (int i = 0; i < 3; i++) {
        lv_obj_set_width(lines[i], pane_w);
        lv_label_set_long_mode(lines[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(lines[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(lines[i], 0, UIM->home_line_y[i]);
    }
    m->base.on_tick = home_tick;
    m->base.suspend = home_suspend;
    m->base.resume = home_resume;
    home_tick(&m->base);
    return &m->base;
}
