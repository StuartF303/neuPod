// SPDX-License-Identifier: MPL-2.0
/*
 * Music > Artists / Albums / Songs / Genres / Compilations, the lists under them, Update Library and Home > Shuffle Songs
 * (docs/ui.md, docs/adr/0017-music-library-index.md).
 *
 * One screen type serves every list. It never reads the index itself (FatFs belongs to the storage task): it asks the library service for the
 * list's size, then for a window of rows around the selection (library_service.h), and shows a "..." placeholder for a row that is not in the window
 * yet, which the next fill replaces. With no index on the card a top-level list builds it first (once nothing is playing). States: opening, ready,
 * building, a message (with a Try again button where that can help), done (Update Library). Every state but a list with rows is a full-pane layout
 * (ui_pane.c, design review 3.3 / LB-4): the empty list, the build with its progress bar (files counted first, so a fraction), the error. Menu on the
 * build pane runs it in the background: the screen goes, the status bar shows busy dots, and a toast says how it ended (ui_library_background()).
 */
#include <stdio.h>
#include <string.h>

#include "art_service.h"
#include "input_events.h"
#include "library_service.h"
#include "os.h"
#include "player.h"
#include "ui_input.h"
#include "ui_library.h"

typedef enum { M_LIST, M_UPDATE, M_SHUFFLE } mode_t_;
typedef enum { S_OPEN, S_READY, S_BUILD, S_MSG, S_DONE } state_t;

#define OPEN_PANE_MS 300u /* an open that takes longer than this shows its pane (a quick one never flashes it) */

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
    uint32_t shown_files, shown_total, shown_ms;
    uint32_t open_ms;           /* S_OPEN: when it began */
    bool bg;                    /* the build was sent to the background: leaving must not cancel it */
    ui_pane_t *pane;            /* the full-pane layout of every state but a list with rows */
    bool empty_toasted;
    wi_letterjump_t lj;
    ui_letter_overlay_t *overlay;
    /* An album's header (LVW_ALBUM): art, title, "Artist . year", "n songs . m:ss . FLAC", and the rule under it */
    ui_artbox_t *hd_art;
    lv_obj_t *hd_title, *hd_l2, *hd_l3, *hd_rule;
    bool hd_filled;           /* the header text has been filled from the index */
    char hd_handle[24];       /* library handle of the album's first song: what its art is asked for by */
} lib_screen_t;

static uint32_t g_seq;

/* The letter the rail shows for a name: the first letter of its sort key (articles and accents do not count), '#' for anything else. */
static char letter_of(const char *text) {
    uint8_t key[8];
    (void)lib_collate_key(text, NULL, key, sizeof key);
    unsigned b = lib_collate_bucket(key);
    return b == 0 ? '#' : (char)('A' + (b - 1u));
}

static bool is_top(uint8_t kind) { return kind <= LVW_COMPILATIONS; }

/* Lists of songs show two lines per row: the title and "Artist . Album" (docs/ui.md "Library lists"). */
static bool two_line_kind(uint8_t kind) { return kind == LVW_SONGS || kind == LVW_ARTIST_SONGS || kind == LVW_GENRE_SONGS; }

static const char *codec_name(uint8_t c) {
    switch (c) {
    case LIB_CODEC_WAV: return "WAV";
    case LIB_CODEC_FLAC: return "FLAC";
    case LIB_CODEC_MP3: return "MP3";
    case LIB_CODEC_ALAC: return "ALAC";
    case LIB_CODEC_AAC: return "AAC";
    default: return "";
    }
}

/* 1204 -> "1,204" (the status bar's count) */
static void fmt_count(char *out, size_t n, uint32_t v) {
    if (v >= 1000000u) {
        snprintf(out, n, "%u,%03u,%03u", (unsigned)(v / 1000000u), (unsigned)(v / 1000u % 1000u), (unsigned)(v % 1000u));
    } else if (v >= 1000u) {
        snprintf(out, n, "%u,%03u", (unsigned)(v / 1000u), (unsigned)(v % 1000u));
    } else {
        snprintf(out, n, "%u", (unsigned)v);
    }
}

// The empty list's heading and what to do about it (a sub-list cannot be empty in practice: the index only has what it has).
static const char *empty_heading(uint8_t kind) {
    switch (kind) {
    case LVW_ARTISTS: return "No artists yet";
    case LVW_ALBUMS: return "No albums yet";
    case LVW_SONGS: return "No songs yet";
    case LVW_GENRES: return "No genres yet";
    case LVW_COMPILATIONS: return "No compilations";
    default: return "Nothing here";
    }
}

/* ---- model ----------------------------------------------------------------------------------------------------------------- */

static int32_t item_count(const lib_screen_t *s) { return s->st == S_READY ? (int32_t)s->info.count : 0; } /* every other state is a pane, not rows */

static void lib_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    lib_screen_t *s = ctx;
    out->dim = true;
    if (s->st != S_READY || s->info.count == 0) {
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
    out->dim = r.child.kind == LVW_KINDS && r.play != LIB_PLAY_OK; /* a song the player cannot play (D6): listed, greyed */
    if (s->view.kind == LVW_ALBUM) { /* "3   Title", the length at the right; a song that cannot play names its codec in brackets */
        char num[12] = "";
        if (r.track != 0) {
            snprintf(num, sizeof num, "%u   ", (unsigned)r.track);
        }
        if (out->dim && r.codec != LIB_CODEC_NONE) {
            snprintf(out->label, sizeof out->label, "%s%s (%s)", num, r.text, codec_name(r.codec));
        } else {
            snprintf(out->label, sizeof out->label, "%s%s", num, r.text);
        }
    } else {
        snprintf(out->label, sizeof out->label, "%s", r.text);
    }
    if (two_line_kind(s->view.kind)) {
        snprintf(out->sub, sizeof out->sub, "%s", r.sub); /* the artist moved to the second line: the codec of a greyed song (LB-5) at the right */
        if (out->dim && r.codec != LIB_CODEC_NONE) {
            snprintf(out->detail, sizeof out->detail, "%s", codec_name(r.codec));
        }
    } else {
        snprintf(out->detail, sizeof out->detail, "%s", r.detail);
    }
    out->chevron = r.child.kind != LVW_KINDS;
}

/* ---- requests ------------------------------------------------------------------------------------------------------------- */

/* ---- album header (LB-3) --------------------------------------------------------------------------------------------------- */

static void fmt_length(char *out, size_t n, uint32_t s) {
    if (s >= 3600u) {
        snprintf(out, n, "%u:%02u:%02u", (unsigned)(s / 3600u), (unsigned)(s / 60u % 60u), (unsigned)(s % 60u));
    } else {
        snprintf(out, n, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
    }
}

// Fill the header from the index once it is open: "Artist . 2019", "3 songs . 9:41 . FLAC"; ask for the album's art by its first song.
static void header_fill(lib_screen_t *s) {
    if (s->hd_title == NULL || s->hd_filled || s->st != S_READY) {
        return;
    }
    s->hd_filled = true;
    char b[LVW_TEXT_MAX + 16];
    ui_label_set_text(s->hd_title, s->info.title);
    if (s->info.year != 0) {
        snprintf(b, sizeof b, "%s \xC2\xB7 %u", s->info.artist, (unsigned)s->info.year);
    } else {
        snprintf(b, sizeof b, "%s", s->info.artist);
    }
    ui_label_set_text(s->hd_l2, b);
    char len[16];
    fmt_length(len, sizeof len, s->info.total_s);
    const char *codec = codec_name(s->info.codec);
    snprintf(b, sizeof b, "%u %s%s%s%s%s", (unsigned)s->info.count, s->info.count == 1 ? "song" : "songs", s->info.total_s != 0 ? " \xC2\xB7 " : "",
             s->info.total_s != 0 ? len : "", codec[0] != '\0' ? " \xC2\xB7 " : "", codec);
    ui_label_set_text(s->hd_l3, b);
    if (s->info.count > 0 && pq_format_handle(s->hd_handle, sizeof s->hd_handle, (uint8_t)s->info.range.order, s->info.range.first)) {
        art_browse_want(s->hd_handle);
    }
}

static void header_create(lib_screen_t *s, const char *title) {
    lv_obj_t *r = s->base.root;
    s->hd_art = ui_artbox_create(r, UIM->np_margin, UIM->hd_art_y, UIM->hd_art);
    int32_t w = UI_W - UIM->hd_text_x - UIM->np_margin;
    s->hd_title = ui_label(r, title, UI_C_INK);
    lv_obj_set_style_text_font(s->hd_title, UIM->font_large, 0);
    lv_obj_set_size(s->hd_title, w, UIM->np_title_h);
    lv_label_set_long_mode(s->hd_title, LV_LABEL_LONG_DOT);
    ui_label_ticker(s->hd_title, true);
    lv_obj_set_pos(s->hd_title, UIM->hd_text_x, UIM->hd_line1_y);
    s->hd_l2 = ui_label(r, "", UI_C_INK2);
    lv_obj_set_size(s->hd_l2, w, UIM->line_h);
    lv_label_set_long_mode(s->hd_l2, LV_LABEL_LONG_DOT);
    ui_label_ticker(s->hd_l2, true);
    lv_obj_set_pos(s->hd_l2, UIM->hd_text_x, UIM->hd_line2_y);
    s->hd_l3 = ui_label_small(r, "", UI_C_INK3);
    lv_obj_set_size(s->hd_l3, w, UIM->np_album_h);
    lv_label_set_long_mode(s->hd_l3, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s->hd_l3, UIM->hd_text_x, UIM->hd_line3_y);
    s->hd_rule = lv_obj_create(r);
    lv_obj_remove_style_all(s->hd_rule);
    lv_obj_set_size(s->hd_rule, UI_W - 2 * UIM->np_margin, 1);
    lv_obj_set_pos(s->hd_rule, UIM->np_margin, UIM->hd_rule_y);
    ui_obj_bg(s->hd_rule, UI_C_LINE, 0);
}

// The header's art: the album's thumbnail once the art service has it, the placeholder before that (and when the album has no picture).
static void header_art_tick(lib_screen_t *s) {
    if (s->hd_art == NULL) {
        return;
    }
    art_view_t v;
    art_browse_get(&v);
    if (v.state == ART_VIEW_READY && v.w != 0) {
        ui_artbox_show(s->hd_art, v.px, v.w, v.h, v.gen);
    } else {
        ui_artbox_show(s->hd_art, NULL, 0, 0, v.gen);
    }
}

/* "552 of 1,204 songs . Aurora Lane" (the count is known), "552 songs found . Aurora Lane" (the first pass has not finished or found none) */
static void build_caption(const library_progress_t *p, char *out, size_t n) {
    char a[16], b[16];
    fmt_count(a, sizeof a, p->files);
    if (p->total != 0) {
        fmt_count(b, sizeof b, p->total);
        snprintf(out, n, "%s of %s songs%s%s", a, b, p->folder[0] != '\0' ? " \xC2\xB7 " : "", p->folder);
    } else if (p->files == 0 && p->folder[0] == '\0') {
        snprintf(out, n, "Counting files...");
    } else {
        snprintf(out, n, "%s %s found%s%s", a, p->files == 1 ? "song" : "songs", p->folder[0] != '\0' ? " \xC2\xB7 " : "", p->folder);
    }
}

// The full-pane layout for the state (LB-4); a list with rows hides it.
static void pane_update(lib_screen_t *s) {
    if (s->pane == NULL) {
        return;
    }
    const bool rows = s->st == S_READY && s->info.count > 0;
    ui_pane_show(s->pane, !rows);
    if (rows) {
        return;
    }
    char b[96];
    switch (s->st) {
    case S_OPEN:
        ui_pane_set(s->pane, "Opening library", NULL, NULL, NULL, NULL);
        ui_pane_set_bar(s->pane, UI_PANE_BAR_SWEEP, NULL);
        ui_pane_show(s->pane, os_tick_ms() - s->open_ms >= OPEN_PANE_MS); /* a quick open never flashes it */
        break;
    case S_BUILD: {
        library_progress_t p;
        library_progress(&p);
        build_caption(&p, b, sizeof b);
        ui_pane_set(s->pane, "Building library", NULL, NULL, NULL, "Menu: run in background \xC2\xB7 Select: cancel");
        ui_pane_set_bar(s->pane, p.total != 0 ? (int32_t)((uint64_t)p.files * 100u / p.total) : UI_PANE_BAR_SWEEP, b);
        break;
    }
    case S_MSG:
        ui_pane_set(s->pane, s->msg, s->hint, NULL, s->retry ? "Try again" : NULL, s->retry ? "Select: try again \xC2\xB7 Menu: back" : "Select or Menu: back");
        ui_pane_set_bar(s->pane, UI_PANE_BAR_NONE, NULL);
        break;
    case S_DONE: {
        library_progress_t p;
        library_progress(&p);
        char a[16], c[16], d[16];
        fmt_count(a, sizeof a, p.tracks);
        fmt_count(c, sizeof c, p.artists);
        fmt_count(d, sizeof d, p.albums);
        snprintf(b, sizeof b, "%s %s \xC2\xB7 %s %s \xC2\xB7 %s %s", a, p.tracks == 1 ? "song" : "songs", c, p.artists == 1 ? "artist" : "artists", d,
                 p.albums == 1 ? "album" : "albums");
        ui_pane_set(s->pane, "Library updated", b, NULL, "Done", NULL);
        ui_pane_set_bar(s->pane, UI_PANE_BAR_NONE, NULL);
        break;
    }
    case S_READY: /* a list with nothing in it: what to do about it, and the one button that does it */
        ui_pane_set(s->pane, empty_heading(s->view.kind), "Copy music to the card,", "then update the library.", "Update Library", "Menu: back");
        ui_pane_set_bar(s->pane, UI_PANE_BAR_NONE, NULL);
        break;
    }
}

// The list and the status bar's left slot (design review 3.10: lists show their count) move together; the pane follows the state.
static void set_count(lib_screen_t *s) {
    ui_list_set_count(s->list, item_count(s));
    pane_update(s);
    if (s->st == S_READY && s->mode == M_LIST && s->info.count > 0) { /* an empty list shows no count */
        char c[16];
        fmt_count(c, sizeof c, s->info.count);
        ui_screen_set_left(&s->base, s->view.kind == LVW_ALBUM ? s->info.title : c); /* an album: its name (the header shows it big) */
        header_fill(s);
    } else {
        ui_screen_set_left(&s->base, "");
    }
}

static void show_msg(lib_screen_t *s, libsvc_status_t st, const char *text, const char *hint, bool retry) {
    s->st = S_MSG;
    s->status = st;
    snprintf(s->msg, sizeof s->msg, "%s", text != NULL ? text : library_status_text(st));
    s->hint = hint;
    s->retry = retry;
    set_count(s);
    if (st == LIBSVC_NEEDS_WRITABLE && !s->empty_toasted) { /* "needs a writable card" is also said as a toast (design review 3.3) */
        s->empty_toasted = true;
        ui_toast_show(library_status_text(st), UI_TOAST_WARN, 0);
    }
}

static void open_info(lib_screen_t *s) {
    s->st = S_OPEN;
    s->info_seq = ++g_seq;
    s->info_pending = true;
    s->rows_pending = false;
    s->miss_idx = -1;
    s->win_n = 0;
    s->open_ms = os_tick_ms();
    set_count(s);
    if (!library_request_info(s->view, s->info_seq)) {
        s->info_pending = false;
        show_msg(s, LIBSVC_BUSY, NULL, NULL, true);
    }
}

// A build that was sent to the background (Menu on its pane): no screen owns it, ui_library_background() takes its answer.
static bool g_bg_active;
static uint32_t g_bg_seq;

static void begin_build(lib_screen_t *s) {
    s->st = S_BUILD;
    s->shown_files = UINT32_MAX;
    s->shown_ms = 0;
    set_count(s);
    if (g_bg_active) { /* one is already running in the background: this screen takes it over instead of asking for another */
        g_bg_active = false;
        library_build_background(false);
        s->build_seq = g_bg_seq;
        return;
    }
    s->build_seq = ++g_seq;
    if (!library_request_build(s->build_seq)) {
        show_msg(s, LIBSVC_BUSY, NULL, NULL, true);
    }
}

bool ui_library_background(void) {
    if (!g_bg_active) {
        return false;
    }
    libsvc_status_t st;
    if (!library_poll(LIBSVC_REQ_BUILD, g_bg_seq, &st)) {
        return true;
    }
    g_bg_active = false;
    library_build_background(false);
    library_progress_t p;
    library_progress(&p);
    if (st == LIBSVC_OK) {
        char c[16], t[40];
        fmt_count(c, sizeof c, p.tracks);
        snprintf(t, sizeof t, "Library updated \xC2\xB7 %s %s", c, p.tracks == 1 ? "song" : "songs");
        ui_toast_show(t, UI_TOAST_INFO, 0);
    } else if (st == LIBSVC_CANCELLED || st == LIBSVC_BUSY) { /* the card was wanted for something else (a Play press, another list): the old index stands */
        ui_toast_show("Library update stopped", UI_TOAST_WARN, 0);
    } else {
        ui_toast_show(library_status_text(st), UI_TOAST_WARN, 0);
    }
    return false;
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
    (void)s;
    ui_toast_show(text, UI_TOAST_WARN, 0);
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
    wi_letterjump_tick(&s->lj, now);
    ui_letter_overlay_update(s->overlay, &s->lj); /* every pass: it fades out once the wheel has rested */
    if (s->st == S_READY) {
        header_art_tick(s);
    }
    libsvc_status_t st;
    switch (s->st) {
    case S_OPEN:
        pane_update(s); /* the "Opening library" pane appears after OPEN_PANE_MS */
        ui_pane_tick(s->pane, now);
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
        ui_pane_tick(s->pane, now);
        if ((p.files != s->shown_files || p.total != s->shown_total) && now - s->shown_ms >= PROGRESS_MS) {
            s->shown_files = p.files;
            s->shown_total = p.total;
            s->shown_ms = now;
            pane_update(s);
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
            wi_letterjump_scroll(&s->lj, steps, os_tick_ms(), letter_of(r.text)); /* by the sort key: "The Beatles" is under B, accents are folded */
            ui_letter_overlay_update(s->overlay, &s->lj);
        }
    }
}

static void select_row(lib_screen_t *s) {
    int32_t i = ui_list_selected(s->list);
    lvw_row_t r;
    if (s->info.count == 0) { /* the empty pane's button */
        ui_frame_push(ui_screen_library_update_create());
        return;
    }
    if (i < 0 || !library_row(s->view, (uint32_t)i, &r)) {
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
    if (btn == HAL_BTN_MENU && s->st == S_BUILD) { /* "run in background": the screen goes (the frame pops), the build carries on */
        s->bg = true;
        g_bg_active = true;
        g_bg_seq = s->build_seq;
        library_build_background(true);
        return false;
    }
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
    if (s->st == S_READY && s->info.count == 0) { /* Update Library was run from the empty pane: look again */
        open_info(s);
        return;
    }
    /* A fill asked for before a child list was opened may have been answered while this screen was not polling, and the child's own fills then
     * replaced that answer (one slot per kind): the poll would never match. Forget it; the misses ask again. */
    s->rows_pending = false;
    s->miss_idx = -1;
    ui_list_refresh(s->list); /* the window may belong to another list now: the misses ask for a new fill */
}

static void lib_destroy(ui_screen_t *b) {
    lib_screen_t *s = (lib_screen_t *)b;
    if (s->st == S_BUILD && !s->bg) {
        library_cancel_build();
    }
    ui_pane_destroy(s->pane);
    ui_letter_overlay_destroy(s->overlay);
    ui_list_destroy(s->list);
    if (s->hd_art != NULL) {
        art_browse_want(""); /* the header's art is no longer wanted */
        ui_artbox_destroy(s->hd_art);
    }
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
    bool header = view.kind == LVW_ALBUM && mode == M_LIST;
    if (header) {
        header_create(s, title);
    }
    s->list = ui_list_create(s->base.root, UI_W,
                             &(ui_list_model_t){.count = 1, .ctx = s, .get = lib_get, .two_line = two_line_kind(view.kind), .top = header ? UIM->hd_rule_y + 2 : 0});
    s->pane = ui_pane_create(s->base.root); /* above the list: a state without rows covers it */
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
