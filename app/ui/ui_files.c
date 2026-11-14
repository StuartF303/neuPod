// SPDX-License-Identifier: MPL-2.0
/*
 * Extras -> Files: READ-ONLY browser of the SD card. Directory reads run in the storage task
 * (storage_service.h): the UI posts a request, shows "Loading..." and keeps scrolling/animating, then
 * takes the result. Dirs first, names, sizes; Select descends, Menu ascends (at the root it leaves).
 * A folder of more than FILES_MAX_ENTRIES entries is read a page at a time (design review 3.6, FL-1): the last row is "Load more" (the next page), a later page starts
 * with "Earlier entries"; a folder found to hold more than a page shows "256+" on its row when the list is shown again.
 */
#include <stdio.h>
#include <string.h>

#include "capabilities.h"
#include "nfs.h"
#include "os.h"
#include "player.h"
#include "storage_service.h"
#include "ui_input.h"
#include "ui_internal.h"

#define MAX_DEPTH 8

typedef enum { LOADING, READY, FAILED } files_state_t;

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    char path[NFS_PATH_MAX];
    int32_t sel_stack[MAX_DEPTH];
    int depth;
    int32_t restore_sel; /* selection to restore after an ascend, -1 = none */
    files_state_t state;
    uint32_t seq;
    uint32_t count;
    bool truncated;
    uint32_t page;                 /* which page of the folder is listed (0 = the first) */
    uint32_t page_stack[MAX_DEPTH]; /* the page of each parent listing, for the way back */
    uint32_t plus_page;            /* the page of that listing */
    int plus_depth;                /* the listing (by depth) that holds a folder known to be longer than a page, -1 = none ... */
    int32_t plus_row;              /* ... and its row there: it shows "256+" */
    char msg[72];
    wi_letterjump_t lj;           /* letter overlay while scrolling fast (the list is sorted by name within dirs/files) */
    ui_letter_overlay_t *overlay;
} files_t;

static uint32_t g_seq;

/* Rows before the entries: "Earlier entries" on any page after the first. */
static int32_t head_rows(const files_t *f) { return f->state == READY && f->page > 0 ? 1 : 0; }

static int32_t item_count(const files_t *f) {
    if (f->state != READY) {
        return 1; /* one status row */
    }
    return head_rows(f) + (int32_t)f->count + (f->truncated ? 1 : 0) + (f->count == 0 ? 1 : 0);
}

/* The entry behind list row @p idx; NULL for "Earlier entries", "Load more" and the status rows. */
static const file_entry_t *entry_at(const files_t *f, int32_t idx) {
    int32_t e = idx - head_rows(f);
    if (f->state != READY || e < 0 || (uint32_t)e >= f->count) {
        return NULL;
    }
    return &storage_list_entries()[e];
}

static void files_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    const files_t *f = ctx;
    if (f->state == LOADING) {
        snprintf(out->label, sizeof out->label, "Loading...");
        out->dim = true;
    } else if (f->state == FAILED) {
        snprintf(out->label, sizeof out->label, "%s", f->msg);
        out->dim = true;
    } else if (head_rows(f) != 0 && idx == 0) {
        snprintf(out->label, sizeof out->label, "Earlier entries");
    } else if (f->count == 0) {
        snprintf(out->label, sizeof out->label, "Empty folder");
        out->dim = true;
    } else if (entry_at(f, idx) == NULL) { /* the row after the last entry */
        snprintf(out->label, sizeof out->label, "Load more");
    } else {
        const file_entry_t *e = entry_at(f, idx); /* not NULL: tested above */
        snprintf(out->label, sizeof out->label, "%s", e->name);
        out->icon = e->is_dir ? UI_ICON_FOLDER : UI_ICON_FILE;
        if (e->is_dir) {
            out->chevron = true;
            if (f->plus_depth == f->depth && f->plus_page == f->page && f->plus_row == idx) {
                snprintf(out->detail, sizeof out->detail, "%u+", (unsigned)FILES_MAX_ENTRIES); /* opened before: it had more than a page, so 256 is a floor */
            }
        } else {
            ui_format_size(e->size, out->detail, sizeof out->detail);
        }
    }
}

/* The title stays "Files"; the folder is the status bar's left slot (design review 3.10). The slot is short: the root is "/", a folder under it
 * "/Name", deeper ones ".../Name" (the last folder is what matters; a name too long for the slot ends in dots). */
static void set_title(files_t *f) {
    const char *name = strrchr(f->path, '/');
    char left[NFS_PATH_MAX + 8];
    if (name == NULL || name == f->path) {
        snprintf(left, sizeof left, "%s", f->path);
    } else {
        snprintf(left, sizeof left, ".../%s", name + 1);
    }
    ui_screen_set_left(&f->base, left);
}

static void fail(files_t *f, const char *msg) {
    f->state = FAILED;
    snprintf(f->msg, sizeof f->msg, "%s", msg);
    ui_list_set_count(f->list, item_count(f));
}

static void load(files_t *f) {
    set_title(f);
    if (!caps_available(CAP_STORAGE)) {
        capabilities_t c;
        caps_get(&c);
        char m[72];
        snprintf(m, sizeof m, "Storage unavailable: %s", c.brief[CAP_STORAGE][0] ? c.brief[CAP_STORAGE] : "not checked");
        fail(f, m);
        return;
    }
    f->state = LOADING;
    f->seq = ++g_seq;
    ui_list_set_count(f->list, item_count(f)); /* stop reading the shared entries before the next request runs */
    if (!storage_list_request_page(f->path, f->seq, f->page)) {
        fail(f, "Storage busy, try again");
    }
}

static bool has_ext(const char *name, const char *ext) {
    size_t n = strlen(name), e = strlen(ext);
    if (n < e) {
        return false;
    }
    for (size_t i = 0; i < e; i++) {
        char c = name[n - e + i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + 'a' - 'A');
        }
        if (c != ext[i]) {
            return false;
        }
    }
    return true;
}

static void show_toast(files_t *f, const char *text) {
    (void)f;
    ui_toast_show(text, UI_TOAST_WARN, 0);
}

static void files_tick(ui_screen_t *s) {
    files_t *f = (files_t *)s;
    wi_letterjump_tick(&f->lj, os_tick_ms());
    ui_letter_overlay_update(f->overlay, &f->lj); /* every pass: it fades out once the wheel has rested */
    storage_result_t r;
    while (f->state == LOADING && storage_result_poll(&r)) {
        if (r.kind != STORAGE_RES_LIST || r.seq != f->seq) {
            continue; /* stale (another request's) result */
        }
        if (r.err != NFS_OK) {
            fail(f, r.err == NFS_ERR_NOT_MOUNTED ? "Storage not mounted" : nfs_err_str((nfs_err_t)r.err));
            return;
        }
        f->state = READY;
        f->count = r.count;
        f->truncated = r.truncated;
        if (f->depth > 0) { /* the row of this folder in its parent's listing: longer than a page, or not */
            if (f->truncated || f->page > 0) {
                f->plus_depth = f->depth - 1;
                f->plus_row = f->sel_stack[f->depth - 1];
                f->plus_page = f->page_stack[f->depth - 1];
            } else if (f->plus_depth == f->depth - 1 && f->plus_page == f->page_stack[f->depth - 1] && f->plus_row == f->sel_stack[f->depth - 1]) {
                f->plus_depth = -1;
            }
        }
        ui_list_set_count(f->list, item_count(f));
        if (f->restore_sel >= 0) {
            ui_list_set_selected(f->list, f->restore_sel);
            f->restore_sel = -1;
        }
    }
}

static void files_scroll(ui_screen_t *s, int32_t steps) {
    files_t *f = (files_t *)s;
    ui_list_move(f->list, steps);
    int32_t i = ui_list_selected(f->list);
    if (i >= 0 && entry_at(f, i) != NULL) {
        wi_letterjump_scroll(&f->lj, steps, os_tick_ms(), entry_at(f, i)->name[0]);
        ui_letter_overlay_update(f->overlay, &f->lj);
    }
}

static bool files_button(ui_screen_t *s, uint8_t btn) {
    files_t *f = (files_t *)s;
    if (btn == HAL_BTN_SELECT && f->state == READY) {
        int32_t i = ui_list_selected(f->list);
        const file_entry_t *ent = entry_at(f, i);
        if (ent == NULL && i >= 0 && (f->count != 0 || (head_rows(f) != 0 && i == 0))) { /* "Earlier entries" / "Load more": another page of the same folder */
            if (head_rows(f) != 0 && i == 0) {
                f->page--;
                f->restore_sel = (f->page > 0 ? 1 : 0) + (int32_t)FILES_MAX_ENTRIES; /* the "Load more" row of that page */
            } else {
                f->page++;
                f->restore_sel = 1; /* the first entry, below "Earlier entries" */
            }
            load(f);
            return true;
        }
        if (ent != NULL && ent->is_dir && f->depth < MAX_DEPTH) {
            const char *name = ent->name;
            size_t n = strlen(f->path);
            if (n + 1 + strlen(name) + 1 > sizeof f->path) {
                return true; /* path too long: stay */
            }
            f->page_stack[f->depth] = f->page;
            f->page = 0;
            f->sel_stack[f->depth++] = i;
            if (n > 1) {
                f->path[n++] = '/';
            }
            strcpy(f->path + n, name);
            f->restore_sel = -1;
            load(f);
        } else if (ent != NULL && !ent->is_dir) {
            const char *name = ent->name;
            if (has_ext(name, ".wav") || has_ext(name, ".flac") || has_ext(name, ".mp3") || has_ext(name, ".m4a") ||
                has_ext(name, ".mp4")) {
                if (strlen(f->path) + 1 + strlen(name) + 1 > NFS_PATH_MAX) {
                    show_toast(f, "Path too long");
                    return true;
                }
                /* the rest of the folder (playable files, in this order) plays after it */
                player_play_folder(f->path, storage_list_entries(), f->count, (uint32_t)(i - head_rows(f)));
                ui_frame_push(ui_screen_nowplaying_create());
            } else {
                show_toast(f, "Not a playable file (WAV, FLAC, MP3, M4A)");
            }
        }
        return true;
    }
    if (btn == HAL_BTN_RIGHT && f->state == READY) { // Right on a .flac: check its MD5 without playing it
        int32_t i = ui_list_selected(f->list);
        const file_entry_t *ent = entry_at(f, i);
        if (ent != NULL && !ent->is_dir && has_ext(ent->name, ".flac")) {
            const char *name = ent->name;
            char full[NFS_PATH_MAX];
            size_t pl = strlen(f->path);
            if (pl + 1 + strlen(name) + 1 > sizeof full) {
                show_toast(f, "Path too long");
                return true;
            }
            strcpy(full, f->path);
            if (pl > 1) {
                strcat(full, "/");
            }
            strcat(full, name);
            player_verify_file(full, name);
            ui_frame_push(ui_screen_nowplaying_create());
            return true;
        }
    }
    if (btn == HAL_BTN_MENU && f->depth > 0) {
        char *slash = strrchr(f->path, '/');
        if (slash == f->path) {
            f->path[1] = '\0'; /* back to the root */
        } else if (slash != NULL) {
            *slash = '\0';
        }
        f->restore_sel = f->sel_stack[--f->depth];
        f->page = f->page_stack[f->depth];
        load(f);
        return true; /* handled: ascend; at the root Menu leaves the screen */
    }
    return false;
}

static void files_destroy(ui_screen_t *s) {
    ui_letter_overlay_destroy(((files_t *)s)->overlay);
    ui_list_destroy(((files_t *)s)->list);
}

ui_screen_t *ui_screen_files_create(void) {
    files_t *f = (files_t *)ui_screen_new(sizeof *f, "Files");
    if (f == NULL) {
        return NULL;
    }
    f->base.on_scroll = files_scroll;
    f->base.on_button = files_button;
    f->base.on_tick = files_tick;
    f->base.destroy = files_destroy;
    strcpy(f->path, "/");
    f->restore_sel = -1;
    f->plus_depth = -1;
    f->list = ui_list_create(f->base.root, UI_W, &(ui_list_model_t){.count = 1, .ctx = f, .get = files_get});
    wi_letterjump_init(&f->lj);
    f->overlay = ui_letter_overlay_create(f->base.root);
    load(f);
    return &f->base;
}
