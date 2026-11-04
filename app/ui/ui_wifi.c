// SPDX-License-Identifier: MPL-2.0
/*
 * Settings > Wi-Fi (docs/wifi.md): status, switches, "Scan for networks" (opens the Networks screen), "Other network..." for
 * hidden SSIDs, time zone and a manual clock. The Networks screen scans when it opens and lists the networks found and the
 * remembered ones; Select on one starts the join flow. Passwords are typed with the wheel text widget (ui_input.h). Chained
 * modals (SSID, then security, then password) are sequenced from the tick of the screen that opened them, because a modal
 * closes AFTER its callback returns.
 */
#include <stdio.h>
#include <string.h>

#include "hal_radio.h"
#include "os.h"
#include "runtime_config.h"
#include "sys_clock.h"
#include "ui_input.h"
#include "ui_internal.h"
#include "wifi_service.h"

#define ROW_ENABLED 0
#define ROW_STATUS 1
#define ROW_SCAN 2
#define ROW_OTHER 3
#define ROW_ZONE 4
#define ROW_TIME 5
#define ROW_PLAYBACK 6
#define ROWS_FIXED 7
#define NROW_SCAN 0 /* Networks screen: "Scan again" / "Scanning...", then the networks */
#define NROWS_FIXED 1
#define NET_MAX 20u

typedef struct {
    char ssid[HAL_WIFI_SSID_MAX + 1];
    int8_t rssi; /* 0 = not seen in the scan */
    hal_wifi_sec_t sec;
    bool saved, needs_pw;
} net_t;

typedef enum { NEXT_NONE, NEXT_ASK_SEC, NEXT_ASK_PW, NEXT_CONNECT_NEW, NEXT_ACTION } next_t;

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    uint32_t last_ms;
} wifi_screen_t;

typedef struct {
    ui_screen_t base;
    ui_list_t *list;
    net_t net[NET_MAX];
    uint32_t nnet;
    bool scanning;
    bool started;      /* the scan was requested from this screen */
    uint32_t last_ms;
} nets_screen_t;

/* The connect flow lives outside the screen object: modals are pushed above it. */
static struct {
    next_t next;
    char ssid[HAL_WIFI_SSID_MAX + 1];
    char psk[HAL_WIFI_PSK_MAX + 1];
    bool secured;
    uint32_t pick;
    char target[HAL_WIFI_SSID_MAX + 1];
} g_flow;

static const char *const k_sec_items[] = {"Open (no password)", "Password (WPA2 / WPA3)"};
static const char *const k_net_actions[] = {"Connect", "Forget this network"};
static const char *const k_cur_actions[] = {"Disconnect", "Forget this network"};

static const char *sec_text(hal_wifi_sec_t s) {
    switch (s) {
    case HAL_WIFI_SEC_OPEN: return "open";
    case HAL_WIFI_SEC_WPA2: return "WPA2";
    case HAL_WIFI_SEC_WPA3: return "WPA3";
    case HAL_WIFI_SEC_WPA2_WPA3: return "WPA2/3";
    default: return "secured";
    }
}

static const char *state_text(const wifi_info_t *w, const wifi_settings_t *s) {
    if (!s->enabled) {
        return "Off";
    }
    if (w->paused_for_playback) {
        return "Paused (playing)";
    }
    switch (w->state) {
    case HAL_WIFI_DOWN: return "Radio idle";
    case HAL_WIFI_IDLE: return w->saved ? "Not connected" : "No network saved";
    case HAL_WIFI_JOINING: return "Connecting...";
    case HAL_WIFI_NO_IP: return "Getting address...";
    case HAL_WIFI_UP: return w->save_failed ? "Connected (not saved)" : "Connected";
    case HAL_WIFI_FAILED: return "Could not connect";
    case HAL_WIFI_BAD_AUTH: return "Wrong password";
    }
    return "?";
}

static net_t *find_net(nets_screen_t *w, const char *ssid) {
    for (uint32_t i = 0; i < w->nnet; i++) {
        if (strcmp(w->net[i].ssid, ssid) == 0) {
            return &w->net[i];
        }
    }
    return NULL;
}

static net_t *add_net(nets_screen_t *w, const char *ssid) {
    net_t *n = find_net(w, ssid);
    if (n == NULL && w->nnet < NET_MAX) {
        n = &w->net[w->nnet++];
        memset(n, 0, sizeof *n);
        snprintf(n->ssid, sizeof n->ssid, "%s", ssid);
    }
    return n;
}

static void rebuild_saved(nets_screen_t *w) {
    for (uint32_t i = 0; i < w->nnet; i++) {
        w->net[i].saved = false;
    }
    char s[HAL_WIFI_SSID_MAX + 1];
    for (uint32_t i = 0; wifi_saved_get(i, s, NULL); i++) {
        net_t *n = add_net(w, s);
        if (n != NULL) {
            n->saved = true;
            n->needs_pw = wifi_saved_needs_password(s);
        }
    }
}

/* A join the user started from the Networks list: its row shows the busy dots until the station is up, the join fails,
 * or UI_BUSY_TIMEOUT_MS passes. g_busy_ssid is empty when nothing is pending. */
#define BUSY_GRACE_MS 1500u /* a FAILED/BAD_AUTH seen this soon after the request is the previous attempt's, not ours */
static char g_busy_ssid[HAL_WIFI_SSID_MAX + 1];
static uint32_t g_busy_since;

static void busy_join_begin(nets_screen_t *w, const char *ssid) { /* w: the Networks screen, if one is showing the row */
    snprintf(g_busy_ssid, sizeof g_busy_ssid, "%s", ssid);
    g_busy_since = os_tick_ms();
    if (w != NULL) {
        ui_list_refresh(w->list);
    }
}

/* True if the busy state changed (finished or timed out). */
static bool busy_join_poll(void) {
    if (g_busy_ssid[0] == '\0') {
        return false;
    }
    wifi_info_t info;
    wifi_get_info(&info);
    uint32_t el = os_tick_ms() - g_busy_since;
    bool up = info.state == HAL_WIFI_UP && strcmp(info.ssid, g_busy_ssid) == 0;
    bool failed = (info.state == HAL_WIFI_FAILED || info.state == HAL_WIFI_BAD_AUTH) && el >= BUSY_GRACE_MS;
    if (up || failed || el >= UI_BUSY_TIMEOUT_MS) {
        g_busy_ssid[0] = '\0';
        return true;
    }
    return false;
}

static void wifi_get(void *ctx, int32_t i, ui_list_item_t *o) {
    (void)ctx;
    wifi_info_t info;
    wifi_settings_t s;
    wifi_get_info(&info);
    wifi_get_settings(&s);
    switch (i) {
    case ROW_ENABLED:
        snprintf(o->label, sizeof o->label, "Wi-Fi");
        snprintf(o->detail, sizeof o->detail, "%s", s.enabled ? "On" : "Off");
        return;
    case ROW_STATUS:
        snprintf(o->label, sizeof o->label, "%s", info.state == HAL_WIFI_UP ? info.ssid : "Status");
        if (info.state == HAL_WIFI_UP) {
            snprintf(o->detail, sizeof o->detail, "%s", info.ip);
            o->bars = (uint8_t)ui_wifi_bars();
        } else {
            snprintf(o->detail, sizeof o->detail, "%s", state_text(&info, &s));
            o->busy = info.state == HAL_WIFI_JOINING || info.state == HAL_WIFI_NO_IP;
        }
        o->dim = !s.enabled;
        return;
    case ROW_SCAN:
        snprintf(o->label, sizeof o->label, "Scan for networks");
        o->dim = !s.enabled;
        o->chevron = true;
        return;
    case ROW_OTHER:
        snprintf(o->label, sizeof o->label, "Other network...");
        o->dim = !s.enabled;
        return;
    case ROW_ZONE:
        snprintf(o->label, sizeof o->label, "Time zone");
        snprintf(o->detail, sizeof o->detail, "%s", k_clock_zone_names[s.zone]);
        return;
    case ROW_TIME: {
        clock_tm_t t;
        snprintf(o->label, sizeof o->label, "Time");
        if (clock_local_now(&t)) {
            snprintf(o->detail, sizeof o->detail, "%02d:%02d %s", (int)t.h, (int)t.mi, clock_ntp_synced() ? "(network)" : "(set)");
        } else {
            snprintf(o->detail, sizeof o->detail, "not set");
        }
        return;
    }
    case ROW_PLAYBACK:
        snprintf(o->label, sizeof o->label, "Wi-Fi while playing");
        snprintf(o->detail, sizeof o->detail, "%s", s.during_playback ? "On" : "Off");
        return;
    default: break;
    }
}

static int32_t nets_count(const nets_screen_t *w) { return NROWS_FIXED + (int32_t)(w->nnet == 0 ? 1 : w->nnet); }

/* Ask for a scan; the table restarts with just the remembered networks. */
static void nets_scan_begin(nets_screen_t *w) {
    if (wifi_scan_request()) {
        w->scanning = true;
        w->started = true;
        w->nnet = 0;
        rebuild_saved(w);
    }
}

static void nets_get(void *ctx, int32_t i, ui_list_item_t *o) {
    const nets_screen_t *w = ctx;
    wifi_info_t info;
    wifi_settings_t s;
    wifi_get_info(&info);
    wifi_get_settings(&s);
    if (i == NROW_SCAN) {
        snprintf(o->label, sizeof o->label, "%s", w->scanning ? "Scanning..." : "Scan again");
        o->dim = !s.enabled;
        o->busy = w->scanning;
        return;
    }
    if (w->nnet == 0) {
        o->dim = true;
        snprintf(o->label, sizeof o->label, "%s", !s.enabled ? "Wi-Fi is off" : w->scanning ? "Searching..." : "No networks: scan again, or add one");
        return;
    }
    const net_t *n = &w->net[i - NROWS_FIXED];
    bool cur = info.state == HAL_WIFI_UP && strcmp(info.ssid, n->ssid) == 0;
    snprintf(o->label, sizeof o->label, "%s%s", cur ? LV_SYMBOL_OK " " : n->saved ? "* " : "", n->ssid);
    if (n->needs_pw) {
        snprintf(o->detail, sizeof o->detail, "password needed");
    } else if (n->rssi != 0) {
        snprintf(o->detail, sizeof o->detail, "%s", sec_text(n->sec));
        o->bars = (uint8_t)ui_signal_bars(n->rssi, &k_ui_signal_wifi, 0, 0);
    } else {
        snprintf(o->detail, sizeof o->detail, "saved");
    }
    o->dim = n->rssi == 0 && !n->saved;
    o->busy = g_busy_ssid[0] != '\0' && strcmp(g_busy_ssid, n->ssid) == 0;
}

/* ---- modal results: recorded, acted on from the tick ---- */
static void on_text_done(void *ctx, bool ok, const wi_value_t *v) {
    (void)ctx;
    if (!ok) {
        g_flow.next = NEXT_NONE;
        return;
    }
    if (g_flow.next == NEXT_ASK_SEC) { /* the SSID of "Other network..." */
        snprintf(g_flow.ssid, sizeof g_flow.ssid, "%.32s", v->text);
        g_flow.next = g_flow.ssid[0] != '\0' ? NEXT_ASK_SEC : NEXT_NONE;
    } else {
        snprintf(g_flow.psk, sizeof g_flow.psk, "%.63s", v->text);
        g_flow.next = NEXT_CONNECT_NEW;
    }
}

static void on_sec_done(void *ctx, bool ok, const wi_value_t *v) {
    (void)ctx;
    g_flow.secured = ok && v->pick == 1;
    g_flow.psk[0] = '\0';
    g_flow.next = !ok ? NEXT_NONE : g_flow.secured ? NEXT_ASK_PW : NEXT_CONNECT_NEW;
}

static void on_action_done(void *ctx, bool ok, const wi_value_t *v) {
    (void)ctx;
    g_flow.pick = ok ? v->pick : 99;
    g_flow.next = ok ? NEXT_ACTION : NEXT_NONE;
}

static void ask_ssid(void) {
    g_flow.next = NEXT_ASK_SEC; /* "the text result is an SSID" */
    ui_input_open(&(wi_request_t){.kind = WI_K_TEXT, .title = "Network name", .max_chars = HAL_WIFI_SSID_MAX, .max_bytes = HAL_WIFI_SSID_MAX, .done = on_text_done});
}

static void ask_password(const char *ssid) {
    g_flow.next = NEXT_CONNECT_NEW;
    if (ssid != g_flow.target) {
        snprintf(g_flow.target, sizeof g_flow.target, "%s", ssid);
    }
    ui_input_open(&(wi_request_t){.kind = WI_K_TEXT, .title = "Password", .max_chars = HAL_WIFI_PSK_MAX, .max_bytes = HAL_WIFI_PSK_MAX, .masked = true, .done = on_text_done});
}

/* @p w: the Networks screen, if that is the one running the flow (a forgotten network is dropped from its table). */
static void tick_flow(nets_screen_t *w) {
    if (ui_input_active()) {
        return;
    }
    switch (g_flow.next) {
    case NEXT_ASK_SEC: /* after the SSID was typed */
        g_flow.next = NEXT_NONE;
        snprintf(g_flow.target, sizeof g_flow.target, "%s", g_flow.ssid);
        ui_input_open(&(wi_request_t){.kind = WI_K_PICKER, .title = "Security", .items = k_sec_items, .count = 2, .sel = 1, .done = on_sec_done});
        break;
    case NEXT_ASK_PW:
        g_flow.next = NEXT_NONE;
        ask_password(g_flow.target);
        break;
    case NEXT_CONNECT_NEW:
        g_flow.next = NEXT_NONE;
        if (wifi_connect_request(g_flow.target, g_flow.psk, true)) {
            busy_join_begin(w, g_flow.target);
        }
        memset(g_flow.psk, 0, sizeof g_flow.psk); /* do not keep the password around in RAM */
        break;
    case NEXT_ACTION: {
        g_flow.next = NEXT_NONE;
        wifi_info_t info;
        wifi_get_info(&info);
        bool cur = info.state == HAL_WIFI_UP && strcmp(info.ssid, g_flow.target) == 0;
        if (g_flow.pick == 1) { /* forget */
            char s[HAL_WIFI_SSID_MAX + 1];
            for (uint32_t i = 0; wifi_saved_get(i, s, NULL); i++) {
                if (strcmp(s, g_flow.target) == 0) {
                    if (cur) {
                        wifi_disconnect_request();
                    }
                    wifi_forget(i);
                    break;
                }
            }
            net_t *n = w != NULL ? find_net(w, g_flow.target) : NULL;
            if (n != NULL) {
                n->saved = false;
                n->needs_pw = false;
            }
        } else if (cur) {
            wifi_disconnect_request();
        } else {
            if (wifi_connect_request(g_flow.target, NULL, false)) {
                busy_join_begin(w, g_flow.target);
            }
        }
        break;
    }
    case NEXT_NONE: break;
    }
}

static void zone_done(void *ctx, bool ok, const wi_value_t *v) {
    (void)ctx;
    if (ok) {
        wifi_settings_t s;
        wifi_get_settings(&s);
        s.zone = (clock_zone_t)v->pick;
        wifi_set_settings(&s);
    }
}

static void time_done(void *ctx, bool ok, const wi_value_t *v) {
    (void)ctx;
    if (!ok) {
        return;
    }
    /* local to UTC: subtract the zone's base offset, then one more hour if the EU rule says it was summer time */
    clock_zone_t z = clock_zone();
    int32_t base = z == CLOCK_ZONE_CET ? 1 : z == CLOCK_ZONE_EET ? 2 : 0;
    uint32_t local = clock_tm_to_unix(v->dt.y, v->dt.mo, v->dt.d, v->dt.h, v->dt.mi, 0);
    uint32_t utc = local - (uint32_t)(base * 3600);
    if (z != CLOCK_ZONE_UTC && clock_eu_dst(utc - 3600u)) {
        utc -= 3600u;
    }
    clock_set_utc(utc, false);
}

static bool wifi_button(ui_screen_t *s, uint8_t btn) {
    wifi_screen_t *w = (wifi_screen_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    int32_t sel = ui_list_selected(w->list);
    wifi_settings_t set;
    wifi_get_settings(&set);
    switch (sel) {
    case ROW_ENABLED:
        set.enabled = !set.enabled;
        wifi_set_settings(&set);
        return true;
    case ROW_PLAYBACK:
        set.during_playback = !set.during_playback;
        wifi_set_settings(&set);
        return true;
    case ROW_SCAN:
        if (set.enabled) {
            ui_frame_push(ui_screen_wifi_networks_create());
        }
        return true;
    case ROW_OTHER:
        if (set.enabled) {
            ask_ssid();
        }
        return true;
    case ROW_ZONE:
        ui_input_open(&(wi_request_t){.kind = WI_K_PICKER, .title = "Time zone", .items = k_clock_zone_names, .count = CLOCK_ZONE_COUNT,
                                      .sel = set.zone, .done = zone_done});
        return true;
    case ROW_TIME: {
        clock_tm_t t;
        if (!clock_local_now(&t)) {
            clock_unix_to_tm(clock_tm_to_unix(2026, 1, 1, 12, 0, 0), &t);
        }
        ui_input_open(&(wi_request_t){.kind = WI_K_DATETIME, .title = "Set the time", .has_date = true, .has_time = true, .y = t.y,
                                      .mo = t.mo, .d = t.d, .h = t.h, .mi = t.mi, .done = time_done});
        return true;
    }
    default: break;
    }
    return true;
}

static bool nets_button(ui_screen_t *s, uint8_t btn) {
    nets_screen_t *w = (nets_screen_t *)s;
    if (btn != HAL_BTN_SELECT) {
        return false;
    }
    int32_t sel = ui_list_selected(w->list);
    wifi_settings_t set;
    wifi_get_settings(&set);
    if (sel == NROW_SCAN) {
        if (set.enabled && !w->scanning) {
            nets_scan_begin(w);
            ui_list_set_count(w->list, nets_count(w));
        }
        return true;
    }
    if (w->nnet == 0 || sel < NROWS_FIXED) {
        return true;
    }
    const net_t *n = &w->net[sel - NROWS_FIXED];
    snprintf(g_flow.target, sizeof g_flow.target, "%s", n->ssid);
    wifi_info_t info;
    wifi_get_info(&info);
    bool cur = info.state == HAL_WIFI_UP && strcmp(info.ssid, n->ssid) == 0;
    if (n->saved && (!n->needs_pw || cur)) {
        ui_input_open(&(wi_request_t){.kind = WI_K_PICKER, .title = n->ssid, .items = cur ? k_cur_actions : k_net_actions, .count = 2,
                                      .done = on_action_done});
    } else if (n->sec == HAL_WIFI_SEC_OPEN) {
        if (wifi_connect_request(n->ssid, "", true)) {
            busy_join_begin(w, n->ssid);
        }
    } else {
        ask_password(n->ssid); /* a new secured network, or a saved one whose password must be typed again */
    }
    return true;
}

static void wifi_tick(ui_screen_t *s) {
    wifi_screen_t *w = (wifi_screen_t *)s;
    tick_flow(NULL);
    uint32_t now = os_tick_ms();
    if (now - w->last_ms >= 1000u) {
        w->last_ms = now;
        ui_list_refresh(w->list);
    }
}

static void nets_tick(ui_screen_t *s) {
    nets_screen_t *w = (nets_screen_t *)s;
    tick_flow(w);
    bool busy_changed = busy_join_poll();
    wifi_settings_t set;
    wifi_get_settings(&set);
    if (!w->started && set.enabled) {
        nets_scan_begin(w); /* a scan on open; if the request queue is full, try again on the next pass */
    }
    bool changed = busy_changed;
    if (w->scanning) {
        hal_wifi_ap_t ap;
        bool done = false;
        while (wifi_scan_next(&ap, &done)) {
            net_t *n = add_net(w, ap.ssid);
            if (n != NULL && (n->rssi == 0 || ap.rssi > n->rssi)) { /* one row per network name: the strongest access point */
                n->rssi = ap.rssi;
                n->sec = ap.sec;
            }
            changed = true;
        }
        if (done) {
            w->scanning = false;
            changed = true;
        }
    }
    uint32_t now = os_tick_ms();
    if (changed || now - w->last_ms >= 1000u) {
        w->last_ms = now;
        rebuild_saved(w);
        ui_list_set_count(w->list, nets_count(w));
    }
}

static void wifi_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((wifi_screen_t *)s)->list, steps); }
static void wifi_destroy(ui_screen_t *s) { ui_list_destroy(((wifi_screen_t *)s)->list); }
static void wifi_suspend(ui_screen_t *x) { ui_list_suspend(((wifi_screen_t *)x)->list); }
static void wifi_resume(ui_screen_t *x) { ui_list_resume(((wifi_screen_t *)x)->list); }

static void nets_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((nets_screen_t *)s)->list, steps); }
static void nets_destroy(ui_screen_t *s) {
    wifi_scan_cancel(); /* leaving mid-scan: nobody will collect the results */
    ui_list_destroy(((nets_screen_t *)s)->list);
}
static void nets_suspend(ui_screen_t *x) { ui_list_suspend(((nets_screen_t *)x)->list); }
static void nets_resume(ui_screen_t *x) { ui_list_resume(((nets_screen_t *)x)->list); }

ui_screen_t *ui_screen_wifi_create(void) {
    wifi_screen_t *w = (wifi_screen_t *)ui_screen_new(sizeof *w, "Wi-Fi");
    if (w == NULL) {
        return NULL;
    }
    w->base.on_scroll = wifi_scroll;
    w->base.on_button = wifi_button;
    w->base.on_tick = wifi_tick;
    w->base.destroy = wifi_destroy;
    w->base.suspend = wifi_suspend;
    w->base.resume = wifi_resume;
    memset(&g_flow, 0, sizeof g_flow);
    w->list = ui_list_create(w->base.root, UI_W, &(ui_list_model_t){.count = ROWS_FIXED, .ctx = w, .get = wifi_get});
    return &w->base;
}

ui_screen_t *ui_screen_wifi_networks_create(void) {
    nets_screen_t *w = (nets_screen_t *)ui_screen_new(sizeof *w, "Networks");
    if (w == NULL) {
        return NULL;
    }
    w->base.on_scroll = nets_scroll;
    w->base.on_button = nets_button;
    w->base.on_tick = nets_tick;
    w->base.destroy = nets_destroy;
    g_busy_ssid[0] = '\0';
    w->base.suspend = nets_suspend;
    w->base.resume = nets_resume;
    rebuild_saved(w);
    w->list = ui_list_create(w->base.root, UI_W, &(ui_list_model_t){.count = nets_count(w), .ctx = w, .get = nets_get});
    return &w->base;
}
