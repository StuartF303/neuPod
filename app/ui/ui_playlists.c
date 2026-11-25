// SPDX-License-Identifier: MPL-2.0
/*
 * Music > Playlists (design review LB-7, docs/ui.md "Playlists"): the list of lists, and one list's songs.
 *
 * The lists are the five smart ones (On-The-Go, Recently added, Most played, Top rated, Unplayed) and the .m3u / .m3u8 files found on the card, each with
 * its count on the right. Select opens one: two-line rows (title, then artist or folder); a row the card cannot play is greyed with the reason on its
 * second line and answers Select with a toast. Select on a song plays the whole list from it. In On-The-Go a long press of Select takes the song
 * off the list, and a last row clears the list (after a question). Like the library screens it never touches the card: it posts requests to the storage
 * task (playlist_service.h) and polls (full-pane layouts while it waits or when there is nothing to show: ui_pane.c).
 */
#include <stdio.h>
#include <string.h>

#include "input_events.h"
#include "os.h"
#include "player.h"
#include "playlist_service.h"
#include "ui_library.h"

#define PANE_MS 300u /* a request that takes longer than this shows its "Loading" pane (a quick one never flashes it) */

static uint32_t g_seq = 0x5000u; /* distinct from the library's sequence numbers is not needed (own answer slot); the offset only keeps log lines apart */

static void fmt_count(char *out, size_t n, uint32_t v, bool capped) {
    if (v >= 1000u) {
        snprintf(out, n, "%u,%03u%s", (unsigned)(v / 1000u), (unsigned)(v % 1000u), capped ? "+" : "");
    } else {
        snprintf(out, n, "%u%s", (unsigned)v, capped ? "+" : "");
    }
}

/* ---- the list of lists ----------------------------------------------------------------------------------------------------------------- */

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    ui_pane_t *pane;
    uint32_t seq;
    bool pending, first;     /* a catalog request is out; no catalog has arrived yet (the pane shows instead of the list) */
    uint32_t open_ms;
    uint32_t shown_n;        /* rows the list was last given */
    bool failed;
    pls_status_t status;
} pc_t;

static void pc_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    (void)ctx;
    pls_item_t it;
    if (!pls_catalog_item((uint32_t)idx, &it)) {
        snprintf(out->label, sizeof out->label, "...");
        out->dim = true;
        return;
    }
    snprintf(out->label, sizeof out->label, "%s", it.name);
    if (it.count_known) {
        fmt_count(out->detail, sizeof out->detail, it.count, it.count_capped);
    }
    out->chevron = true;
}

static void pc_pane(pc_t *p) {
    bool rows = pls_catalog_count() > 0;
    if (p->failed && !rows) {
        ui_pane_set(p->pane, pls_status_text(p->status), NULL, NULL, "Try again", "Select: try again \xC2\xB7 Menu: back");
        ui_pane_set_bar(p->pane, UI_PANE_BAR_NONE, NULL);
        ui_pane_show(p->pane, true);
    } else if (!rows) {
        ui_pane_set(p->pane, "Loading playlists", NULL, NULL, NULL, NULL);
        ui_pane_set_bar(p->pane, UI_PANE_BAR_SWEEP, NULL);
        ui_pane_show(p->pane, os_tick_ms() - p->open_ms >= PANE_MS);
    } else {
        ui_pane_show(p->pane, false);
    }
}

static void pc_request(pc_t *p) {
    p->seq = ++g_seq;
    p->failed = false;
    p->open_ms = os_tick_ms();
    p->pending = pls_request_catalog(p->seq);
    if (!p->pending) {
        p->failed = true;
        p->status = PLS_BUSY;
    }
    pc_pane(p);
}

static void pc_sync(pc_t *p) {
    uint32_t n = pls_catalog_count();
    if (n != p->shown_n) {
        p->shown_n = n;
        ui_list_set_count(p->list, (int32_t)n);
    } else {
        ui_list_refresh(p->list);
    }
    char c[12];
    snprintf(c, sizeof c, "%u", (unsigned)n);
    ui_screen_set_left(&p->base, n != 0 ? c : "");
    pc_pane(p);
}

static void pc_tick(ui_screen_t *s) {
    pc_t *p = (pc_t *)s;
    pls_status_t st;
    if (p->pending) {
        ui_pane_tick(p->pane, os_tick_ms());
        if (pls_poll(p->seq, &st)) {
            p->pending = false;
            p->first = false;
            if (st != PLS_OK) {
                p->failed = true;
                p->status = st;
            }
            pc_sync(p);
        } else if (pls_catalog_count() == 0) {
            pc_pane(p); /* the "Loading" pane appears after PANE_MS */
        }
    }
}

static void pc_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((pc_t *)s)->list, steps); }

static bool pc_button(ui_screen_t *s, uint8_t btn) {
    pc_t *p = (pc_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    if (p->failed && pls_catalog_count() == 0) {
        pc_request(p);
        return true;
    }
    pls_item_t it;
    if (p->pending || !pls_catalog_item((uint32_t)ui_list_selected(p->list), &it)) {
        return true; /* a refresh is under way (the file numbers may change under it) or the row is a placeholder */
    }
    if (!ui_frame_push(ui_screen_playlist_view_create(it.kind, it.file, it.name))) {
        ui_toast_show("Can't open: too many screens", UI_TOAST_WARN, 0);
    }
    return true;
}

static void pc_suspend(ui_screen_t *s) { ui_list_suspend(((pc_t *)s)->list); }

static void pc_resume(ui_screen_t *s) {
    pc_t *p = (pc_t *)s;
    ui_list_resume(p->list);
    pc_request(p); /* counts change (On-The-Go, plays): refresh them, the old ones stay on show until the new ones arrive */
}

static void pc_destroy(ui_screen_t *s) {
    pc_t *p = (pc_t *)s;
    ui_pane_destroy(p->pane);
    ui_list_destroy(p->list);
}

ui_screen_t *ui_screen_playlists_create(void) {
    pc_t *p = (pc_t *)ui_screen_new(sizeof *p, "Playlists");
    if (p == NULL) {
        return NULL;
    }
    p->base.on_tick = pc_tick;
    p->base.on_scroll = pc_scroll;
    p->base.on_button = pc_button;
    p->base.suspend = pc_suspend;
    p->base.resume = pc_resume;
    p->base.destroy = pc_destroy;
    p->first = true;
    p->list = ui_list_create(p->base.root, UI_W, &(ui_list_model_t){.count = 0, .ctx = p, .get = pc_get});
    p->pane = ui_pane_create(p->base.root);
    pc_request(p);
    return &p->base;
}

/* ---- one list ----------------------------------------------------------------------------------------------------------------------------- */

typedef enum { V_LOAD, V_READY, V_MSG } vstate_t;

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    ui_pane_t *pane;
    uint8_t kind, file;
    char name[PLS_NAME_MAX];
    vstate_t st;
    uint32_t seq, open_ms;
    bool pending;
    pls_status_t status;
    uint32_t n;              /* entries of the open list */
    bool trunc;
    uint32_t otg_seq;        /* an On-The-Go change (remove / clear) is out */
    bool otg_pending;
    int32_t keep_sel;        /* the row to go back to when the list has been read again (-1 = the first) */
    bool sel_down, sel_used; /* On-The-Go: Select acts on release (a short press plays) or on the hold (removes) */
    uint32_t sel_ms;
    bool pop_me;
} pv_t;

static bool is_otg(const pv_t *v) { return v->kind == PLS_OTG; }
static int32_t pv_rows(const pv_t *v) { return v->st == V_READY ? (int32_t)v->n + (is_otg(v) && v->n > 0 ? 1 : 0) : 0; }

static void pv_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    const pv_t *v = ctx;
    pls_entry_view_t e;
    if (v->st == V_READY && (uint32_t)idx < v->n && pls_entry((uint32_t)idx, &e)) {
        snprintf(out->label, sizeof out->label, "%s", e.title);
        snprintf(out->sub, sizeof out->sub, "%s", e.sub);
        out->dim = e.state != PLS_ENT_OK;
    } else if (v->st == V_READY && (uint32_t)idx == v->n) {
        snprintf(out->label, sizeof out->label, "Clear On-The-Go");
        out->chevron = true;
    } else {
        snprintf(out->label, sizeof out->label, "...");
        out->dim = true;
    }
}

static const char *empty_heading(uint8_t kind) {
    switch (kind) {
    case PLS_OTG: return "On-The-Go is empty";
    case PLS_MOST: return "Nothing played yet";
    case PLS_TOP: return "No rated songs yet";
    case PLS_RECENT: return "Nothing new";
    case PLS_UNPLAYED: return "Everything is played";
    default: return "This playlist is empty";
    }
}

static void pv_pane(pv_t *v) {
    if (v->st == V_READY && pv_rows(v) > 0) {
        ui_pane_show(v->pane, false);
        return;
    }
    switch (v->st) {
    case V_LOAD:
        ui_pane_set(v->pane, "Loading", NULL, NULL, NULL, NULL);
        ui_pane_set_bar(v->pane, UI_PANE_BAR_SWEEP, NULL);
        ui_pane_show(v->pane, os_tick_ms() - v->open_ms >= PANE_MS);
        break;
    case V_MSG:
        ui_pane_set(v->pane, pls_status_text(v->status), v->status == PLS_NO_LIBRARY ? "Music > Update Library" : NULL, NULL, "Try again",
                    "Select: try again \xC2\xB7 Menu: back");
        ui_pane_set_bar(v->pane, UI_PANE_BAR_NONE, NULL);
        ui_pane_show(v->pane, true);
        break;
    case V_READY: {
        const char *l1 = NULL, *l2 = NULL;
        if (v->kind == PLS_OTG) {
            l1 = "On Now Playing, press Select and";
            l2 = "choose Add to On-The-Go.";
        } else if (v->kind == PLS_TOP) {
            l1 = "Rate songs on the Now Playing page:";
            l2 = "four stars or more are listed here.";
        } else if (v->kind == PLS_MOST) {
            l1 = "Songs you play appear here,";
            l2 = "most played first.";
        }
        ui_pane_set(v->pane, empty_heading(v->kind), l1, l2, NULL, "Menu: back");
        ui_pane_set_bar(v->pane, UI_PANE_BAR_NONE, NULL);
        ui_pane_show(v->pane, true);
        break;
    }
    }
}

static void pv_sync(pv_t *v) {
    ui_list_set_count(v->list, pv_rows(v));
    ui_list_refresh(v->list);
    char c[16] = "";
    if (v->st == V_READY && v->n > 0) {
        fmt_count(c, sizeof c, v->n, v->trunc);
    }
    ui_screen_set_left(&v->base, c);
    pv_pane(v);
}

static void pv_request(pv_t *v) {
    v->st = V_LOAD;
    v->seq = ++g_seq;
    v->open_ms = os_tick_ms();
    v->pending = pls_request_open(v->kind, v->file, v->seq);
    if (!v->pending) {
        v->st = V_MSG;
        v->status = PLS_BUSY;
    }
    pv_sync(v);
}

static void pv_tick(ui_screen_t *s) {
    pv_t *v = (pv_t *)s;
    if (v->pop_me) {
        v->pop_me = false;
        ui_frame_pop();
        return;
    }
    uint32_t now = os_tick_ms();
    if (v->pending) {
        ui_pane_tick(v->pane, now);
        pls_status_t st;
        if (pls_poll(v->seq, &st)) {
            v->pending = false;
            if (st == PLS_OK) {
                v->st = V_READY;
                v->n = pls_entry_count();
                v->trunc = pls_entry_truncated();
            } else {
                v->st = V_MSG;
                v->status = st;
            }
            pv_sync(v);
            if (v->st == V_READY && v->keep_sel > 0) {
                ui_list_set_selected(v->list, v->keep_sel < pv_rows(v) ? v->keep_sel : pv_rows(v) - 1);
            }
            v->keep_sel = -1;
        } else {
            pv_pane(v);
        }
    }
    if (v->otg_pending) {
        pls_status_t st;
        if (pls_otg_poll(v->otg_seq, &st)) {
            v->otg_pending = false;
            if (st == PLS_OK) {
                v->keep_sel = ui_list_selected(v->list);
                pv_request(v); /* the list as it is now */
            } else {
                ui_toast_show(pls_status_text(st), UI_TOAST_WARN, 0);
            }
        }
    }
    if (v->sel_down && !v->sel_used && is_otg(v) && now - v->sel_ms >= UI_SELHOLD_MS) { /* held: take the song off the list */
        v->sel_used = true;
        int32_t i = ui_list_selected(v->list);
        if (v->st == V_READY && i >= 0 && (uint32_t)i < v->n && !v->otg_pending && !v->pending) {
            v->otg_seq = ++g_seq;
            v->otg_pending = pls_otg_remove((uint32_t)i, v->otg_seq);
            if (v->otg_pending) {
                ui_toast_show("Removed from On-The-Go", UI_TOAST_INFO, 0);
                ui_feedback_step();
            } else {
                ui_toast_show(pls_status_text(PLS_BUSY), UI_TOAST_WARN, 0);
            }
        }
    }
}

static void pv_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((pv_t *)s)->list, steps); }

static void clear_yes(void *ctx) {
    pv_t *v = ctx;
    v->otg_seq = ++g_seq;
    v->otg_pending = pls_otg_clear(v->otg_seq);
    ui_toast_show(v->otg_pending ? "On-The-Go cleared" : pls_status_text(PLS_BUSY), v->otg_pending ? UI_TOAST_INFO : UI_TOAST_WARN, 0);
}

static void play_from(pv_t *v, uint32_t at) {
    static const char *paths[PLS_ENTRIES_MAX]; /* UI task only; the queue copies what it needs */
    uint32_t m = 0, start = 0;
    for (uint32_t i = 0; i < v->n && m < PLS_ENTRIES_MAX; i++) {
        pls_entry_view_t e;
        if (!pls_entry(i, &e) || e.state != PLS_ENT_OK) {
            continue;
        }
        if (i == at) {
            start = m;
        }
        paths[m++] = e.path;
    }
    if (m == 0) {
        return;
    }
    player_play_paths(paths, m, start);
    if (!ui_frame_push(ui_screen_nowplaying_create())) {
        ui_toast_show("Can't open: too many screens", UI_TOAST_WARN, 0);
    }
}

static void pv_select(pv_t *v) {
    int32_t i = ui_list_selected(v->list);
    if (v->st == V_MSG) {
        pv_request(v);
        return;
    }
    if (v->st != V_READY || i < 0) {
        return;
    }
    if ((uint32_t)i == v->n && is_otg(v)) { /* the Clear row */
        char head[40];
        snprintf(head, sizeof head, "Clear On-The-Go?");
        char sub[48];
        snprintf(sub, sizeof sub, "%u %s come off the list.", (unsigned)v->n, v->n == 1 ? "song" : "songs");
        if (!ui_frame_push(ui_confirm_create("Clear", head, sub, "Clear", "Keep", clear_yes, v))) {
            ui_toast_show("Can't open: too many screens", UI_TOAST_WARN, 0);
        }
        return;
    }
    pls_entry_view_t e;
    if ((uint32_t)i >= v->n || !pls_entry((uint32_t)i, &e)) {
        return;
    }
    if (e.state != PLS_ENT_OK) {
        ui_toast_show(e.state == PLS_ENT_MISSING ? "Not on the card" : e.state == PLS_ENT_UNRESOLVABLE ? "Can't find this file" : e.sub[0] != '\0' ? e.sub : "Can't play this", UI_TOAST_WARN, 0);
        return;
    }
    play_from(v, (uint32_t)i);
}

static bool pv_button(ui_screen_t *s, uint8_t btn) {
    pv_t *v = (pv_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    if (is_otg(v) && v->st == V_READY && ui_list_selected(v->list) >= 0 && (uint32_t)ui_list_selected(v->list) < v->n) {
        v->sel_down = true; /* a song of On-The-Go: the press decides on release (play) or after UI_SELHOLD_MS (remove) */
        v->sel_used = false;
        v->sel_ms = os_tick_ms();
        return true;
    }
    pv_select(v);
    return true;
}

static void pv_button_up(ui_screen_t *s, uint8_t btn) {
    pv_t *v = (pv_t *)s;
    if (btn != HAL_BTN_SELECT || !v->sel_down) {
        return;
    }
    v->sel_down = false;
    if (!v->sel_used) {
        pv_select(v);
    }
    v->sel_used = false;
}

static void pv_suspend(ui_screen_t *s) { ui_list_suspend(((pv_t *)s)->list); }

static void pv_resume(ui_screen_t *s) {
    pv_t *v = (pv_t *)s;
    v->sel_down = false;
    ui_list_resume(v->list);
    v->keep_sel = ui_list_selected(v->list);
    pv_request(v); /* the card or the plays may have changed while Now Playing was on top (and the pool may hold another list by now) */
}

static void pv_destroy(ui_screen_t *s) {
    pv_t *v = (pv_t *)s;
    ui_pane_destroy(v->pane);
    ui_list_destroy(v->list);
}

ui_screen_t *ui_screen_playlist_view_create(uint8_t kind, uint8_t file, const char *name) {
    pv_t *v = (pv_t *)ui_screen_new(sizeof *v, "");
    if (v == NULL) {
        return NULL;
    }
    v->kind = kind;
    v->file = file;
    v->keep_sel = -1;
    snprintf(v->name, sizeof v->name, "%s", name);
    v->base.title = v->name;
    v->base.on_tick = pv_tick;
    v->base.on_scroll = pv_scroll;
    v->base.on_button = pv_button;
    v->base.on_button_up = pv_button_up;
    v->base.suspend = pv_suspend;
    v->base.resume = pv_resume;
    v->base.destroy = pv_destroy;
    v->list = ui_list_create(v->base.root, UI_W, &(ui_list_model_t){.count = 0, .ctx = v, .get = pv_get, .two_line = true});
    v->pane = ui_pane_create(v->base.root);
    pv_request(v);
    return &v->base;
}
