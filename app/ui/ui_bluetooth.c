// SPDX-License-Identifier: MPL-2.0
/*
 * Settings > Bluetooth (docs/ui.md, docs/bluetooth.md): the radio's status, the local address and a "Scan for devices" row
 * that opens the Devices screen. That screen starts a scan when it opens and lists the devices as they are found (audio
 * devices marked with a speaker symbol, signal strength on the right). Stage 1: discovery only, selecting a device says
 * pairing is not available yet.
 *
 * Data flow: bt_service.c queues the radio's events (state, device found / named, scan done); the Devices screen polls the
 * queue on every UI pass and keeps its own table. It starts and stops a scan, both non-blocking; leaving the screen stops it.
 */
#include <stdio.h>
#include <string.h>

#include "bt_service.h"
#include "capabilities.h"
#include "hal_radio.h"
#include "os.h"
#include "ui_internal.h"

#define BT_UI_MAX 12u        /* devices shown (the radio keeps the same number) */
#define MSG_MS 3000u         /* how long a transient message replaces the status */

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

/* ---- Settings > Bluetooth: status, address, the way into the scan ---- */

#define ROW_STATUS 0
#define ROW_ADDRESS 1
#define ROW_SCAN 2
#define ROWS_BT 3

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    hal_bt_state_t shown;    /* what the rows were last built for */
    bool have_local;
    uint8_t local[6];
} bt_screen_t;

static void bt_get(void *ctx, int32_t idx, ui_list_item_t *o) {
    const bt_screen_t *b = ctx;
    hal_bt_state_t st = hal_bt_state();
    switch (idx) {
    case ROW_STATUS:
        snprintf(o->label, sizeof o->label, "Status");
        snprintf(o->detail, sizeof o->detail, "%s", state_text(st));
        return;
    case ROW_ADDRESS:
        snprintf(o->label, sizeof o->label, "Address");
        if (b->have_local) {
            fmt_addr(b->local, o->detail, sizeof o->detail);
        } else {
            snprintf(o->detail, sizeof o->detail, "-");
        }
        return;
    default:
        snprintf(o->label, sizeof o->label, "Scan for devices");
        o->dim = !radio_usable(st);
        o->chevron = true;
        return;
    }
}

static void bt_tick(ui_screen_t *s) {
    bt_screen_t *b = (bt_screen_t *)s;
    bt_service_tick();
    hal_bt_state_t st = hal_bt_state();
    bool changed = false;
    if (st != b->shown) {
        b->shown = st;
        changed = true;
    }
    if (!b->have_local && radio_usable(st) && hal_bt_local_address(b->local)) {
        b->have_local = true;
        changed = true;
    }
    if (changed) {
        ui_list_refresh(b->list);
    }
}

static void bt_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((bt_screen_t *)s)->list, steps); }

static bool bt_button(ui_screen_t *s, uint8_t btn) {
    bt_screen_t *b = (bt_screen_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    if (ui_list_selected(b->list) == ROW_SCAN && radio_usable(hal_bt_state())) {
        ui_frame_push(ui_screen_bt_devices_create());
    }
    return true;
}

static void bt_destroy(ui_screen_t *s) { ui_list_destroy(((bt_screen_t *)s)->list); }
static void bt_suspend(ui_screen_t *x) { ui_list_suspend(((bt_screen_t *)x)->list); }
static void bt_resume(ui_screen_t *x) { ui_list_resume(((bt_screen_t *)x)->list); }

ui_screen_t *ui_screen_bluetooth_create(void) {
    bt_screen_t *b = (bt_screen_t *)ui_screen_new(sizeof *b, "Bluetooth");
    if (b == NULL) {
        return NULL;
    }
    b->base.on_scroll = bt_scroll;
    b->base.on_button = bt_button;
    b->base.on_tick = bt_tick;
    b->base.destroy = bt_destroy;
    b->base.suspend = bt_suspend;
    b->base.resume = bt_resume;
    b->shown = hal_bt_state();
    b->have_local = radio_usable(b->shown) && hal_bt_local_address(b->local);
    b->list = ui_list_create(b->base.root, UI_W, &(ui_list_model_t){.count = ROWS_BT, .ctx = b, .get = bt_get});
    return &b->base;
}

/* ---- Devices: starts a scan when it opens, lists what it finds ---- */

#define DROW_SCAN 0 /* "Stop scanning" / "Scan again", with the state or the transient message on the right */
#define DROWS_FIXED 1

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    hal_bt_device_t dev[BT_UI_MAX];
    int32_t ndev;
    bool started;            /* the scan was started from this screen */
    bool scan_done;          /* ... and has finished */
    bool gave_up;            /* the radio is not usable: stop trying to start */
    hal_bt_state_t shown;
    uint32_t msg_until;      /* ms; 0 = none */
} dev_screen_t;

static int32_t dev_rows(const dev_screen_t *d) { return DROWS_FIXED + (d->ndev > 0 ? d->ndev : 1); }

static void dev_get(void *ctx, int32_t idx, ui_list_item_t *o) {
    const dev_screen_t *d = ctx;
    hal_bt_state_t st = hal_bt_state();
    if (idx == DROW_SCAN) {
        snprintf(o->label, sizeof o->label, "%s", st == HAL_BT_SCANNING ? "Stop scanning" : "Scan again");
        if (d->msg_until != 0) {
            snprintf(o->detail, sizeof o->detail, "Pairing: not yet");
        } else if (st == HAL_BT_SCANNING) {
            snprintf(o->detail, sizeof o->detail, "%d found", (int)d->ndev);
        }
        o->dim = st != HAL_BT_ON && st != HAL_BT_SCANNING;
        return;
    }
    if (d->ndev == 0) {
        o->dim = true;
        if (st == HAL_BT_STARTING) {
            snprintf(o->label, sizeof o->label, "Waiting for the radio...");
        } else if (!radio_usable(st)) {
            capabilities_t c;
            caps_get(&c);
            snprintf(o->label, sizeof o->label, "Not available: %s", c.brief[CAP_BLUETOOTH][0] != '\0' ? c.brief[CAP_BLUETOOTH] : "radio off");
        } else if (st == HAL_BT_SCANNING || !d->scan_done) {
            snprintf(o->label, sizeof o->label, "Searching...");
        } else {
            snprintf(o->label, sizeof o->label, "No devices found");
        }
        return;
    }
    const hal_bt_device_t *v = &d->dev[idx - DROWS_FIXED];
    char addr[18];
    fmt_addr(v->addr, addr, sizeof addr);
    snprintf(o->label, sizeof o->label, "%s%s", v->audio_sink ? LV_SYMBOL_AUDIO " " : "", v->name[0] != '\0' ? v->name : addr);
    o->dim = !v->audio_sink; /* not a headset or speaker: shown, but not what we are looking for */
    if (v->rssi != HAL_BT_RSSI_UNKNOWN) {
        snprintf(o->detail, sizeof o->detail, "%d dBm", (int)v->rssi);
    }
}

/* Sort once the scan is over (not while it runs, so the row under the cursor does not jump): audio devices first,
 * strongest signal first. The selection follows the device it was on. */
static void sort_devices(dev_screen_t *d) {
    int32_t sel = ui_list_selected(d->list);
    uint8_t keep[6];
    bool had = sel >= DROWS_FIXED && sel - DROWS_FIXED < d->ndev;
    if (had) {
        memcpy(keep, d->dev[sel - DROWS_FIXED].addr, 6);
    }
    for (int32_t i = 1; i < d->ndev; i++) { /* insertion sort: at most 12 */
        hal_bt_device_t t = d->dev[i];
        int32_t j = i - 1;
        while (j >= 0) {
            const hal_bt_device_t *p = &d->dev[j];
            int ra = t.rssi == HAL_BT_RSSI_UNKNOWN ? -128 : t.rssi;
            int rb = p->rssi == HAL_BT_RSSI_UNKNOWN ? -128 : p->rssi;
            bool before = (t.audio_sink != p->audio_sink) ? t.audio_sink : ra > rb;
            if (!before) {
                break;
            }
            d->dev[j + 1] = d->dev[j];
            j--;
        }
        d->dev[j + 1] = t;
    }
    if (had) {
        for (int32_t i = 0; i < d->ndev; i++) {
            if (memcmp(d->dev[i].addr, keep, 6) == 0) {
                ui_list_set_selected(d->list, DROWS_FIXED + i);
            }
        }
    }
}

static void add_or_update(dev_screen_t *d, const hal_bt_device_t *v) {
    for (int32_t i = 0; i < d->ndev; i++) {
        if (memcmp(d->dev[i].addr, v->addr, 6) == 0) {
            d->dev[i] = *v; /* the caller has already kept a known name when the update has none */
            return;
        }
    }
    if ((uint32_t)d->ndev < BT_UI_MAX) {
        d->dev[d->ndev++] = *v;
    }
}

/* Begin (or restart) a scan: the table starts empty. The radio may still be finishing an earlier stop. */
static void dev_scan_begin(dev_screen_t *d) {
    bt_service_event_flush();
    if (hal_bt_inquiry_start(BT_SCAN_SECONDS) == HAL_OK) {
        d->ndev = 0;
        d->started = true;
        d->scan_done = false;
    }
}

static void dev_tick(ui_screen_t *s) {
    dev_screen_t *d = (dev_screen_t *)s;
    bt_service_tick();
    hal_bt_state_t st = hal_bt_state();
    bool changed = false;
    if (!d->started && !d->gave_up) {
        if (st == HAL_BT_ON) {
            dev_scan_begin(d);
            changed = true;
        } else if (st == HAL_BT_OFF || st == HAL_BT_FAILED) {
            d->gave_up = true;
            changed = true;
        } /* STARTING, or SCANNING (an earlier stop is not finished yet): try again on the next pass */
    }
    hal_bt_event_t ev;
    while (bt_service_event_poll(&ev)) {
        if (!d->started) {
            continue;
        }
        if (ev.type == HAL_BT_EVT_DEVICE) {
            /* a name that arrives later for a device we have: keep the earlier name if the update has none */
            hal_bt_device_t v = ev.dev;
            for (int32_t i = 0; i < d->ndev; i++) {
                if (memcmp(d->dev[i].addr, v.addr, 6) == 0 && v.name[0] == '\0') {
                    memcpy(v.name, d->dev[i].name, sizeof v.name);
                }
            }
            add_or_update(d, &v);
            changed = true;
        } else if (ev.type == HAL_BT_EVT_SCAN_DONE) {
            d->scan_done = true;
            sort_devices(d);
            changed = true;
        }
    }
    if (st != d->shown) {
        d->shown = st;
        changed = true;
    }
    if (d->msg_until != 0 && (int32_t)(os_tick_ms() - d->msg_until) >= 0) {
        d->msg_until = 0;
        changed = true;
    }
    if (changed) {
        ui_list_set_count(d->list, dev_rows(d));
    }
}

static void dev_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((dev_screen_t *)s)->list, steps); }

static bool dev_button(ui_screen_t *s, uint8_t btn) {
    dev_screen_t *d = (dev_screen_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    int32_t sel = ui_list_selected(d->list);
    hal_bt_state_t st = hal_bt_state();
    if (sel == DROW_SCAN) {
        if (st == HAL_BT_SCANNING) {
            hal_bt_inquiry_stop();
        } else if (st == HAL_BT_ON) {
            dev_scan_begin(d);
            ui_list_set_count(d->list, dev_rows(d));
        }
    } else if (d->ndev > 0) {
        d->msg_until = os_tick_ms() + MSG_MS; /* selecting a device does nothing yet */
        ui_list_refresh(d->list);
    }
    return true;
}

static void dev_destroy(ui_screen_t *s) {
    if (hal_bt_state() == HAL_BT_SCANNING) {
        hal_bt_inquiry_stop(); /* do not leave the radio scanning for nobody */
    }
    ui_list_destroy(((dev_screen_t *)s)->list);
}

static void dev_suspend(ui_screen_t *x) { ui_list_suspend(((dev_screen_t *)x)->list); }
static void dev_resume(ui_screen_t *x) { ui_list_resume(((dev_screen_t *)x)->list); }

ui_screen_t *ui_screen_bt_devices_create(void) {
    dev_screen_t *d = (dev_screen_t *)ui_screen_new(sizeof *d, "Devices");
    if (d == NULL) {
        return NULL;
    }
    d->base.on_scroll = dev_scroll;
    d->base.on_button = dev_button;
    d->base.on_tick = dev_tick;
    d->base.destroy = dev_destroy;
    d->base.suspend = dev_suspend;
    d->base.resume = dev_resume;
    d->shown = hal_bt_state();
    d->list = ui_list_create(d->base.root, UI_W, &(ui_list_model_t){.count = dev_rows(d), .ctx = d, .get = dev_get});
    return &d->base;
}
