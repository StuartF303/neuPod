// SPDX-License-Identifier: MPL-2.0
/*
 * Music > Artists / Albums / Songs / Genres / Compilations, the lists under them, Update Library and Home > Shuffle Songs
 * (docs/ui.md, docs/adr/0017-music-library-index.md).
 *
 * One screen type serves every list. It never reads the index itself (FatFs belongs to the storage task): it asks the library service for the
 * list's size, then for a window of rows around the selection (library_service.h), and shows a "..." placeholder for a row that is not in the window
 * yet, which the next fill replaces. With no index on the card a top-level list builds it first (progress rows with busy dots, Cancel on Select),
 * once nothing is playing. States: opening, ready, building, a message (with "Select to try again" where that can help), done (Update Library).
 */
#include <stdio.h>
#include <string.h>

#include "input_events.h"
#include "library_service.h"
#include "os.h"
#include "player.h"
#include "ui_input.h"
#include "ui_library.h"

typedef enum { M_LIST, M_UPDATE, M_SHUFFLE } mode_t_;
typedef enum { S_OPEN, S_READY, S_BUILD, S_MSG, S_DONE } state_t;

#define PREFETCH_EDGE 3 /* rows from the edge of the window at which the next fill is asked for */
#define PROGRESS_MS 250u

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    lvw_view_t view;
    lvw_info_t info;
    mode_t_ mode;
    state_t st;
    char title[LVW_TEXT_MAX];
    uint32_t seq;
    uint32_t info_seq, rows_seq, build_seq;
    bool info_pending, rows_pending;
    int32_t miss_idx;           /* a visible row that was not in the window (-1 = none) */
    uint32_t win_first, win_n;  /* the window last asked for */
    libsvc_status_t status;     /* S_MSG */
    char msg[64];
    const char *hint;           /* S_MSG: a second line, or NULL */
    bool retry;                 /* S_MSG: Select tries again */
    bool pop_me;                /* leave on the next tick (a screen may not pop itself from inside a button handler or resume) */
    bool played;                /* Shuffle Songs started: when Now Playing is left, leave too */
    uint32_t shown_files, shown_ms;
    lv_obj_t *toast;
    uint32_t toast_until;
    wi_letterjump_t lj;
    ui_letter_overlay_t *overlay;
} lib_screen_t;

static uint32_t g_seq;

static bool is_top(uint8_t kind) { return kind <= LVW_COMPILATIONS; }

static const char *empty_text(uint8_t kind) {
    switch (kind) {
    case LVW_ARTISTS: return "No artists";
    case LVW_ALBUMS: return "No albums";
    case LVW_SONGS: return "No songs";
    case LVW_GENRES: return "No genres";
    case LVW_COMPILATIONS: return "No compilations";
    default: return "Empty";
    }
}

/* ---- model ----------------------------------------------------------------------------------------------------------------- */

static int32_t item_count(const lib_screen_t *s) {
    switch (s->st) {
    case S_READY: return s->info.count > 0 ? (int32_t)s->info.count : 1;
    case S_BUILD: return 3;
    case S_MSG: return 1 + (s->hint != NULL ? 1 : 0) + (s->retry ? 1 : 0);
    case S_DONE: return 2;
    default: return 1;
    }
}

static void lib_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    lib_screen_t *s = ctx;
    out->dim = true;
    switch (s->st) {
    case S_OPEN:
        snprintf(out->label, sizeof out->label, "Opening library...");
        out->busy = true;
        return;
    case S_BUILD: {
        library_progress_t p;
        library_progress(&p);
        if (idx == 0) {
            snprintf(out->label, sizeof out->label, "Building library...");
            out->busy = true;
            out->dim = false;
        } else if (idx == 1) {
            snprintf(out->label, sizeof out->label, p.files == 1 ? "%u song found" : "%u songs found", (unsigned)p.files);
        } else {
            snprintf(out->label, sizeof out->label, "Select to cancel");
        }
        return;
    }
    case S_MSG:
        if (idx == 0) {
            snprintf(out->label, sizeof out->label, "%s", s->msg);
        } else if (idx == 1 && s->hint != NULL) {
            snprintf(out->label, sizeof out->label, "%s", s->hint);
        } else {
            snprintf(out->label, sizeof out->label, "Select to try again");
        }
        return;
    case S_DONE: {
        library_progress_t p;
        library_progress(&p);
        if (idx == 0) {
            snprintf(out->label, sizeof out->label, "Library updated");
            out->dim = false;
        } else {
            snprintf(out->label, sizeof out->label, "%u songs, %u artists, %u albums", (unsigned)p.tracks, (unsigned)p.artists, (unsigned)p.albums);
        }
        return;
    }
    case S_READY: {
        if (s->info.count == 0) {
            snprintf(out->label, sizeof out->label, "%s", empty_text(s->view.kind));
            return;
        }
        lvw_row_t r;
        if (!library_row(s->view, (uint32_t)idx, &r)) {
            snprintf(out->label, sizeof out->label, "...");
            if (s->miss_idx < 0) {
                s->miss_idx = idx;
            }
            return;
        }
        snprintf(out->label, sizeof out->label, "%s", r.text);
        snprintf(out->detail, sizeof out->detail, "%s", r.detail);
        out->chevron = r.child.kind != LVW_KINDS;
        out->dim = r.child.kind == LVW_KINDS && r.play != LIB_PLAY_OK; /* a song the player cannot play (D6): listed, greyed */
        return;
    }
    }
}

/* ---- requests ------------------------------------------------------------------------------------------------------------- */

static void set_count(lib_screen_t *s) { ui_list_set_count(s->list, item_count(s)); }

static void show_msg(lib_screen_t *s, libsvc_status_t st, const char *text, const char *hint, bool retry) {
    s->st = S_MSG;
    s->status = st;
    snprintf(s->msg, sizeof s->msg, "%s", text != NULL ? text : library_status_text(st));
    s->hint = hint;
    s->retry = retry;
    set_count(s);
}

static void open_info(lib_screen_t *s) {
    s->st = S_OPEN;
    s->info_seq = ++g_seq;
    s->info_pending = true;
    s->rows_pending = false;
    s->miss_idx = -1;
    s->win_n = 0;
    set_count(s);
    if (!library_request_info(s->view, s->info_seq)) {
        s->info_pending = false;
        show_msg(s, LIBSVC_BUSY, NULL, NULL, true);
    }
}

static void begin_build(lib_screen_t *s) {
    s->st = S_BUILD;
    s->shown_files = UINT32_MAX;
    s->shown_ms = 0;
    s->build_seq = ++g_seq;
    set_count(s);
    if (!library_request_build(s->build_seq)) {
        show_msg(s, LIBSVC_BUSY, NULL, NULL, true);
    }
}

static void request_rows(lib_screen_t *s, uint32_t first) {
    uint32_t count = s->info.count;
    if (count > LIBSVC_ROWS && first + LIBSVC_ROWS > count) {
        first = count - LIBSVC_ROWS;
    }
    s->rows_seq = ++g_seq;
    s->win_first = first;
    s->win_n = count - first < LIBSVC_ROWS ? count - first : LIBSVC_ROWS;
    s->rows_pending = library_request_rows(s->view, first, LIBSVC_ROWS, s->rows_seq);
}

static uint32_t centred(int32_t idx) { return idx >= 5 ? (uint32_t)idx - 5u : 0u; }

static const char *hint_for(libsvc_status_t st) {
    switch (st) {
    case LIBSVC_NEEDS_WRITABLE: return "The index is saved on the card";
    case LIBSVC_NO_CARD: return "Put the card in, then try again";
    case LIBSVC_PLAYING: return "The scan needs the player stopped";
    default: return NULL;
    }
}

static void start_shuffle(lib_screen_t *s) {
    if (s->info.count == 0) {
        show_msg(s, LIBSVC_OK, "No songs to shuffle", "Add music to the card, then Update Library", false);
        return;
    }
    player_play_library(&s->info.range, PLAY_QUEUE_NO_ENTRY, true);
    s->played = true;
    ui_frame_push(ui_screen_nowplaying_create());
}

static void show_toast(lib_screen_t *s, const char *text) {
    ui_label_set_text(s->toast, text);
    lv_obj_set_hidden(s->toast, false);
    s->toast_until = os_tick_ms() + 3000u;
}

/* ---- screen callbacks ---------------------------------------------------------------------------------------------------- */

static void lib_tick(ui_screen_t *b) {
    lib_screen_t *s = (lib_screen_t *)b;
    if (s->pop_me) {
        s->pop_me = false;
        ui_frame_pop(); /* destroys this screen: touch nothing after it */
        return;
    }
    uint32_t now = os_tick_ms();
    if (s->lj.active) {
        wi_letterjump_tick(&s->lj, now);
        ui_letter_overlay_update(s->overlay, &s->lj);
    }
    if (s->toast_until != 0 && (int32_t)(now - s->toast_until) >= 0) {
        s->toast_until = 0;
        lv_obj_set_hidden(s->toast, true);
    }
    libsvc_status_t st;
    switch (s->st) {
    case S_OPEN:
        if (s->info_pending && library_poll(LIBSVC_REQ_INFO, s->info_seq, &st)) {
            s->info_pending = false;
            if (st == LIBSVC_OK) {
                library_info(&s->info);
                s->st = S_READY;
                if (s->mode == M_LIST && s->info.title[0] != '\0') {
                    snprintf(s->title, sizeof s->title, "%s", s->info.title);
                    s->base.title = s->title;
                    ui_frame_set_title(s->title);
                }
                set_count(s);
                if (s->mode == M_SHUFFLE) {
                    start_shuffle(s);
                } else if (s->info.count > 0) {
                    request_rows(s, 0);
                }
            } else if ((st == LIBSVC_NONE || st == LIBSVC_DAMAGED) && (s->mode != M_LIST || is_top(s->view.kind))) {
                begin_build(s); /* no index yet (or an old one): build it, once nothing plays */
            } else {
                show_msg(s, st, NULL, hint_for(st), true);
            }
        }
        break;
    case S_BUILD: {
        library_progress_t p;
        library_progress(&p);
        if (p.files != s->shown_files && now - s->shown_ms >= PROGRESS_MS) {
            s->shown_files = p.files;
            s->shown_ms = now;
            ui_list_refresh(s->list);
        }
        if (library_poll(LIBSVC_REQ_BUILD, s->build_seq, &st)) {
            if (st == LIBSVC_OK) {
                if (s->mode == M_UPDATE) {
                    s->st = S_DONE;
                    set_count(s);
                } else {
                    open_info(s);
                }
            } else if (st == LIBSVC_CANCELLED) {
                s->pop_me = true;
            } else {
                show_msg(s, st, NULL, hint_for(st), true);
            }
        }
        break;
    }
    case S_READY:
        if (s->rows_pending && library_poll(LIBSVC_REQ_ROWS, s->rows_seq, &st)) {
            s->rows_pending = false;
            if (st == LIBSVC_OK) {
                ui_list_refresh(s->list);
            } else {
                show_msg(s, st, NULL, NULL, true);
                break;
            }
        }
        if (!s->rows_pending && s->info.count > 0) {
            int32_t sel = ui_list_selected(s->list);
            if (s->miss_idx >= 0) { /* a visible row has no data: fill around it */
                int32_t at = s->miss_idx;
                s->miss_idx = -1;
                request_rows(s, centred(at));
            } else if (s->win_n != 0 &&
                       ((sel < (int32_t)s->win_first + PREFETCH_EDGE && s->win_first > 0) ||
                        (sel >= (int32_t)(s->win_first + s->win_n) - PREFETCH_EDGE && s->win_first + s->win_n < s->info.count))) {
                request_rows(s, centred(sel)); /* the selection is nearing the edge of the window: move it ahead of the scrolling */
            }
        }
        break;
    default: break;
    }
}

static void lib_scroll(ui_screen_t *b, int32_t steps) {
    lib_screen_t *s = (lib_screen_t *)b;
    ui_list_move(s->list, steps);
    if (s->st == S_READY && is_top(s->view.kind) && s->info.count > 0) {
        lvw_row_t r;
        int32_t i = ui_list_selected(s->list);
        if (library_row(s->view, (uint32_t)i, &r) && r.text[0] != '\0') {
            wi_letterjump_scroll(&s->lj, steps, os_tick_ms(), r.text[0]);
            ui_letter_overlay_update(s->overlay, &s->lj);
        }
    }
}

static void select_row(lib_screen_t *s) {
    int32_t i = ui_list_selected(s->list);
    lvw_row_t r;
    if (s->info.count == 0 || i < 0 || !library_row(s->view, (uint32_t)i, &r)) {
        return; /* a placeholder: nothing to act on yet */
    }
    if (r.child.kind != LVW_KINDS) {
        ui_frame_push(ui_screen_library_view_create(r.child.kind, r.child.id, r.text));
        return;
    }
    if (r.play != LIB_PLAY_OK) { /* D6: a file the player has no decoder for says why instead of opening */
        show_toast(s, lib_cant_play_str(r.play));
        return;
    }
    player_play_library(&s->info.range, (uint32_t)i - s->info.head, false); /* the whole list is the queue, from this song */
    ui_frame_push(ui_screen_nowplaying_create());
}

static bool lib_button(ui_screen_t *b, uint8_t btn) {
    lib_screen_t *s = (lib_screen_t *)b;
    if (btn != HAL_BTN_SELECT) {
        return false; /* Menu leaves (the frame pops), Play/Pause and Left/Right keep their global meaning */
    }
    switch (s->st) {
    case S_READY: select_row(s); break;
    case S_BUILD:
        library_cancel_build();
        s->pop_me = true;
        break;
    case S_MSG:
        if (s->retry) {
            if (s->mode == M_UPDATE) {
                begin_build(s);
            } else {
                open_info(s);
            }
        } else {
            s->pop_me = true;
        }
        break;
    case S_DONE: s->pop_me = true; break;
    default: break;
    }
    return true;
}

static void lib_suspend(ui_screen_t *b) { ui_list_suspend(((lib_screen_t *)b)->list); }

static void lib_resume(ui_screen_t *b) {
    lib_screen_t *s = (lib_screen_t *)b;
    if (s->played) { /* Shuffle Songs: Now Playing was left, so is this */
        s->pop_me = true;
    }
    ui_list_resume(s->list);
    /* A fill asked for before a child list was opened may have been answered while this screen was not polling, and the child's own fills then
     * replaced that answer (one slot per kind): the poll would never match. Forget it; the misses ask again. */
    s->rows_pending = false;
    s->miss_idx = -1;
    ui_list_refresh(s->list); /* the window may belong to another list now: the misses ask for a new fill */
}

static void lib_destroy(ui_screen_t *b) {
    lib_screen_t *s = (lib_screen_t *)b;
    if (s->st == S_BUILD) {
        library_cancel_build();
    }
    ui_letter_overlay_destroy(s->overlay);
    ui_list_destroy(s->list);
}

static ui_screen_t *screen_new(lvw_view_t view, mode_t_ mode, const char *title) {
    lib_screen_t *s = (lib_screen_t *)ui_screen_new(sizeof *s, "");
    if (s == NULL) {
        return NULL;
    }
    snprintf(s->title, sizeof s->title, "%s", title);
    s->base.title = s->title;
    s->view = view;
    s->mode = mode;
    s->miss_idx = -1;
    s->base.on_scroll = lib_scroll;
    s->base.on_button = lib_button;
    s->base.on_tick = lib_tick;
    s->base.suspend = lib_suspend;
    s->base.resume = lib_resume;
    s->base.destroy = lib_destroy;
    s->list = ui_list_create(s->base.root, UI_W, &(ui_list_model_t){.count = 1, .ctx = s, .get = lib_get});
    s->toast = ui_label(s->base.root, "", UI_L_TEXT);
    lv_obj_set_size(s->toast, UI_W, UI_ROW_H);
    lv_obj_set_style_bg_color(s->toast, lv_color_hex(0xFFF3CD), 0);
    lv_obj_set_style_bg_opa(s->toast, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s->toast, UIM->toast_pad, 0);
    lv_obj_set_pos(s->toast, 0, UI_CONTENT_H - UI_ROW_H);
    lv_label_set_long_mode(s->toast, LV_LABEL_LONG_DOT);
    lv_obj_set_hidden(s->toast, true);
    wi_letterjump_init(&s->lj);
    s->overlay = ui_letter_overlay_create(s->base.root);
    return &s->base;
}

ui_screen_t *ui_screen_library_view_create(uint8_t kind, uint32_t id, const char *title) {
    ui_screen_t *b = screen_new((lvw_view_t){kind, id}, M_LIST, title);
    if (b != NULL) {
        open_info((lib_screen_t *)b);
    }
    return b;
}

ui_screen_t *ui_screen_library_update_create(void) {
    ui_screen_t *b = screen_new((lvw_view_t){LVW_SONGS, 0}, M_UPDATE, "Update Library");
    if (b != NULL) {
        begin_build((lib_screen_t *)b);
    }
    return b;
}

ui_screen_t *ui_screen_shuffle_create(void) {
    ui_screen_t *b = screen_new((lvw_view_t){LVW_SONGS, 0}, M_SHUFFLE, "Shuffle Songs");
    if (b != NULL) {
        open_info((lib_screen_t *)b);
    }
    return b;
}
