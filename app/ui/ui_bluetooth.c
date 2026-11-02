// SPDX-License-Identifier: MPL-2.0
/*
 * Settings > Bluetooth (docs/ui.md, docs/bluetooth.md): the radio's status, the local address, a Scan action and the
 * devices it finds (audio devices marked with a speaker symbol, signal strength on the right). Stage 1: discovery
 * only, selecting a device says pairing is not available yet.
 *
 * Data flow: bt_service.c queues the radio's events (state, device found / named, scan done); this screen polls the queue
 * on every UI pass and keeps its own table. It starts and stops a scan, both non-blocking; leaving the screen stops it.
 */
#include <stdio.h>
#include <string.h>

#include "bt_service.h"
#include "capabilities.h"
#include "hal_radio.h"
#include "os.h"
#include "ui_internal.h"

#define BT_UI_MAX 12u        /* devices shown (the radio keeps the same number) */
#define ROW_STATUS 0
#define ROW_ADDRESS 1
#define ROW_SCAN 2
#define ROWS_FIXED 3
#define MSG_MS 3000u         /* how long a transient message replaces the status */

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    hal_bt_device_t dev[BT_UI_MAX];
    int32_t ndev;
    bool scanned;            /* a scan was started on this screen */
    bool scan_done;          /* ... and has finished */
    hal_bt_state_t shown;    /* what the rows were last built for */
    bool have_local;
    uint8_t local[6];
    uint32_t msg_until;      /* ms; 0 = none */
    bool dirty;
} bt_screen_t;

static void fmt_addr(const uint8_t a[6], char *out, size_t n) {
    snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

static bool radio_usable(hal_bt_state_t s) { return s == HAL_BT_ON || s == HAL_BT_SCANNING; }

static const char *state_text(hal_bt_state_t s) {
    switch (s) {
    case HAL_BT_OFF:
        return caps_available(CAP_BLUETOOTH) ? "Off" : "Unavailable";
    case HAL_BT_STARTING: return "Starting...";
    case HAL_BT_ON: return "On";
    case HAL_BT_SCANNING: return "Scanning...";
    case HAL_BT_FAILED: return "Failed";
    }
    return "?";
}

static int32_t row_count(const bt_screen_t *b) {
    int32_t extra = b->ndev > 0 ? b->ndev : 1; /* the single information row while there is nothing to list */
    return ROWS_FIXED + extra;
}

static void bt_get(void *ctx, int32_t idx, ui_list_item_t *o) {
    const bt_screen_t *b = ctx;
    hal_bt_state_t st = hal_bt_state();
    switch (idx) {
    case ROW_STATUS:
        snprintf(o->label, sizeof o->label, "Status");
        snprintf(o->detail, sizeof o->detail, "%s", b->msg_until != 0 ? "Pairing: not yet" : state_text(st));
        return;
    case ROW_ADDRESS:
        snprintf(o->label, sizeof o->label, "Address");
        if (b->have_local) {
            fmt_addr(b->local, o->detail, sizeof o->detail);
        } else {
            snprintf(o->detail, sizeof o->detail, "-");
        }
        return;
    case ROW_SCAN:
        if (st == HAL_BT_SCANNING) {
            snprintf(o->label, sizeof o->label, "Stop scanning");
        } else {
            snprintf(o->label, sizeof o->label, "Scan for devices");
            o->dim = !radio_usable(st);
        }
        return;
    default:
        break;
    }
    if (b->ndev == 0) {
        o->dim = true;
        if (st == HAL_BT_STARTING) {
            snprintf(o->label, sizeof o->label, "Waiting for the radio...");
        } else if (!radio_usable(st)) {
            capabilities_t c;
            caps_get(&c);
            snprintf(o->label, sizeof o->label, "Not available: %s", c.brief[CAP_BLUETOOTH][0] != '\0' ? c.brief[CAP_BLUETOOTH] : "radio off");
        } else if (st == HAL_BT_SCANNING) {
            snprintf(o->label, sizeof o->label, "Searching...");
        } else if (b->scan_done) {
            snprintf(o->label, sizeof o->label, "No devices found");
        } else {
            snprintf(o->label, sizeof o->label, "Select Scan to look for headphones");
        }
        return;
    }
    const hal_bt_device_t *d = &b->dev[idx - ROWS_FIXED];
    char addr[18];
    fmt_addr(d->addr, addr, sizeof addr);
    snprintf(o->label, sizeof o->label, "%s%s", d->audio_sink ? LV_SYMBOL_AUDIO " " : "", d->name[0] != '\0' ? d->name : addr);
    o->dim = !d->audio_sink; /* not a headset or speaker: shown, but not what we are looking for */
    if (d->rssi != HAL_BT_RSSI_UNKNOWN) {
        snprintf(o->detail, sizeof o->detail, "%d dBm", (int)d->rssi);
    }
}

/* Sort once the scan is over (not while it runs, so the row under the cursor does not jump): audio devices first,
 * strongest signal first. The selection follows the device it was on. */
static void sort_devices(bt_screen_t *b) {
    int32_t sel = ui_list_selected(b->list);
    uint8_t keep[6];
    bool had = sel >= ROWS_FIXED && sel - ROWS_FIXED < b->ndev;
    if (had) {
        memcpy(keep, b->dev[sel - ROWS_FIXED].addr, 6);
    }
    for (int32_t i = 1; i < b->ndev; i++) { /* insertion sort: at most 12 */
        hal_bt_device_t t = b->dev[i];
        int32_t j = i - 1;
        while (j >= 0) {
            const hal_bt_device_t *p = &b->dev[j];
            int ra = t.rssi == HAL_BT_RSSI_UNKNOWN ? -128 : t.rssi;
            int rb = p->rssi == HAL_BT_RSSI_UNKNOWN ? -128 : p->rssi;
            bool before = (t.audio_sink != p->audio_sink) ? t.audio_sink : ra > rb;
            if (!before) {
                break;
            }
            b->dev[j + 1] = b->dev[j];
            j--;
        }
        b->dev[j + 1] = t;
    }
    if (had) {
        for (int32_t i = 0; i < b->ndev; i++) {
            if (memcmp(b->dev[i].addr, keep, 6) == 0) {
                ui_list_set_selected(b->list, ROWS_FIXED + i);
            }
        }
    }
}

static void add_or_update(bt_screen_t *b, const hal_bt_device_t *d) {
    for (int32_t i = 0; i < b->ndev; i++) {
        if (memcmp(b->dev[i].addr, d->addr, 6) == 0) {
            b->dev[i] = *d; /* the caller has already kept a known name when the update has none */
            return;
        }
    }
    if ((uint32_t)b->ndev < BT_UI_MAX) {
        b->dev[b->ndev++] = *d;
    }
}

static void bt_tick(ui_screen_t *s) {
    bt_screen_t *b = (bt_screen_t *)s;
    bt_service_tick();
    hal_bt_event_t ev;
    bool changed = false;
    while (bt_service_event_poll(&ev)) {
        if (ev.type == HAL_BT_EVT_DEVICE && b->scanned) {
            /* a name that arrives later for a device we have: keep the earlier name if the update has none */
            hal_bt_device_t d = ev.dev;
            for (int32_t i = 0; i < b->ndev; i++) {
                if (memcmp(b->dev[i].addr, d.addr, 6) == 0 && d.name[0] == '\0') {
                    memcpy(d.name, b->dev[i].name, sizeof d.name);
                }
            }
            add_or_update(b, &d);
            changed = true;
        } else if (ev.type == HAL_BT_EVT_SCAN_DONE && b->scanned) {
            b->scan_done = true;
            sort_devices(b);
            changed = true;
        }
    }
    hal_bt_state_t st = hal_bt_state();
    if (st != b->shown) {
        b->shown = st;
        changed = true;
    }
    if (!b->have_local && radio_usable(st) && hal_bt_local_address(b->local)) {
        b->have_local = true;
        changed = true;
    }
    if (b->msg_until != 0 && (int32_t)(os_tick_ms() - b->msg_until) >= 0) {
        b->msg_until = 0;
        changed = true;
    }
    if (changed || b->dirty) {
        b->dirty = false;
        ui_list_set_count(b->list, row_count(b));
    }
}

static void bt_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((bt_screen_t *)s)->list, steps); }

static bool bt_button(ui_screen_t *s, uint8_t btn) {
    bt_screen_t *b = (bt_screen_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    int32_t sel = ui_list_selected(b->list);
    hal_bt_state_t st = hal_bt_state();
    if (sel == ROW_SCAN) {
        if (st == HAL_BT_SCANNING) {
            hal_bt_inquiry_stop();
        } else if (st == HAL_BT_ON) {
            bt_service_event_flush();
            b->ndev = 0;
            b->scanned = true;
            b->scan_done = false;
            if (hal_bt_inquiry_start(BT_SCAN_SECONDS) != HAL_OK) {
                b->scanned = false;
            }
            b->dirty = true;
        }
    } else if (sel >= ROWS_FIXED && b->ndev > 0) {
        b->msg_until = os_tick_ms() + MSG_MS; /* selecting a device does nothing yet */
        b->dirty = true;
    }
    return true;
}

static void bt_destroy(ui_screen_t *s) {
    if (hal_bt_state() == HAL_BT_SCANNING) {
        hal_bt_inquiry_stop(); /* do not leave the radio scanning for nobody */
    }
    ui_list_destroy(((bt_screen_t *)s)->list);
}

ui_screen_t *ui_screen_bluetooth_create(void) {
    bt_screen_t *b = (bt_screen_t *)ui_screen_new(sizeof *b, "Bluetooth");
    if (b == NULL) {
        return NULL;
    }
    b->base.on_scroll = bt_scroll;
    b->base.on_button = bt_button;
    b->base.on_tick = bt_tick;
    b->base.destroy = bt_destroy;
    bt_service_event_flush();
    b->shown = hal_bt_state();
    b->have_local = radio_usable(b->shown) && hal_bt_local_address(b->local);
    b->list = ui_list_create(b->base.root, UI_W, &(ui_list_model_t){.count = ROWS_FIXED + 1, .ctx = b, .get = bt_get});
    return &b->base;
}
