// SPDX-License-Identifier: MPL-2.0
// Extras > Network test: HTTP GET of the URL in settings, body discarded, throughput reported. Audio keeps running while it
// transfers (the point of the test: watch the audio line on the UART for underruns). The SD-file variant is not built yet.
#include <stdio.h>
#include <string.h>

#include "ui_input.h"
#include "ui_internal.h"
#include "wifi_service.h"

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    wifi_test_t t;
    uint32_t last_ms;
} nt_t;

static void get(void *ctx, int32_t i, ui_list_item_t *o) {
    const nt_t *n = ctx;
    char url[96];
    switch (i) {
    case 0:
        wifi_test_get_url(url, sizeof url);
        snprintf(o->label, sizeof o->label, "URL");
        snprintf(o->detail, sizeof o->detail, "%.31s", strncmp(url, "http://", 7) == 0 ? url + 7 : url);
        break;
    case 1:
        snprintf(o->label, sizeof o->label, "%s", n->t.state == WT_RUNNING ? "Running..." : "Run test");
        break;
    case 2:
        snprintf(o->label, sizeof o->label, "Result");
        snprintf(o->detail, sizeof o->detail, "%s", n->t.state == WT_IDLE ? "-" : n->t.state == WT_RUNNING ? "transferring" : n->t.state == WT_DONE ? "done" : "failed");
        break;
    case 3:
        snprintf(o->label, sizeof o->label, "Speed");
        snprintf(o->detail, sizeof o->detail, "%u KB/s", (unsigned)n->t.kbps);
        break;
    case 4:
        snprintf(o->label, sizeof o->label, "Received");
        snprintf(o->detail, sizeof o->detail, "%u KB in %u ms", (unsigned)(n->t.bytes / 1024u), (unsigned)n->t.ms);
        break;
    default:
        snprintf(o->label, sizeof o->label, "Error code");
        snprintf(o->detail, sizeof o->detail, "%d", n->t.err);
        o->dim = true;
    }
}

static void url_done(void *ctx, bool ok, const wi_value_t *v) {
    (void)ctx;
    if (ok && v->text[0] != '\0') {
        wifi_test_set_url(v->text);
    }
}

static bool button(ui_screen_t *s, uint8_t btn) {
    nt_t *n = (nt_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    int32_t sel = ui_list_selected(n->list);
    if (sel == 0) {
        char url[96];
        wifi_test_get_url(url, sizeof url);
        ui_input_open(&(wi_request_t){.kind = WI_K_TEXT, .title = "Test URL", .initial = url, .max_chars = 63, .done = url_done});
    } else if (sel == 1 && n->t.state != WT_RUNNING) {
        n->t.state = WT_RUNNING;
        (void)wifi_test_start();
    }
    return true;
}

static void tick(ui_screen_t *s) {
    nt_t *n = (nt_t *)s;
    uint32_t now = os_tick_ms();
    if (now - n->last_ms >= 500u) {
        n->last_ms = now;
        wifi_test_t t;
        wifi_test_get(&t);
        if (n->t.state != WT_RUNNING || t.state != WT_IDLE) {
            n->t = t;
        }
        ui_list_refresh(n->list);
    }
}

static void scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((nt_t *)s)->list, steps); }
static void destroy(ui_screen_t *s) { ui_list_destroy(((nt_t *)s)->list); }
static void susp(ui_screen_t *x) { ui_list_suspend(((nt_t *)x)->list); }
static void resm(ui_screen_t *x) { ui_list_resume(((nt_t *)x)->list); }

ui_screen_t *ui_screen_nettest_create(void) {
    nt_t *n = (nt_t *)ui_screen_new(sizeof *n, "Network test");
    if (n == NULL) {
        return NULL;
    }
    n->base.on_scroll = scroll;
    n->base.on_button = button;
    n->base.on_tick = tick;
    n->base.destroy = destroy;
    n->base.suspend = susp;
    n->base.resume = resm;
    wifi_test_get(&n->t);
    n->list = ui_list_create(n->base.root, UI_W, &(ui_list_model_t){.count = 6, .ctx = n, .get = get});
    return &n->base;
}
