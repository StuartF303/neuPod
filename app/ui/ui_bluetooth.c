// SPDX-License-Identifier: MPL-2.0
/*
 * Bluetooth screens (design review 2026-11-12, BT-1 to BT-3; docs/ui.md, docs/bluetooth.md):
 *
 *  - Settings > Bluetooth: a short page, Audio output (Auto / Wired / Bluetooth), Devices >, Absolute volume (read-only: what the headset
 *    reports; the firmware has no switch for it). Bluetooth On / Off is not offered: the radio drivers have no clean power-down (backlog).
 *  - Devices: the remembered headset first (its state on the right: Streaming / Connected / Saved / Reconnecting...), then the devices a scan
 *    found (a monochrome three-bar signal; phones and the like greyed and named "(phone)"), then "Scan again". A scan starts when the screen opens
 *    (not while audio is streaming: an inquiry would stutter it) and shows four dots at the bottom. Select on a found audio device pairs
 *    (the one-headset model: with a headset remembered it asks "Replace ...?" first, Select = yes, Menu = no); the row says "Pairing..." with busy dots and
 *    Menu cancels. Select on the remembered headset opens a small list: Connect / Disconnect / Cancel, Forget (asks "Forget ...?", Select = yes, Menu = no).
 *    Renaming a headset is not offered (the record keeps the name the headset reports).
 *  - The link's life (lost, reconnecting, back, given up) is told by toasts from the frame (ui_frame.c), not by these screens.
 *
 * Data flow: bt_service.c queues the radio's scan events (state, device found / named, scan done); the Devices screen polls the queue on every UI pass
 * and keeps its own table. Link changes are not queued: the screens read hal_bt_link_state() on their tick. Starting and stopping a scan or a link
 * are non-blocking; leaving the Devices screen stops the scan and cancels a Pair.
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

#define BT_UI_MAX 12u /* devices shown (the radio keeps the same number) */
#define ELLIPSIS "\xE2\x80\xA6"

static void fmt_addr(const uint8_t a[6], char *out, size_t n) {
    snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

static bool radio_usable(hal_bt_state_t s) { return s == HAL_BT_ON || s == HAL_BT_SCANNING; }

static const char *state_text(hal_bt_state_t s) {
    switch (s) {
    case HAL_BT_OFF:
        return caps_available(CAP_BLUETOOTH) ? "Off" : "Unavailable";
    case HAL_BT_STARTING: return "Starting" ELLIPSIS;
    case HAL_BT_ON: return "On";
    case HAL_BT_SCANNING: return "Scanning" ELLIPSIS;
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

/*
 * The state of a device as one phrase for the right of its row, and whether work is going on (busy dots). @p saved: the remembered headset, which
 * rests at "Saved"; a found device has no phrase at rest (the caller shows its signal).
 */
static void device_state(const uint8_t addr[6], bool saved, char *out, size_t n, bool *busy) {
    hal_bt_link_state_t l = hal_bt_link_state();
    const char *rest = saved ? "Saved" : "";
    *busy = false;
    if (!link_is_for(addr)) {
        *busy = saved && bt_service_reconnecting();
        snprintf(out, n, "%s", *busy ? "Reconnecting" ELLIPSIS : rest);
        return;
    }
    switch (l) {
    case HAL_BT_LINK_PAIRING:
        *busy = true;
        snprintf(out, n, "Pairing" ELLIPSIS);
        break;
    case HAL_BT_LINK_CONNECTING:
        *busy = true;
        snprintf(out, n, "%s", bt_service_reconnecting() ? "Reconnecting" ELLIPSIS : "Connecting" ELLIPSIS);
        break;
    case HAL_BT_LINK_CONNECTED:
        if (bt_saved_notice() == BT_SAVED_NOTICE_NOT_SAVED) {
            snprintf(out, n, "Not saved"); /* paired, but the card could not keep it: gone after a restart */
        } else {
            switch (hal_bt_stream_state()) {
            case HAL_BT_STREAM_STREAMING: snprintf(out, n, "Streaming"); break;
            case HAL_BT_STREAM_OPENING:
                *busy = true;
                snprintf(out, n, "Connecting" ELLIPSIS);
                break;
            case HAL_BT_STREAM_FAILED: snprintf(out, n, "No audio"); break;
            default: snprintf(out, n, "Connected"); break;
            }
        }
        break;
    case HAL_BT_LINK_FAILED:
        if (bt_service_reconnecting()) {
            *busy = true;
            snprintf(out, n, "Reconnecting" ELLIPSIS); /* the next attempt is on its way (bt_reconnect.h) */
        } else if (saved && hal_bt_link_reason() == HAL_BT_FAIL_LINK_LOST) {
            snprintf(out, n, "Saved"); /* it went away and the reconnect gave up: the toast said so */
        } else {
            snprintf(out, n, "Failed: %s", hal_bt_fail_text(hal_bt_link_reason()));
        }
        break;
    case HAL_BT_LINK_DISCONNECTED:
        *busy = saved && bt_service_reconnecting();
        snprintf(out, n, "%s", *busy ? "Reconnecting" ELLIPSIS : rest);
        break;
    }
}

/* The remembered headset's name for display ("Headset" if it never had one). */
static void saved_name(const hal_bt_device_t *sv, char *out, size_t n) {
    ui_text_display(out, n, sv->name);
    if (out[0] == '\0') {
        snprintf(out, n, "Headset");
    }
}

void ui_bt_headset_text(char *out, size_t n) {
    hal_bt_device_t sv;
    if (!bt_saved_get(&sv)) {
        out[0] = '\0';
        return;
    }
    char st[32], nm[HAL_BT_NAME_MAX + 8];
    bool busy;
    device_state(sv.addr, true, st, sizeof st, &busy);
    saved_name(&sv, nm, sizeof nm);
    snprintf(out, n, "%s\n%s", nm, st);
}

/* Everything on the link side that a row shows, folded into one number: when it changes the rows are rebuilt. */
static uint32_t link_sig(const uint8_t addr[6]) {
    return (uint32_t)hal_bt_link_state() | (uint32_t)hal_bt_link_reason() << 4 | (link_is_for(addr) ? 1u << 8 : 0u) |
           (uint32_t)hal_bt_stream_state() << 9 | (uint32_t)hal_bt_stream_reason() << 12 | (uint32_t)audio_route_pref() << 17 |
           (uint32_t)audio_route_output() << 19 | (bt_service_reconnecting() ? 1u << 20 : 0u) | (hal_bt_volume_supported() ? 1u << 21 : 0u) |
           (uint32_t)bt_saved_notice() << 22 | (uint32_t)hal_bt_state() << 24;
}

/* ---- a yes / no question (Forget, Replace): Select = yes, Menu = no, never a self-changing row ---- */

typedef struct {
    ui_screen_t base;
    void (*yes)(void *ctx);
    void *ctx;
} confirm_t;

static bool confirm_button(ui_screen_t *s, uint8_t btn) {
    confirm_t *c = (confirm_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false; /* Menu: no, back to where it came from */
    }
    void (*yes)(void *) = c->yes;
    void *ctx = c->ctx;
    ui_frame_pop(); /* destroys this screen: only the copies above are used from here */
    if (yes != NULL) {
        yes(ctx);
    }
    return true;
}

/* @p head and @p sub are shown through the display filter (they carry device names); @p yes_word and @p no_word name the two keys. */
ui_screen_t *ui_confirm_create(const char *title, const char *head, const char *sub, const char *yes_word, const char *no_word, void (*yes)(void *), void *ctx) {
    confirm_t *c = (confirm_t *)ui_screen_new(sizeof *c, title);
    if (c == NULL) {
        return NULL;
    }
    c->base.on_button = confirm_button;
    c->yes = yes;
    c->ctx = ctx;
    const int32_t w = UI_W - 2 * UIM->pad;
    lv_obj_t *h = ui_label(c->base.root, "", UI_C_INK);
    lv_obj_set_style_text_font(h, UIM->font_large, 0);
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(h, w);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    ui_label_set_text(h, head);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, UI_CONTENT_H * 22 / 100);
    lv_obj_t *s = ui_label_small(c->base.root, "", UI_C_INK2);
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s, w);
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    ui_label_set_text(s, sub);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, UI_CONTENT_H * 22 / 100 + 2 * lv_font_get_line_height(UIM->font_large) + UIM->pad);
    char keys[64];
    snprintf(keys, sizeof keys, "Select = %s  \xC2\xB7  Menu = %s", yes_word, no_word);
    lv_obj_t *k = ui_label_small(c->base.root, keys, UI_C_INK3);
    lv_obj_set_style_text_align(k, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(k, LV_ALIGN_BOTTOM_MID, 0, -UIM->pad);
    return &c->base;
}

#define confirm_create ui_confirm_create

/* ---- the remembered headset's options: Connect / Disconnect / Cancel, Forget ---- */

#define OPT_ACTION 0
#define OPT_FORGET 1
#define OPT_ROWS 2

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    hal_bt_device_t saved;
    uint32_t sig;
    char title[HAL_BT_NAME_MAX + 8];
} opt_screen_t;

static bool opt_mine(const opt_screen_t *o) { return link_is_for(o->saved.addr) && hal_bt_link_state() != HAL_BT_LINK_FAILED; }

static void opt_get(void *ctx, int32_t idx, ui_list_item_t *it) {
    const opt_screen_t *o = ctx;
    hal_bt_link_state_t l = hal_bt_link_state();
    if (idx == OPT_ACTION) {
        if (opt_mine(o) && link_busy(l)) {
            snprintf(it->label, sizeof it->label, "Cancel");
        } else if (opt_mine(o) && l == HAL_BT_LINK_CONNECTED) {
            snprintf(it->label, sizeof it->label, "Disconnect");
        } else {
            snprintf(it->label, sizeof it->label, "Connect");
            it->dim = hal_bt_state() != HAL_BT_ON || link_busy(l) || l == HAL_BT_LINK_CONNECTED; /* not while scanning, or while another link exists */
        }
        return;
    }
    snprintf(it->label, sizeof it->label, "Forget");
}

static void forget_yes(void *ctx) {
    (void)ctx;
    bt_service_forget(); /* the options screen under it sees the headset gone and closes itself */
}

static bool opt_button(ui_screen_t *s, uint8_t btn) {
    opt_screen_t *o = (opt_screen_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    if (ui_list_selected(o->list) == OPT_FORGET) {
        char nm[HAL_BT_NAME_MAX + 8], head[HAL_BT_NAME_MAX + 16];
        saved_name(&o->saved, nm, sizeof nm);
        snprintf(head, sizeof head, "Forget %s?", nm);
        ui_frame_push(confirm_create("Forget", head, "It will have to be paired again.", "Forget", "Keep", forget_yes, NULL));
        return true;
    }
    hal_bt_link_state_t l = hal_bt_link_state();
    if (opt_mine(o) && (link_busy(l) || l == HAL_BT_LINK_CONNECTED)) {
        bt_service_disconnect();
    } else if (hal_bt_state() == HAL_BT_ON && !link_busy(l) && l != HAL_BT_LINK_CONNECTED) {
        (void)bt_service_connect();
    } else {
        ui_toast_show("Not now", UI_TOAST_WARN, 0);
        return true;
    }
    ui_frame_pop(); /* back to Devices, where the row shows what happens; destroys this screen */
    return true;
}

static void opt_tick(ui_screen_t *s) {
    opt_screen_t *o = (opt_screen_t *)s;
    if (!bt_saved_get(NULL)) {
        ui_frame_pop(); /* forgotten (here or elsewhere): nothing left to give options for; destroys this screen */
        return;
    }
    uint32_t sig = link_sig(o->saved.addr);
    if (sig != o->sig) {
        o->sig = sig;
        ui_list_refresh(o->list);
    }
}

static void opt_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((opt_screen_t *)s)->list, steps); }
static void opt_destroy(ui_screen_t *s) { ui_list_destroy(((opt_screen_t *)s)->list); }
static void opt_suspend(ui_screen_t *s) { ui_list_suspend(((opt_screen_t *)s)->list); }
static void opt_resume(ui_screen_t *s) {
    ui_list_resume(((opt_screen_t *)s)->list);
    ui_list_refresh(((opt_screen_t *)s)->list);
}

static ui_screen_t *options_create(const hal_bt_device_t *saved) {
    opt_screen_t *o = (opt_screen_t *)ui_screen_new(sizeof *o, "");
    if (o == NULL) {
        return NULL;
    }
    o->saved = *saved;
    saved_name(saved, o->title, sizeof o->title);
    o->base.title = o->title; /* the status bar names the headset */
    o->base.on_scroll = opt_scroll;
    o->base.on_button = opt_button;
    o->base.on_tick = opt_tick;
    o->base.destroy = opt_destroy;
    o->base.suspend = opt_suspend;
    o->base.resume = opt_resume;
    o->sig = link_sig(saved->addr);
    o->list = ui_list_create(o->base.root, UI_W, &(ui_list_model_t){.count = OPT_ROWS, .ctx = o, .get = opt_get});
    return &o->base;
}

/* ---- Settings > Bluetooth ---- */

#define ROW_OUTPUT 0 /* Audio output: Auto / Wired / Bluetooth (saved) */
#define ROW_DEVICES 1
#define ROW_ABS 2    /* Absolute volume: what the headset reports */
#define ROWS_BT 3

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    uint32_t sig;
} bt_screen_t;

static const char *output_text(char *buf, size_t n) {
    switch (audio_route_pref()) {
    case AUDIO_ROUTE_WIRED: return "Wired";
    case AUDIO_ROUTE_BLUETOOTH: return "Bluetooth";
    case AUDIO_ROUTE_AUTO: break;
    }
    snprintf(buf, n, "%s", audio_route_output() == AUDIO_OUT_BT ? "Auto: Bluetooth" : "Auto");
    return buf;
}

static void bt_get(void *ctx, int32_t idx, ui_list_item_t *o) {
    (void)ctx;
    hal_bt_state_t st = hal_bt_state();
    char buf[32];
    switch (idx) {
    case ROW_OUTPUT:
        snprintf(o->label, sizeof o->label, "Audio output");
        snprintf(o->detail, sizeof o->detail, "%s", output_text(buf, sizeof buf));
        return;
    case ROW_DEVICES: {
        hal_bt_device_t sv;
        snprintf(o->label, sizeof o->label, "Devices");
        o->chevron = true;
        o->dim = !radio_usable(st);
        if (!radio_usable(st)) {
            snprintf(o->detail, sizeof o->detail, "%s", state_text(st));
        } else if (bt_saved_get(&sv)) {
            bool busy;
            device_state(sv.addr, true, o->detail, sizeof o->detail, &busy);
        } else {
            snprintf(o->detail, sizeof o->detail, "None");
        }
        return;
    }
    default:
        snprintf(o->label, sizeof o->label, "Absolute volume");
        snprintf(o->detail, sizeof o->detail, "%s", hal_bt_volume_supported() ? "Yes" : hal_bt_link_state() == HAL_BT_LINK_CONNECTED ? "No" : "-");
        o->dim = true; /* read-only: the headset decides (docs/bluetooth.md) */
        return;
    }
}

static void bt_tick(ui_screen_t *s) {
    bt_screen_t *b = (bt_screen_t *)s;
    hal_bt_device_t sv;
    uint32_t sig = link_sig(bt_saved_get(&sv) ? sv.addr : (const uint8_t[6]){0}) ^ (bt_saved_get(NULL) ? 1u << 31 : 0u);
    if (sig != b->sig) {
        b->sig = sig;
        ui_list_refresh(b->list);
    }
}

static void bt_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((bt_screen_t *)s)->list, steps); }

static bool bt_button(ui_screen_t *s, uint8_t btn) {
    bt_screen_t *b = (bt_screen_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    switch (ui_list_selected(b->list)) {
    case ROW_OUTPUT:
        audio_route_set_pref(audio_route_pref() == AUDIO_ROUTE_AUTO ? AUDIO_ROUTE_WIRED : audio_route_pref() == AUDIO_ROUTE_WIRED ? AUDIO_ROUTE_BLUETOOTH : AUDIO_ROUTE_AUTO);
        ui_list_refresh(b->list);
        break;
    case ROW_DEVICES:
        if (radio_usable(hal_bt_state())) {
            ui_frame_push(ui_screen_bt_devices_create());
        } else {
            ui_toast_show(hal_bt_state() == HAL_BT_STARTING ? "Bluetooth is starting" : "Bluetooth is not available", UI_TOAST_WARN, 0);
        }
        break;
    default:
        break; /* Absolute volume is a read-out */
    }
    return true;
}

static void bt_destroy(ui_screen_t *s) { ui_list_destroy(((bt_screen_t *)s)->list); }
static void bt_suspend(ui_screen_t *x) { ui_list_suspend(((bt_screen_t *)x)->list); }
static void bt_resume(ui_screen_t *x) {
    ui_list_resume(((bt_screen_t *)x)->list);
    ui_list_refresh(((bt_screen_t *)x)->list);
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
    b->base.suspend = bt_suspend;
    b->base.resume = bt_resume;
    b->sig = UINT32_MAX; /* the first tick draws once more: harmless */
    b->list = ui_list_create(b->base.root, UI_W, &(ui_list_model_t){.count = ROWS_BT, .ctx = b, .get = bt_get});
    return &b->base;
}

/* ---- Devices: the remembered headset, what a scan found, Scan again ---- */

#define DOTS 4
#define DOT_MS 350u

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    hal_bt_device_t dev[BT_UI_MAX]; /* everything the scan reported (the remembered headset included; it is not listed twice) */
    int32_t ndev;
    bool have_saved;
    hal_bt_device_t saved;
    bool started;            /* a scan was started from this screen */
    bool scan_done;          /* ... and has finished */
    bool gave_up;            /* the radio is not usable: stop trying to start */
    hal_bt_state_t shown;
    bool pair_wanted;        /* Select on a device while the scan was still running (or the old headset still connected): pair once that is over */
    hal_bt_device_t pair_dev;
    uint32_t sig;
    lv_obj_t *strip, *dot[DOTS]; /* "Scanning" and four dots under the list, while a scan runs */
    uint32_t dot_col[DOTS];
    int dots_on;
    int32_t strip_h;
} dev_screen_t;

/* The found devices shown in the list: all of them but the remembered headset. */
static int32_t found_count(const dev_screen_t *d) {
    int32_t n = 0;
    for (int32_t i = 0; i < d->ndev; i++) {
        n += !(d->have_saved && memcmp(d->dev[i].addr, d->saved.addr, 6) == 0);
    }
    return n;
}

static const hal_bt_device_t *found_at(const dev_screen_t *d, int32_t k) {
    for (int32_t i = 0; i < d->ndev; i++) {
        if (d->have_saved && memcmp(d->dev[i].addr, d->saved.addr, 6) == 0) {
            continue;
        }
        if (k-- == 0) {
            return &d->dev[i];
        }
    }
    return NULL;
}

/* Row layout: [remembered headset] [found devices, or one placeholder row] [Scan again] */
static int32_t dev_rows(const dev_screen_t *d) {
    int32_t nf = found_count(d);
    return (d->have_saved ? 1 : 0) + (nf > 0 ? nf : 1) + 1;
}

static void dev_get(void *ctx, int32_t idx, ui_list_item_t *o) {
    const dev_screen_t *d = ctx;
    hal_bt_state_t st = hal_bt_state();
    const int32_t ns = d->have_saved ? 1 : 0;
    const int32_t nf = found_count(d);
    const int32_t nmid = nf > 0 ? nf : 1;
    if (idx < ns) { /* the remembered headset */
        char nm[HAL_BT_NAME_MAX + 8];
        saved_name(&d->saved, nm, sizeof nm);
        snprintf(o->label, sizeof o->label, "%s", nm);
        device_state(d->saved.addr, true, o->detail, sizeof o->detail, &o->busy);
        return;
    }
    if (idx == ns + nmid) { /* Scan again */
        snprintf(o->label, sizeof o->label, "%s", st == HAL_BT_SCANNING ? "Stop scanning" : "Scan again");
        o->dim = !radio_usable(st);
        return;
    }
    if (nf == 0) {
        o->dim = true;
        if (st == HAL_BT_STARTING) {
            snprintf(o->label, sizeof o->label, "Waiting for the radio" ELLIPSIS);
        } else if (!radio_usable(st)) {
            capabilities_t c;
            caps_get(&c);
            snprintf(o->label, sizeof o->label, "Not available: %s", c.brief[CAP_BLUETOOTH][0] != '\0' ? c.brief[CAP_BLUETOOTH] : "radio off");
        } else if (st == HAL_BT_SCANNING || (d->started && !d->scan_done)) {
            snprintf(o->label, sizeof o->label, "Searching" ELLIPSIS);
        } else if (d->scan_done) {
            snprintf(o->label, sizeof o->label, "No devices found");
        } else {
            snprintf(o->label, sizeof o->label, "Not scanned yet");
        }
        return;
    }
    const hal_bt_device_t *v = found_at(d, idx - ns);
    if (v == NULL) {
        return;
    }
    char addr[18], nm[HAL_BT_NAME_MAX + 8];
    fmt_addr(v->addr, addr, sizeof addr);
    ui_text_display(nm, sizeof nm, v->name);
    const char *word = v->audio_sink ? "" : ui_bt_class_word(v->cod);
    snprintf(o->label, sizeof o->label, "%s%s%s%s", nm[0] != '\0' ? nm : addr, word[0] != '\0' ? " (" : "", word, word[0] != '\0' ? ")" : "");
    o->dim = !v->audio_sink; /* not a headset or speaker: shown, but not what we are looking for */
    if (link_is_for(v->addr)) {
        /* the device the link is about (a pairing in progress, or one that just failed): its state replaces the signal meter */
        device_state(v->addr, false, o->detail, sizeof o->detail, &o->busy);
        o->dim = false;
    } else if (v->rssi != HAL_BT_RSSI_UNKNOWN) {
        o->bars = (uint8_t)ui_signal_bars(v->rssi, &k_ui_signal_bt, 0, 0);
        o->mono = true;
    }
}

/* Sort once the scan is over (not while it runs, so the row under the cursor does not jump): audio devices first,
 * strongest signal first. The selection follows the device it was on. */
static void sort_devices(dev_screen_t *d) {
    const int32_t ns = d->have_saved ? 1 : 0;
    int32_t sel = ui_list_selected(d->list) - ns;
    uint8_t keep[6];
    const hal_bt_device_t *cur = sel >= 0 ? found_at(d, sel) : NULL;
    if (cur != NULL) {
        memcpy(keep, cur->addr, 6);
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
    if (cur != NULL) {
        for (int32_t k = 0, nf = found_count(d); k < nf; k++) {
            if (memcmp(found_at(d, k)->addr, keep, 6) == 0) {
                ui_list_set_selected(d->list, ns + k);
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

/* The four dots under the list: filled in ink3 one more every 350 ms, then start again. Only the dots whose fill changes are touched. */
static void strip_update(dev_screen_t *d) {
    const bool show = hal_bt_state() == HAL_BT_SCANNING;
    if (lv_obj_is_hidden(d->strip) == show) {
        lv_obj_set_hidden(d->strip, !show);
    }
    if (!show) {
        return;
    }
    const int on = 1 + (int)((os_tick_ms() / DOT_MS) % DOTS);
    if (on == d->dots_on) {
        return;
    }
    d->dots_on = on;
    for (int i = 0; i < DOTS; i++) {
        uint32_t col = i < on ? UI_C_INK3 : UI_C_LINE;
        ui_obj_bg_swap(d->dot[i], d->dot_col[i], col, 0);
        d->dot_col[i] = col;
    }
}

static void strip_build(dev_screen_t *d) {
    d->strip = lv_obj_create(d->base.root);
    lv_obj_remove_style_all(d->strip);
    lv_obj_set_size(d->strip, UI_W, d->strip_h);
    lv_obj_set_pos(d->strip, 0, UI_CONTENT_H - d->strip_h);
    lv_obj_set_scrollable(d->strip, false);
    lv_obj_set_flex_flow(d->strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(d->strip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(d->strip, UIM->glyph_bar_w, 0);
    (void)ui_label_small(d->strip, "Scanning", UI_C_INK3);
    const int32_t dd = UIM->glyph_bar_w + UIM->glyph_bar_w / 2; /* a little larger than a busy dot: 6 px at 320x240 */
    for (int i = 0; i < DOTS; i++) {
        d->dot[i] = lv_obj_create(d->strip);
        lv_obj_remove_style_all(d->dot[i]);
        lv_obj_set_size(d->dot[i], dd, dd);
        lv_obj_set_scrollable(d->dot[i], false);
        lv_obj_set_style_radius(d->dot[i], LV_RADIUS_CIRCLE, 0);
        d->dot_col[i] = UI_C_LINE;
        ui_obj_bg(d->dot[i], UI_C_LINE, 0);
    }
    d->dots_on = 0;
    lv_obj_set_hidden(d->strip, true);
}

static void dev_status_left(dev_screen_t *d) {
    char b[24];
    int32_t nf = found_count(d);
    if (nf > 0) {
        snprintf(b, sizeof b, "%d found", (int)nf);
    } else {
        b[0] = '\0';
    }
    ui_screen_set_left(&d->base, b);
}

static void start_pair_now(dev_screen_t *d) {
    d->pair_wanted = false;
    if (bt_service_pair(&d->pair_dev) != HAL_OK) {
        ui_toast_show("Can't pair now", UI_TOAST_WARN, 0);
    }
}

static void dev_tick(ui_screen_t *s) {
    dev_screen_t *d = (dev_screen_t *)s;
    hal_bt_state_t st = hal_bt_state();
    bool changed = false;
    bool reselect = false;
    hal_bt_device_t sv;
    bool have = bt_saved_get(&sv);
    if (have != d->have_saved || (have && memcmp(&sv, &d->saved, sizeof sv) != 0)) {
        reselect = have != d->have_saved; /* a headset became remembered (a pairing finished) or was forgotten: the rows moved, go to the top */
        d->have_saved = have;
        if (have) {
            d->saved = sv;
        }
        changed = true;
    }
    if (d->pair_wanted && st == HAL_BT_ON && (hal_bt_link_state() == HAL_BT_LINK_DISCONNECTED || hal_bt_link_state() == HAL_BT_LINK_FAILED)) {
        start_pair_now(d); /* the scan has stopped (and the replaced headset has let go): pair now */
        changed = true;
    }
    uint32_t sig = link_sig(d->saved.addr);
    if (sig != d->sig) {
        if (bt_saved_notice() != (bt_saved_notice_t)((d->sig >> 22) & 3u)) { /* the Pair ended connected but could not be remembered */
            if (bt_saved_notice() == BT_SAVED_NOTICE_FULL) {
                ui_toast_show("Can't save: settings full", UI_TOAST_WARN, 0);
            } else if (bt_saved_notice() == BT_SAVED_NOTICE_NOT_SAVED) {
                ui_toast_show("Paired, but not saved (read-only card)", UI_TOAST_WARN, 0);
            }
        }
        d->sig = sig;
        changed = true;
    }
    if (!d->started && !d->gave_up) {
        if (st == HAL_BT_ON) {
            if (hal_bt_stream_state() != HAL_BT_STREAM_STREAMING) { /* an inquiry would stutter the music: then the user asks for it (Scan again) */
                dev_scan_begin(d);
            } else {
                d->gave_up = true; /* "gave up" on the automatic scan only */
            }
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
    if (changed) {
        ui_list_set_count(d->list, dev_rows(d));
        if (reselect) {
            ui_list_set_selected(d->list, 0);
        }
        dev_status_left(d);
    }
    strip_update(d);
}

static void dev_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((dev_screen_t *)s)->list, steps); }

/* "Replace <old> with <new>?" answered yes: let the old one go, pair the new one as soon as the radio is free. */
static void replace_yes(void *ctx) {
    dev_screen_t *d = ctx;
    if (hal_bt_link_state() == HAL_BT_LINK_CONNECTED || link_busy(hal_bt_link_state())) {
        bt_service_disconnect();
    }
    if (hal_bt_state() == HAL_BT_SCANNING) {
        hal_bt_inquiry_stop();
    }
    d->pair_wanted = true;
}

static bool dev_button(ui_screen_t *s, uint8_t btn) {
    dev_screen_t *d = (dev_screen_t *)s;
    if (btn == HAL_BTN_MENU) {
        if (d->pair_wanted || hal_bt_link_state() == HAL_BT_LINK_PAIRING) {
            d->pair_wanted = false; /* Menu cancels a pairing; the next Menu leaves */
            if (hal_bt_link_state() == HAL_BT_LINK_PAIRING) {
                bt_service_disconnect();
            }
            ui_list_refresh(d->list);
            return true;
        }
        return false;
    }
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    const int32_t ns = d->have_saved ? 1 : 0;
    const int32_t nf = found_count(d);
    const int32_t sel = ui_list_selected(d->list);
    hal_bt_state_t st = hal_bt_state();
    hal_bt_link_state_t l = hal_bt_link_state();
    if (sel < ns) { /* the remembered headset: its options */
        if (st == HAL_BT_SCANNING) {
            hal_bt_inquiry_stop(); /* Connect needs the radio idle */
        }
        d->pair_wanted = false;
        ui_frame_push(options_create(&d->saved));
    } else if (sel == ns + (nf > 0 ? nf : 1)) { /* Scan again / Stop scanning */
        if (st == HAL_BT_SCANNING) {
            d->pair_wanted = false;
            hal_bt_inquiry_stop();
        } else if (st == HAL_BT_ON) {
            dev_scan_begin(d);
            ui_list_set_count(d->list, dev_rows(d));
            dev_status_left(d);
        }
        ui_list_refresh(d->list);
    } else if (nf > 0) {
        const hal_bt_device_t *v = found_at(d, sel - ns);
        if (v == NULL) {
            return true;
        }
        if (!v->audio_sink) {
            ui_toast_show("Not an audio device", UI_TOAST_WARN, 0);
        } else if (link_busy(l) || d->pair_wanted) {
            ui_toast_show("Busy", UI_TOAST_WARN, 0);
        } else if (st != HAL_BT_ON && st != HAL_BT_SCANNING) {
            ui_toast_show("Bluetooth is not ready", UI_TOAST_WARN, 0);
        } else if (d->have_saved) { /* one headset at a time: ask before the remembered one is replaced */
            char old[HAL_BT_NAME_MAX + 8], nw[HAL_BT_NAME_MAX + 8], head[2 * HAL_BT_NAME_MAX + 64];
            char addr[18];
            saved_name(&d->saved, old, sizeof old);
            fmt_addr(v->addr, addr, sizeof addr);
            ui_text_display(nw, sizeof nw, v->name);
            snprintf(head, sizeof head, "Replace %s with %s?", old, nw[0] != '\0' ? nw : addr);
            d->pair_dev = *v;
            ui_frame_push(confirm_create("Replace", head, "The old one is forgotten.", "Replace", "Keep", replace_yes, d));
        } else {
            d->pair_dev = *v;
            if (st == HAL_BT_SCANNING) {
                d->pair_wanted = true; /* a scan and a link set-up cannot overlap: stop the scan, pair when it has */
                hal_bt_inquiry_stop();
            } else {
                start_pair_now(d);
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
static void dev_resume(ui_screen_t *x) {
    dev_screen_t *d = (dev_screen_t *)x;
    ui_list_resume(d->list);
    ui_list_set_count(d->list, dev_rows(d)); /* a headset may have been forgotten under it */
    dev_status_left(d);
}

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
    d->have_saved = bt_saved_get(&d->saved);
    d->sig = link_sig(d->saved.addr);
    d->strip_h = UI_CONTENT_H - (UI_ROWS_VISIBLE - 1) * UI_ROW_H; /* the scan strip takes the room of the seventh row */
    d->list = ui_list_create(d->base.root, UI_W, &(ui_list_model_t){.count = dev_rows(d), .ctx = d, .get = dev_get, .bottom = d->strip_h});
    strip_build(d);
    return &d->base;
}
