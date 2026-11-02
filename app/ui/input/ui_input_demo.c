// SPDX-License-Identifier: MPL-2.0
// Extras > Input widgets: one entry per wheel input widget (docs/ui-input.md). A bring-up/demo screen; the last result is shown on the first row.
#include <stdio.h>
#include <string.h>

#include "ui_input.h"
#include "ui_internal.h"

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    char last[32];
    uint32_t live;
} demo_t;

static const char *const k_names[] = {"Text", "Password", "Search (live)", "Number (dB)", "Slider (level)", "Choice", "Multi choice", "Date and time"};
static const char *const k_items[] = {"Off", "On", "Auto", "Only at night", "When charging"};
#define N_DEMOS ((int32_t)(sizeof k_names / sizeof k_names[0]))

static void get(void *ctx, int32_t i, ui_list_item_t *o) {
    const demo_t *d = ctx;
    if (i == 0) {
        snprintf(o->label, sizeof o->label, "Last result");
        snprintf(o->detail, sizeof o->detail, "%s", d->last[0] ? d->last : "-");
        o->dim = true;
        return;
    }
    snprintf(o->label, sizeof o->label, "%s", k_names[i - 1]);
    o->chevron = true;
}

static demo_t *g_demo; /* the demo screen outlives its modals (the modal is pushed on top of it) */

static void done(void *ctx, bool ok, const wi_value_t *v) {
    (void)ctx;
    if (g_demo == NULL) {
        return;
    }
    if (!ok) {
        snprintf(g_demo->last, sizeof g_demo->last, "cancelled");
    } else {
        snprintf(g_demo->last, sizeof g_demo->last, "%.14s %d/%u/%X", v->text, (int)v->number, (unsigned)v->pick, (unsigned)v->mask);
    }
    ui_list_refresh(g_demo->list);
}

static void live(void *ctx, const wi_value_t *v) {
    (void)ctx;
    if (g_demo != NULL) {
        g_demo->live++;
        snprintf(g_demo->last, sizeof g_demo->last, "live: %.18s %d", v->text, (int)v->number);
    }
}

static bool button(ui_screen_t *s, uint8_t btn) {
    demo_t *d = (demo_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    wi_request_t r = {.done = done, .change = live, .title = "Input"};
    switch (ui_list_selected(d->list)) {
    case 1: r.kind = WI_K_TEXT; r.title = "Text"; r.initial = "neu"; r.max_chars = 24; break;
    case 2: r.kind = WI_K_TEXT; r.title = "Password"; r.max_chars = 63; r.masked = true; break;
    case 3: r.kind = WI_K_TEXT; r.title = "Search"; r.max_chars = 24; r.search = true; break;
    case 4: r.kind = WI_K_NUMBER; r.title = "Gain"; r.value = -60; r.min = -240; r.max = 0; r.step = 5; r.decimals = 1; r.unit = "dB"; break;
    case 5: r.kind = WI_K_SLIDER; r.title = "Level"; r.value = 40; r.min = 0; r.max = 100; r.step = 5; r.unit = "%"; break;
    case 6: r.kind = WI_K_PICKER; r.title = "Choose"; r.items = k_items; r.count = 5; r.sel = 1; break;
    case 7: r.kind = WI_K_PICKER; r.title = "Choose several"; r.items = k_items; r.count = 5; r.multi = true; break;
    case 8: r.kind = WI_K_DATETIME; r.title = "Set the clock"; r.has_date = r.has_time = true; r.y = 2026; r.mo = 11; r.d = 2; r.h = 21; r.mi = 30; break;
    default: return true;
    }
    ui_input_open(&r);
    return true;
}

static void scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((demo_t *)s)->list, steps); }
static void destroy(ui_screen_t *s) {
    g_demo = NULL;
    ui_list_destroy(((demo_t *)s)->list);
}

static void list_suspend(ui_screen_t *x) { ui_list_suspend(((demo_t *)x)->list); }
static void list_resume(ui_screen_t *x) { ui_list_resume(((demo_t *)x)->list); }

ui_screen_t *ui_screen_inputdemo_create(void) {
    demo_t *d = (demo_t *)ui_screen_new(sizeof *d, "Input widgets");
    if (d == NULL) {
        return NULL;
    }
    d->base.on_scroll = scroll;
    d->base.on_button = button;
    d->base.destroy = destroy;
    d->base.suspend = list_suspend;
    d->base.resume = list_resume;
    g_demo = d;
    d->list = ui_list_create(d->base.root, UI_W, &(ui_list_model_t){.count = N_DEMOS + 1, .ctx = d, .get = get});
    return &d->base;
}
