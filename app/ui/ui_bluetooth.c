// SPDX-License-Identifier: MPL-2.0
/*
 * Settings > Bluetooth (docs/ui.md, docs/bluetooth.md): the radio's status, the local address, a "Scan for devices" row
 * that opens the Devices screen and, once a headset is remembered (bt_saved.h), three rows for it: its name with the link state,
 * Connect / Disconnect / Cancel, and Forget (asks twice). The Devices screen starts a scan when it opens and lists the devices
 * as they are found (audio devices marked with a speaker symbol, signal strength on the right); Select on an audio device
 * stops the scan, starts the Pair action (bt_service_pair) and shows a busy indicator on that row until the link is CONNECTED
 * (the device is then remembered) or FAILED (the row says why).
 *
 * Data flow: bt_service.c queues the radio's scan events (state, device found / named, scan done); the Devices screen polls the
 * queue on every UI pass and keeps its own table. Link changes are not queued: the screens read hal_bt_link_state() on their
 * tick. Starting and stopping a scan or a link are non-blocking; leaving the Devices screen stops the scan and cancels a Pair.
 */
#include <stdio.h>
#include <string.h>

#include "audio_route.h"
#include "bt_saved.h"
#include "bt_service.h"
#include "capabilities.h"
#include "hal_radio.h"
#include "os.h"
#include "ui_internal.h"
#include "ui_logic.h"

#define BT_UI_MAX 12u        /* devices shown (the radio keeps the same number) */
#define MSG_MS 3000u         /* how long a transient message replaces the status */
#define FORGET_ARM_MS 4000u  /* Forget asks for a second Select within this time */

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

static bool link_busy(hal_bt_link_state_t l) { return l == HAL_BT_LINK_PAIRING || l == HAL_BT_LINK_CONNECTING; }

/* Does the link (its current attempt, or the last one that failed) concern the device with address @p addr? */
static bool link_is_for(const uint8_t addr[6]) {
    hal_bt_device_t peer;
    return hal_bt_link_state() != HAL_BT_LINK_DISCONNECTED && hal_bt_link_device(&peer) && memcmp(peer.addr, addr, 6) == 0;
}

/* The audio stream on a CONNECTED link as one phrase: what the headset got ("Connected - SBC"), "Streaming", or why it did not open. */
static void stream_text(char *out, size_t n) {
    hal_bt_codec_t c;
    switch (hal_bt_stream_state()) {
    case HAL_BT_STREAM_STREAMING: snprintf(out, n, "Streaming"); break;
    case HAL_BT_STREAM_OPEN:
    case HAL_BT_STREAM_SUSPENDED:
        if (hal_bt_stream_codec(&c)) {
            snprintf(out, n, "Connected - %s", c.codec == HAL_BT_CODEC_SBC_XQ ? "SBC-XQ" : "SBC");
        } else {
            snprintf(out, n, "Connected");
        }
        break;
    case HAL_BT_STREAM_OPENING: snprintf(out, n, "Connecting audio..."); break;
    case HAL_BT_STREAM_FAILED: snprintf(out, n, "Failed: %s", hal_bt_fail_text(hal_bt_stream_reason())); break;
    case HAL_BT_STREAM_CLOSED: snprintf(out, n, "Connected"); break;
    }
}

void ui_bt_stream_pane_text(char *out, size_t n) {
    hal_bt_codec_t c;
    hal_bt_stats_t st;
    out[0] = '\0';
    hal_bt_stream_state_t s = hal_bt_stream_state();
    if ((s == HAL_BT_STREAM_OPEN || s == HAL_BT_STREAM_STREAMING || s == HAL_BT_STREAM_SUSPENDED) && hal_bt_stream_codec(&c)) {
        hal_bt_stats(&st);
        snprintf(out, n, "%s bitpool %u\n%u kbit/s\n%u underruns\nAbsolute volume: %s", c.codec == HAL_BT_CODEC_SBC_XQ ? "SBC-XQ" : "SBC", (unsigned)c.bitpool,
                 (unsigned)c.kbps, (unsigned)st.underruns, hal_bt_volume_supported() ? "yes" : "no");
    }
}

/* The link state as one phrase for that device; "Not connected" when the link is about something else. */
static void link_text_for(const uint8_t addr[6], char *out, size_t n) {
    hal_bt_link_state_t l = hal_bt_link_state();
    if (!link_is_for(addr)) {
        snprintf(out, n, "%s", bt_service_reconnecting() ? "Reconnecting..." : "Not connected");
        return;
    }
    switch (l) {
    case HAL_BT_LINK_PAIRING: snprintf(out, n, "Pairing..."); break;
    case HAL_BT_LINK_CONNECTING: snprintf(out, n, "%s", bt_service_reconnecting() ? "Reconnecting..." : "Connecting..."); break;
    case HAL_BT_LINK_CONNECTED:
        if (bt_saved_notice() == BT_SAVED_NOTICE_NOT_SAVED) {
            snprintf(out, n, "Paired - not saved (read-only)");
        } else {
            stream_text(out, n);
        }
        break;
    case HAL_BT_LINK_FAILED:
        if (bt_service_reconnecting()) {
            snprintf(out, n, "Reconnecting..."); /* the next attempt is on its way (bt_reconnect.h) */
        } else {
            snprintf(out, n, "Failed: %s", hal_bt_fail_text(hal_bt_link_reason()));
        }
        break;
    case HAL_BT_LINK_DISCONNECTED: snprintf(out, n, "%s", bt_service_reconnecting() ? "Reconnecting..." : "Not connected"); break;
    }
}

void ui_bt_headset_text(char *out, size_t n) {
    hal_bt_device_t sv;
    if (!bt_saved_get(&sv)) {
        out[0] = '\0';
        return;
    }
    char st[32];
    link_text_for(sv.addr, st, sizeof st);
    char nm[HAL_BT_NAME_MAX + 8];
    ui_display_name(nm, sizeof nm, sv.name);
    snprintf(out, n, "%s\n%s", nm[0] != '\0' ? nm : "Headset", st);
}

/* ---- Settings > Bluetooth: status, address, the way into the scan, the remembered headset ---- */

#define ROW_STATUS 0
#define ROW_ADDRESS 1
#define ROW_SCAN 2
#define ROW_OUTPUT 3       /* Audio output: Auto / Wired (saved) */
#define ROWS_BT 4          /* ... plus these three while a headset is remembered: */
#define ROW_DEVICE 4       /* its name, with the link state on the right */
#define ROW_ACTION 5       /* Connect / Disconnect / Cancel */
#define ROW_FORGET 6
#define ROWS_BT_SAVED 7

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    hal_bt_state_t shown;    /* what the rows were last built for */
    bool have_local;
    uint8_t local[6];
    bool have_saved;
    hal_bt_device_t saved;
    uint32_t sig;            /* link state / reason / which device, as last shown */
    uint32_t forget_until;   /* ms; 0 = not asked yet */
} bt_screen_t;

static uint32_t link_sig(const uint8_t saved_addr[6]) {
    return (uint32_t)hal_bt_link_state() | (uint32_t)hal_bt_link_reason() << 4 | (link_is_for(saved_addr) ? 1u << 8 : 0u) |
           (uint32_t)hal_bt_stream_state() << 9 | (uint32_t)hal_bt_stream_reason() << 12 | (uint32_t)audio_route_pref() << 17 |
           (uint32_t)audio_route_output() << 18;
}

static int32_t bt_rows(const bt_screen_t *b) { return b->have_saved ? ROWS_BT_SAVED : ROWS_BT; }

static void bt_get(void *ctx, int32_t idx, ui_list_item_t *o) {
    const bt_screen_t *b = ctx;
    hal_bt_state_t st = hal_bt_state();
    switch (idx) {
    case ROW_STATUS:
        snprintf(o->label, sizeof o->label, "Status");
        snprintf(o->detail, sizeof o->detail, "%s", state_text(st));
        o->busy = st == HAL_BT_STARTING;
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
        snprintf(o->label, sizeof o->label, "Scan for devices");
        o->dim = !radio_usable(st);
        o->chevron = true;
        return;
    case ROW_OUTPUT:
        snprintf(o->label, sizeof o->label, "Audio output");
        snprintf(o->detail, sizeof o->detail, "%s", audio_route_pref() == AUDIO_ROUTE_WIRED ? "Wired" : audio_route_output() == AUDIO_OUT_BT ? "Auto: Bluetooth" : "Auto");
        o->chevron = false;
        return;
    case ROW_DEVICE: {
        hal_bt_link_state_t l = hal_bt_link_state();
        char nm[HAL_BT_NAME_MAX + 8];
        ui_display_name(nm, sizeof nm, b->saved.name);
        snprintf(o->label, sizeof o->label, "%s%s", b->saved.audio_sink ? LV_SYMBOL_AUDIO " " : "", nm[0] != '\0' ? nm : "Headset");
        link_text_for(b->saved.addr, o->detail, sizeof o->detail);
        o->busy = (link_busy(l) && link_is_for(b->saved.addr)) || (bt_service_reconnecting() && l != HAL_BT_LINK_CONNECTED);
        return;
    }
    case ROW_ACTION: {
        hal_bt_link_state_t l = hal_bt_link_state();
        bool mine = link_is_for(b->saved.addr) && l != HAL_BT_LINK_FAILED;
        if (mine && link_busy(l)) {
            snprintf(o->label, sizeof o->label, "Cancel");
        } else if (mine && l == HAL_BT_LINK_CONNECTED) {
            snprintf(o->label, sizeof o->label, "Disconnect");
        } else {
            snprintf(o->label, sizeof o->label, "Connect");
            o->dim = st != HAL_BT_ON || link_busy(l) || l == HAL_BT_LINK_CONNECTED; /* not while scanning, or while another link exists */
        }
        return;
    }
    default:
        snprintf(o->label, sizeof o->label, "Forget");
        if (b->forget_until != 0) {
            snprintf(o->detail, sizeof o->detail, "Select again");
        }
        return;
    }
}

static void bt_tick(ui_screen_t *s) {
    bt_screen_t *b = (bt_screen_t *)s;
    hal_bt_state_t st = hal_bt_state();
    bool changed = false;
    bool rows_changed = false;
    hal_bt_device_t sv;
    bool have = bt_saved_get(&sv);
    if (have != b->have_saved || (have && memcmp(&sv, &b->saved, sizeof sv) != 0)) {
        b->have_saved = have;
        if (have) {
            b->saved = sv;
        }
        rows_changed = true;
    }
    uint32_t sig = link_sig(b->saved.addr);
    if (sig != b->sig) {
        b->sig = sig;
        changed = true;
    }
    if (b->forget_until != 0 && (int32_t)(os_tick_ms() - b->forget_until) >= 0) {
        b->forget_until = 0;
        changed = true;
    }
    if (st != b->shown) {
        b->shown = st;
        changed = true;
    }
    if (!b->have_local && radio_usable(st) && hal_bt_local_address(b->local)) {
        b->have_local = true;
        changed = true;
    }
    if (rows_changed) {
        b->forget_until = 0;
        ui_list_set_count(b->list, bt_rows(b));
        if (ui_list_selected(b->list) >= bt_rows(b)) {
            ui_list_set_selected(b->list, bt_rows(b) - 1);
        }
    } else if (changed) {
        ui_list_refresh(b->list);
    }
}

static void bt_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((bt_screen_t *)s)->list, steps); }

static bool bt_button(ui_screen_t *s, uint8_t btn) {
    bt_screen_t *b = (bt_screen_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    int32_t sel = ui_list_selected(b->list);
    if (sel != ROW_FORGET && b->forget_until != 0) {
        b->forget_until = 0; /* moved on: the confirmation lapses */
        ui_list_refresh(b->list);
    }
    if (sel == ROW_SCAN && radio_usable(hal_bt_state())) {
        ui_frame_push(ui_screen_bt_devices_create());
    } else if (sel == ROW_OUTPUT) {
        audio_route_set_pref(audio_route_pref() == AUDIO_ROUTE_WIRED ? AUDIO_ROUTE_AUTO : AUDIO_ROUTE_WIRED);
        ui_list_refresh(b->list);
    } else if (sel == ROW_ACTION && b->have_saved) {
        hal_bt_link_state_t l = hal_bt_link_state();
        bool mine = link_is_for(b->saved.addr) && l != HAL_BT_LINK_FAILED;
        if (mine && (link_busy(l) || l == HAL_BT_LINK_CONNECTED)) {
            bt_service_disconnect();
        } else if (hal_bt_state() == HAL_BT_ON && !link_busy(l) && l != HAL_BT_LINK_CONNECTED) {
            (void)bt_service_connect();
        }
        ui_list_refresh(b->list);
    } else if (sel == ROW_FORGET && b->have_saved) {
        if (b->forget_until == 0) {
            b->forget_until = os_tick_ms() + FORGET_ARM_MS;
        } else {
            b->forget_until = 0;
            bt_service_forget();
        }
        ui_list_refresh(b->list);
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
    b->have_saved = bt_saved_get(&b->saved);
    b->sig = link_sig(b->saved.addr);
    b->list = ui_list_create(b->base.root, UI_W, &(ui_list_model_t){.count = bt_rows(b), .ctx = b, .get = bt_get});
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
    char msg[24];            /* the transient message while msg_until is set */
    bool pair_wanted;        /* Select on a device while the scan was still running: pair as soon as it has stopped */
    hal_bt_device_t pair_dev;
    uint32_t sig;            /* link state / reason, as last shown */
} dev_screen_t;

static void dev_say(dev_screen_t *d, const char *text) {
    snprintf(d->msg, sizeof d->msg, "%s", text);
    d->msg_until = os_tick_ms() + MSG_MS;
}

static int32_t dev_rows(const dev_screen_t *d) { return DROWS_FIXED + (d->ndev > 0 ? d->ndev : 1); }

static void dev_get(void *ctx, int32_t idx, ui_list_item_t *o) {
    const dev_screen_t *d = ctx;
    hal_bt_state_t st = hal_bt_state();
    if (idx == DROW_SCAN) {
        hal_bt_link_state_t l = hal_bt_link_state();
        if (link_busy(l)) {
            snprintf(o->label, sizeof o->label, "%s", l == HAL_BT_LINK_PAIRING ? "Cancel pairing" : "Cancel");
            return;
        }
        snprintf(o->label, sizeof o->label, "%s", st == HAL_BT_SCANNING ? "Stop scanning" : "Scan again");
        if (d->msg_until != 0) {
            snprintf(o->detail, sizeof o->detail, "%s", d->msg);
        } else if (bt_saved_notice() == BT_SAVED_NOTICE_FULL) {
            snprintf(o->detail, sizeof o->detail, "Can't save: settings full");
        } else if (st == HAL_BT_SCANNING) {
            snprintf(o->detail, sizeof o->detail, "%d found", (int)d->ndev);
        }
        o->busy = st == HAL_BT_SCANNING;
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
    char nm[HAL_BT_NAME_MAX + 8];
    ui_display_name(nm, sizeof nm, v->name);
    snprintf(o->label, sizeof o->label, "%s%s", v->audio_sink ? LV_SYMBOL_AUDIO " " : "", nm[0] != '\0' ? nm : addr);
    o->dim = !v->audio_sink; /* not a headset or speaker: shown, but not what we are looking for */
    if (link_is_for(v->addr)) {
        /* the device the link is about: its state replaces the signal meter (busy dots while it works) */
        hal_bt_link_state_t l = hal_bt_link_state();
        link_text_for(v->addr, o->detail, sizeof o->detail);
        o->busy = link_busy(l);
        o->dim = false;
    } else if (v->rssi != HAL_BT_RSSI_UNKNOWN) {
        o->bars = (uint8_t)ui_signal_bars(v->rssi, &k_ui_signal_bt, 0, 0);
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
    hal_bt_state_t st = hal_bt_state();
    bool changed = false;
    if (d->pair_wanted && st == HAL_BT_ON) { /* the scan Select asked to stop has stopped: pair now */
        d->pair_wanted = false;
        if (bt_service_pair(&d->pair_dev) != HAL_OK) {
            dev_say(d, "Cannot pair now");
        }
        changed = true;
    }
    uint32_t sig = (uint32_t)hal_bt_link_state() | (uint32_t)hal_bt_link_reason() << 4 | (uint32_t)bt_saved_notice() << 12;
    if (sig != d->sig) {
        d->sig = sig;
        changed = true;
    }
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
    hal_bt_link_state_t l = hal_bt_link_state();
    if (sel == DROW_SCAN) {
        if (link_busy(l)) {
            bt_service_disconnect(); /* "Cancel pairing" */
        } else if (st == HAL_BT_SCANNING) {
            d->pair_wanted = false;
            hal_bt_inquiry_stop();
        } else if (st == HAL_BT_ON) {
            dev_scan_begin(d);
            ui_list_set_count(d->list, dev_rows(d));
        }
        ui_list_refresh(d->list);
    } else if (d->ndev > 0 && sel - DROWS_FIXED < d->ndev) {
        const hal_bt_device_t *v = &d->dev[sel - DROWS_FIXED];
        if (!v->audio_sink) {
            dev_say(d, "Not an audio device");
        } else if (link_busy(l)) {
            dev_say(d, "Busy");
        } else if (l == HAL_BT_LINK_CONNECTED) {
            dev_say(d, "Disconnect first"); /* one headset at a time */
        } else if (st == HAL_BT_SCANNING) {
            d->pair_dev = *v; /* a scan and a link set-up cannot overlap: stop the scan, pair when it has */
            d->pair_wanted = true;
            hal_bt_inquiry_stop();
        } else if (st == HAL_BT_ON) {
            if (bt_service_pair(v) != HAL_OK) {
                dev_say(d, "Cannot pair now");
            }
        }
        ui_list_refresh(d->list);
    }
    return true;
}

static void dev_destroy(ui_screen_t *s) {
    if (hal_bt_state() == HAL_BT_SCANNING) {
        hal_bt_inquiry_stop(); /* do not leave the radio scanning for nobody */
    }
    if (hal_bt_link_state() == HAL_BT_LINK_PAIRING) {
        bt_service_disconnect(); /* nor discoverable and pairing for nobody */
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
