// SPDX-License-Identifier: MPL-2.0
/*
 * Home menu, Music, Extras, Now Playing and the placeholder screens (docs/ui.md mock-ups: classic split
 * menu, blue selection bar, lightly modernised; original drawings, no third-party artwork).
 */
#include <stdio.h>
#include <string.h>

#include "art_service.h"
#include "battery_store.h"
#include "hal_storage.h"
#include "hal_system.h"
#include "input_events.h"
#include "library_service.h"
#include "os.h"
#include "player.h"
#include "sys_clock.h"
#include "ui_internal.h"
#include "ui_library.h"

/* ---- generic menu screen: a list of entries, Select pushes the entry's screen ----------------------- */

typedef struct {
    const char *label;
    ui_screen_t *(*create)(void);
    bool plain; /* no chevron: the row acts at once instead of opening a list (Shuffle Songs) */
} menu_entry_t;

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    const menu_entry_t *entries;
    int32_t n;
    /* Home's right panel (design review 3.1). Playing: art, title, artist, a 3 px progress bar, elapsed and remaining. Idle: the clock (48 px), the date and
     * "1,204 songs . 72 %"; with no clock set, "Nothing playing"; with no library, a prompt. */
    lv_obj_t *title, *artist, *bar_track, *bar_fill, *t_el, *t_rem, *icon_pause; /* playing */
    lv_obj_t *clock, *date, *count, *hint;                                         /* idle */
    lv_obj_t *art_box, *art_img, *art_note; /* art box, thumbnail inside it, note shown without a thumbnail */
    lv_image_dsc_t art_dsc;     /* points at the art service's thumbnail; nothing is copied */
    uint32_t art_gen, last_ms;
    bool art_shown, playing, refresh; /* refresh: the next tick skips the rate limit and re-sets the thumbnail (after resume) */
    int32_t bar_w;              /* the bar's fill width now (-1 = unknown) */
    bool queued;                /* something is queued or playing: Now Playing is selectable */
    /* the library's size for the idle line (one Info request when the panel goes idle or comes back) */
    uint8_t lib_state;          /* 0 unknown, 1 known (lib_songs), 2 not built */
    uint32_t lib_songs, lib_seq;
    bool lib_pending;
} menu_screen_t;

static void menu_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    const menu_screen_t *m = ctx;
    snprintf(out->label, sizeof out->label, "%s", m->entries[idx].label);
    out->chevron = !m->entries[idx].plain;
    out->dim = m->entries[idx].create == ui_screen_nowplaying_create && !m->queued; /* greyed while nothing is queued */
}

static void menu_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((menu_screen_t *)s)->list, steps); }

static bool menu_button(ui_screen_t *s, uint8_t btn) {
    menu_screen_t *m = (menu_screen_t *)s;
    if (btn == HAL_BTN_SELECT) {
        int32_t i = ui_list_selected(m->list);
        if (i >= 0 && i < m->n && m->entries[i].create != NULL) {
            if (m->entries[i].create == ui_screen_nowplaying_create && !m->queued) {
                ui_toast_show("Nothing playing", UI_TOAST_INFO, 0);
                return true;
            }
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
    ui_obj_bg(p, UI_C_SURFACE, 0);
    ui_obj_border(p, UI_C_LINE, 1, LV_BORDER_SIDE_LEFT);
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
static ui_screen_t *make_shuffle(void) { return ui_screen_shuffle_create(); }

/* ---- Music: the library lists (ui_library.c) and the one list that is still empty ------------------------------- */

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
} empty_list_t;

static void empty_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    (void)ctx;
    (void)idx;
    snprintf(out->label, sizeof out->label, "None yet");
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
static ui_screen_t *make_artists(void) { return ui_screen_library_view_create(LVW_ARTISTS, 0, "Artists"); }
static ui_screen_t *make_albums(void) { return ui_screen_library_view_create(LVW_ALBUMS, 0, "Albums"); }
static ui_screen_t *make_songs(void) { return ui_screen_library_view_create(LVW_SONGS, 0, "Songs"); }
static ui_screen_t *make_genres(void) { return ui_screen_library_view_create(LVW_GENRES, 0, "Genres"); }
static ui_screen_t *make_comps(void) { return ui_screen_library_view_create(LVW_COMPILATIONS, 0, "Compilations"); }
static ui_screen_t *make_playlists(void) { return empty_list("Playlists"); }

static const menu_entry_t k_music[] = {{"Artists", make_artists},         {"Albums", make_albums},      {"Songs", make_songs},
                                       {"Genres", make_genres},           {"Compilations", make_comps}, {"Playlists", make_playlists},
                                       {"Update Library", ui_screen_library_update_create}};

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

static ui_screen_t *make_settings(void) { return ui_screen_settings_create(); }

static const menu_entry_t k_home[] = {{"Music", ui_screen_music_create, false},
                                      {"Podcasts", make_podcasts, false},
                                      {"Photos", make_photos, false},
                                      {"Extras", ui_screen_extras_create, false},
                                      {"Settings", make_settings, false},
                                      {"Shuffle Songs", make_shuffle, true},
                                      {"Now Playing", ui_screen_nowplaying_create, false}};

static void home_set(lv_obj_t *l, const char *t) { ui_label_set_text(l, t); }

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

/* Switch the panel between idle (clock, date, count) and playing (art, title, artist, progress). */
static void home_mode(menu_screen_t *m, bool playing) {
    if (playing == m->playing) {
        return;
    }
    m->playing = playing;
    home_hide(m->art_box, !playing);
    home_hide(m->title, !playing);
    home_hide(m->artist, !playing);
    home_hide(m->bar_track, !playing);
    home_hide(m->bar_fill, !playing);
    home_hide(m->t_el, !playing);
    home_hide(m->t_rem, !playing);
    home_hide(m->icon_pause, true);
    home_hide(m->clock, playing);
    home_hide(m->date, playing);
    home_hide(m->count, playing);
    home_hide(m->hint, playing);
    m->bar_w = -1;
}

/* Nothing to free: it only exists so the frame calls resume (it only does when suspend ran first). */
static void home_suspend(ui_screen_t *s) { (void)s; }

static void home_resume(ui_screen_t *s) {
    menu_screen_t *m = (menu_screen_t *)s;
    m->refresh = true; /* the art generation may have moved while Now Playing was on top: tick at once, re-set the thumbnail */
    m->lib_state = 0;  /* the library may have been built or rebuilt meanwhile: ask again when idle */
}

static void fmt_mmss(char *out, size_t n, const char *sign, uint32_t s) {
    snprintf(out, n, "%s%u:%02u", sign, (unsigned)(s / 60u), (unsigned)(s % 60u));
}

static void fmt_songs(char *out, size_t n, uint32_t v) {
    if (v >= 1000u) {
        snprintf(out, n, "%u,%03u", (unsigned)(v / 1000u), (unsigned)(v % 1000u));
    } else {
        snprintf(out, n, "%u", (unsigned)v);
    }
}

// The idle line's library size: one Info request for the song list; "not built" when the card has no index yet. Only while idle (it uses the card).
static void home_lib_poll(menu_screen_t *m) {
    libsvc_status_t st;
    if (m->lib_pending && library_poll(LIBSVC_REQ_INFO, m->lib_seq, &st)) {
        m->lib_pending = false;
        if (st == LIBSVC_OK) {
            lvw_info_t info;
            library_info(&info);
            m->lib_songs = info.count;
            m->lib_state = 1;
        } else if (st == LIBSVC_NONE || st == LIBSVC_DAMAGED) {
            m->lib_state = 2;
        } else {
            m->lib_state = 0; /* no card, a busy card: say nothing, ask again at the next visit */
        }
        m->last_ms = 0;
    } else if (!m->lib_pending && m->lib_state == 0 && hal_storage_ready() && !ui_library_background() && os_tick_ms() > 1500u) {
        static uint32_t seq;
        m->lib_seq = 0x40000000u + ++seq; /* its own range: the library screens count from 1 */
        m->lib_pending = library_request_info((lvw_view_t){LVW_SONGS, 0}, m->lib_seq);
    }
}

static void home_tick(ui_screen_t *s) {
    menu_screen_t *m = (menu_screen_t *)s;
    uint32_t now = os_tick_ms();
    clock_tm_t clk;
    char hm[8] = "";
    const bool have_clock = clock_local_now(&clk);
    if (have_clock) {
        snprintf(hm, sizeof hm, "%02d:%02d", (int)clk.h, (int)clk.mi);
    }
    ui_screen_set_left(&m->base, hm); /* the status bar's left slot on Home is the clock (design review 3.10) */
    /* The player and the art view are polled on every tick: the art decoder only waits ~300 ms for our ack before it overwrites the
     * thumbnail we point at. Label writes are strcmp-guarded, so nothing is redrawn needlessly. */
    player_info_t n;
    player_get(&n);
    char buf[96];
    bool live = n.state == PLAYER_PLAYING || n.state == PLAYER_PAUSED || n.state == PLAYER_ENDED;
    bool file = n.src == PLAYER_SRC_FILE && (live || n.state == PLAYER_OPENING || n.state == PLAYER_ERROR || n.state == PLAYER_VERIFYING ||
                                             n.state == PLAYER_VERIFIED);
    bool tone = n.src == PLAYER_SRC_TONE;
    bool queued = n.src != PLAYER_SRC_NONE;
    if (queued != m->queued) { /* Now Playing greys out while nothing is queued */
        m->queued = queued;
        ui_list_refresh(m->list);
    }
    bool changed = (file || tone) != m->playing;
    home_mode(m, file || tone);
    home_art(m, file && live);
    bool force = m->refresh || changed;
    m->refresh = false;
    if (file || tone) {
        if (file) {
            home_set(m->title, n.tags.title[0] != '\0' ? n.tags.title : n.name);
            home_set(m->artist, n.tags.artist);
        } else {
            home_set(m->title, n.name);
            home_set(m->artist, "Test signal");
        }
        int32_t bw = 0;
        if (file && n.rate != 0 && n.total_frames != 0 && live) {
            uint32_t total = (uint32_t)(n.total_frames / n.rate);
            uint32_t el = n.state == PLAYER_ENDED ? total : n.elapsed_s;
            fmt_mmss(buf, sizeof buf, "", el);
            home_set(m->t_el, buf);
            fmt_mmss(buf, sizeof buf, "-", total > el ? total - el : 0);
            home_set(m->t_rem, buf);
            const int32_t full = UI_W - UIM->home_list_w - 2 * UIM->home_art_off;
            bw = (int32_t)((uint64_t)full * (n.state == PLAYER_ENDED ? n.total_frames : (uint64_t)el * n.rate) / n.total_frames);
            if (bw > full) {
                bw = full;
            }
        } else {
            home_set(m->t_el, file ? "--:--" : "");
            home_set(m->t_rem, "");
        }
        if (bw != m->bar_w) {
            m->bar_w = bw;
            lv_obj_set_width(m->bar_fill, bw > 0 ? bw : 1);
            home_hide(m->bar_fill, bw <= 0);
        }
        /* paused: the pause glyph in front of the elapsed time */
        const bool paused = n.state == PLAYER_PAUSED;
        home_hide(m->icon_pause, !paused);
        int32_t ex = UIM->home_art_off + (paused ? lv_font_get_line_height(UIM->font_icon_sm) + 2 : 0);
        if (lv_obj_get_x(m->t_el) != ex) {
            lv_obj_set_x(m->t_el, ex);
        }
        return;
    }
    home_lib_poll(m);
    if (!force && now - m->last_ms < 500u && m->last_ms != 0) {
        return; /* idle panel: a clock and a count, twice a second is plenty */
    }
    m->last_ms = now;
    if (have_clock) { // a real clock once NTP (or a manual set) has told us the time
        static const char *const wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char *const mn[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        home_set(m->clock, hm);
        snprintf(buf, sizeof buf, "%s %d %s", wd[clk.wday % 7], (int)clk.d, mn[(clk.mo + 11) % 12]);
        home_set(m->date, buf);
        home_hide(m->clock, false);
    } else {
        home_hide(m->clock, true); /* no clock: the date line says what is going on instead */
        home_set(m->date, "Nothing playing");
    }
    // "1,204 songs . 72 %": what we know, nothing invented. No library yet: a prompt for it.
    hal_battery_status_t bs;
    char pct[16] = "";
    if (battery_store_get(&bs)) {
        snprintf(pct, sizeof pct, "%u %%", (unsigned)bs.soc_pct);
    }
    if (m->lib_state == 1) {
        char c[16];
        fmt_songs(c, sizeof c, m->lib_songs);
        snprintf(buf, sizeof buf, "%s %s%s%s", c, m->lib_songs == 1 ? "song" : "songs", pct[0] != '\0' ? " \xC2\xB7 " : "", pct);
        home_set(m->hint, "");
    } else if (m->lib_state == 2) {
        snprintf(buf, sizeof buf, "No library yet%s%s", pct[0] != '\0' ? " \xC2\xB7 " : "", pct);
        home_set(m->hint, "Music > Update Library");
    } else {
        snprintf(buf, sizeof buf, "%s", pct);
        home_set(m->hint, "");
    }
    home_set(m->count, buf);
#ifdef NEUPOD_UI_DEBUG
    if (hal_storage_ready()) { /* the card summary ("SDHC 32 GB"): a debug line, not product copy (design review 3.1) */
        hal_card_info_t info;
        if (hal_storage_get_info(&info) == HAL_STORAGE_OK) {
            char sz[16];
            ui_format_size(info.capacity_bytes, sz, sizeof sz);
            snprintf(buf, sizeof buf, "%s %s", hal_storage_card_type_str(info.type), sz);
            home_set(m->hint, buf);
        }
    }
#endif
}

static lv_obj_t *home_label(lv_obj_t *pane, bool small, uint32_t colour, int32_t y, int32_t h, bool ticker) {
    lv_obj_t *l = small ? ui_label_small(pane, "", colour) : ui_label(pane, "", colour);
    lv_obj_set_size(l, UI_W - UIM->home_list_w, h); /* fixed height: a long title ends in dots instead of wrapping onto the next line */
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    if (ticker) {
        ui_label_ticker(l, true); /* the track title and artist scroll when too long */
    }
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, 0, y);
    return l;
}

ui_screen_t *ui_screen_home_create(void) {
    const int32_t pane_w = UI_W - UIM->home_list_w;
    menu_screen_t *m = menu_new("neuPod", k_home, (int32_t)(sizeof k_home / sizeof k_home[0]), UIM->home_list_w);
    if (m == NULL) {
        return NULL;
    }
    lv_obj_t *pane = pane_new(m->base.root, UIM->home_list_w, pane_w);
    const int32_t sh = lv_font_get_line_height(UIM->font_small);
    m->art_box = ui_artwork(pane, UIM->home_art_off, UIM->home_art_off, UIM->home_art); /* keeps its ring and dot while there is no thumbnail */
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
    lv_obj_set_hidden(m->art_box, true);
    /* playing */
    m->title = home_label(pane, false, UI_C_INK, UIM->home_title_y, UIM->line_h, true);
    m->artist = home_label(pane, true, UI_C_INK2, UIM->home_artist_y, sh, true);
    const int32_t full = pane_w - 2 * UIM->home_art_off;
    m->bar_track = lv_obj_create(pane);
    lv_obj_remove_style_all(m->bar_track);
    lv_obj_set_size(m->bar_track, full, UIM->home_bar_h);
    lv_obj_set_pos(m->bar_track, UIM->home_art_off, UIM->home_bar_y);
    lv_obj_set_scrollable(m->bar_track, false);
    ui_obj_bg(m->bar_track, UI_C_LINE, 0);
    m->bar_fill = lv_obj_create(pane);
    lv_obj_remove_style_all(m->bar_fill);
    lv_obj_set_size(m->bar_fill, 1, UIM->home_bar_h);
    lv_obj_set_pos(m->bar_fill, UIM->home_art_off, UIM->home_bar_y);
    lv_obj_set_scrollable(m->bar_fill, false);
    ui_obj_bg(m->bar_fill, UI_C_ACCENT, 0);
    m->icon_pause = ui_icon(pane, UI_ICON_PAUSE, false, UI_C_INK2);
    lv_obj_set_pos(m->icon_pause, UIM->home_art_off, UIM->home_time_y);
    m->t_el = ui_label_small(pane, "", UI_C_INK2);
    lv_obj_set_pos(m->t_el, UIM->home_art_off, UIM->home_time_y);
    m->t_rem = ui_label_small(pane, "", UI_C_INK2);
    lv_obj_set_size(m->t_rem, full, sh);
    lv_obj_set_style_text_align(m->t_rem, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(m->t_rem, UIM->home_art_off, UIM->home_time_y);
    /* idle */
    m->clock = home_label(pane, false, UI_C_INK, UIM->home_clock_y, lv_font_get_line_height(UIM->font_huge), false);
    lv_obj_set_style_text_font(m->clock, UIM->font_huge, 0);
    m->date = home_label(pane, false, UI_C_INK, UIM->home_date_y, UIM->line_h, false);
    m->count = home_label(pane, true, UI_C_INK3, UIM->home_count_y, sh, false);
    m->hint = home_label(pane, true, UI_C_INK3, UIM->home_count_y + sh + 4, sh, false);
    m->playing = true; /* so home_mode() below builds the idle state */
    home_mode(m, false);
    m->base.on_tick = home_tick;
    m->base.suspend = home_suspend;
    m->base.resume = home_resume;
    m->bar_w = -1;
    home_tick(&m->base);
    return &m->base;
}
