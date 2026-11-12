// SPDX-License-Identifier: MPL-2.0
/*
 * Extras -> Files: READ-ONLY browser of the SD card. Directory reads run in the storage task
 * (storage_service.h): the UI posts a request, shows "Loading..." and keeps scrolling/animating, then
 * takes the result. Dirs first, names, sizes; Select descends, Menu ascends (at the root it leaves).
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
    char title[40];
    int32_t sel_stack[MAX_DEPTH];
    int depth;
    int32_t restore_sel; /* selection to restore after an ascend, -1 = none */
    files_state_t state;
    uint32_t seq;
    uint32_t count;
    bool truncated;
    char msg[72];
    lv_obj_t *toast; /* short notice over the last row (unsupported file type) */
    uint32_t toast_until;
    wi_letterjump_t lj;           /* letter overlay while scrolling fast (the list is sorted by name within dirs/files) */
    ui_letter_overlay_t *overlay;
} files_t;

static uint32_t g_seq;

static int32_t item_count(const files_t *f) {
    if (f->state != READY) {
        return 1; /* one status row */
    }
    return (int32_t)f->count + (f->truncated ? 1 : 0) + (f->count == 0 ? 1 : 0);
}

static void files_get(void *ctx, int32_t idx, ui_list_item_t *out) {
    const files_t *f = ctx;
    if (f->state == LOADING) {
        snprintf(out->label, sizeof out->label, "Loading...");
        out->dim = true;
    } else if (f->state == FAILED) {
        snprintf(out->label, sizeof out->label, "%s", f->msg);
        out->dim = true;
    } else if (f->count == 0) {
        snprintf(out->label, sizeof out->label, "Empty folder");
        out->dim = true;
    } else if ((uint32_t)idx >= f->count) {
        snprintf(out->label, sizeof out->label, "... more (first %u shown)", (unsigned)f->count);
        out->dim = true;
    } else {
        const file_entry_t *e = &storage_list_entries()[idx];
        snprintf(out->label, sizeof out->label, "%s %s", e->is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, e->name);
        if (e->is_dir) {
            out->chevron = true;
        } else {
            ui_format_size(e->size, out->detail, sizeof out->detail);
        }
    }
}

static void set_title(files_t *f) {
    const char *name = strrchr(f->path, '/');
    snprintf(f->title, sizeof f->title, "%s", (name == NULL || name[1] == '\0') ? "Files" : name + 1);
    f->base.title = f->title;
    ui_frame_set_title(f->title);
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
    if (!storage_list_request(f->path, f->seq)) {
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
    ui_label_set_text(f->toast, text);
    lv_obj_set_hidden(f->toast, false);
    f->toast_until = os_tick_ms() + 3000u;
}

static void files_tick(ui_screen_t *s) {
    files_t *f = (files_t *)s;
    if (f->lj.active) {
        wi_letterjump_tick(&f->lj, os_tick_ms());
        ui_letter_overlay_update(f->overlay, &f->lj);
    }
    if (f->toast_until != 0 && (int32_t)(os_tick_ms() - f->toast_until) >= 0) {
        f->toast_until = 0;
        lv_obj_set_hidden(f->toast, true);
    }
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
    if (f->state == READY && i >= 0 && (uint32_t)i < f->count) {
        wi_letterjump_scroll(&f->lj, steps, os_tick_ms(), storage_list_entries()[i].name[0]);
        ui_letter_overlay_update(f->overlay, &f->lj);
    }
}

static bool files_button(ui_screen_t *s, uint8_t btn) {
    files_t *f = (files_t *)s;
    if (btn == HAL_BTN_SELECT && f->state == READY) {
        int32_t i = ui_list_selected(f->list);
        if (i >= 0 && (uint32_t)i < f->count && storage_list_entries()[i].is_dir && f->depth < MAX_DEPTH) {
            const char *name = storage_list_entries()[i].name;
            size_t n = strlen(f->path);
            if (n + 1 + strlen(name) + 1 > sizeof f->path) {
                return true; /* path too long: stay */
            }
            f->sel_stack[f->depth++] = i;
            if (n > 1) {
                f->path[n++] = '/';
            }
            strcpy(f->path + n, name);
            f->restore_sel = -1;
            load(f);
        } else if (i >= 0 && (uint32_t)i < f->count && !storage_list_entries()[i].is_dir) {
            const char *name = storage_list_entries()[i].name;
            if (has_ext(name, ".wav") || has_ext(name, ".flac") || has_ext(name, ".mp3") || has_ext(name, ".m4a") ||
                has_ext(name, ".mp4")) {
                if (strlen(f->path) + 1 + strlen(name) + 1 > NFS_PATH_MAX) {
                    show_toast(f, "Path too long");
                    return true;
                }
                /* the rest of the folder (playable files, in this order) plays after it */
                player_play_folder(f->path, storage_list_entries(), f->count, (uint32_t)i);
                ui_frame_push(ui_screen_nowplaying_create());
            } else {
                show_toast(f, "Not a playable file (WAV, FLAC, MP3, M4A)");
            }
        }
        return true;
    }
    if (btn == HAL_BTN_RIGHT && f->state == READY) { // Right on a .flac: check its MD5 without playing it
        int32_t i = ui_list_selected(f->list);
        if (i >= 0 && (uint32_t)i < f->count && !storage_list_entries()[i].is_dir &&
            has_ext(storage_list_entries()[i].name, ".flac")) {
            const char *name = storage_list_entries()[i].name;
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
    f->list = ui_list_create(f->base.root, UI_W, &(ui_list_model_t){.count = 1, .ctx = f, .get = files_get});
    f->toast = ui_label(f->base.root, "", UI_L_TEXT);
    lv_obj_set_size(f->toast, UI_W, UI_ROW_H);
    lv_obj_set_style_bg_color(f->toast, lv_color_hex(0xFFF3CD), 0);
    lv_obj_set_style_bg_opa(f->toast, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(f->toast, UIM->toast_pad, 0);
    lv_obj_set_pos(f->toast, 0, UI_CONTENT_H - UI_ROW_H);
    lv_label_set_long_mode(f->toast, LV_LABEL_LONG_DOT);
    lv_obj_set_hidden(f->toast, true);
    wi_letterjump_init(&f->lj);
    f->overlay = ui_letter_overlay_create(f->base.root);
    load(f);
    return &f->base;
}
