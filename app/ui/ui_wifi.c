// SPDX-License-Identifier: MPL-2.0
/*
 * Settings > Wi-Fi (docs/wifi.md): status, switches, scan, networks (scanned and remembered), "Other network..." for hidden SSIDs,
 * time zone and a manual clock. Passwords are typed with the wheel text widget (ui_input.h). Chained modals (SSID, then
 * security, then password) are sequenced from this screen's tick, because a modal closes AFTER its callback returns.
 */
#include <stdio.h>
#include <string.h>

#include "hal_radio.h"
#include "os.h"
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
    net_t net[NET_MAX];
    uint32_t nnet;
    bool scanning;
    uint32_t last_ms;
} wifi_screen_t;

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
static const char *const k_onoff[] = {"Off", "On"};

static bool g_pending_scan_open; /* the screen was just built */

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
    case HAL_WIFI_UP: return "Connected";
    case HAL_WIFI_FAILED: return "Could not connect";
    case HAL_WIFI_BAD_AUTH: return "Wrong password";
    }
    return "?";
}

static net_t *find_net(wifi_screen_t *w, const char *ssid) {
    for (uint32_t i = 0; i < w->nnet; i++) {
        if (strcmp(w->net[i].ssid, ssid) == 0) {
            return &w->net[i];
        }
    }
    return NULL;
}

static net_t *add_net(wifi_screen_t *w, const char *ssid) {
    net_t *n = find_net(w, ssid);
    if (n == NULL && w->nnet < NET_MAX) {
        n = &w->net[w->nnet++];
        memset(n, 0, sizeof *n);
        snprintf(n->ssid, sizeof n->ssid, "%s", ssid);
    }
    return n;
}

static void rebuild_saved(wifi_screen_t *w) {
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

static int32_t count(const wifi_screen_t *w) { return ROWS_FIXED + (int32_t)(w->nnet == 0 ? 1 : w->nnet); }

static void wifi_get(void *ctx, int32_t i, ui_list_item_t *o) {
    const wifi_screen_t *w = ctx;
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
            snprintf(o->detail, sizeof o->detail, "%s  %d dBm", info.ip, info.rssi);
        } else {
            snprintf(o->detail, sizeof o->detail, "%s", state_text(&info, &s));
        }
        o->dim = !s.enabled;
        return;
    case ROW_SCAN:
        snprintf(o->label, sizeof o->label, "%s", w->scanning ? "Scanning..." : "Scan for networks");
        o->dim = !s.enabled;
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
    if (w->nnet == 0) {
        o->dim = true;
        snprintf(o->label, sizeof o->label, "%s", w->scanning ? "Searching..." : "No networks: scan, or add one");
        return;
    }
    const net_t *n = &w->net[i - ROWS_FIXED];
    bool cur = info.state == HAL_WIFI_UP && strcmp(info.ssid, n->ssid) == 0;
    snprintf(o->label, sizeof o->label, "%s%s", cur ? LV_SYMBOL_OK " " : n->saved ? "* " : "", n->ssid);
    if (n->needs_pw) {
        snprintf(o->detail, sizeof o->detail, "password needed");
    } else if (n->rssi != 0) {
        snprintf(o->detail, sizeof o->detail, "%s %d dBm", sec_text(n->sec), (int)n->rssi);
    } else {
        snprintf(o->detail, sizeof o->detail, "saved");
    }
    o->dim = n->rssi == 0 && !n->saved;
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
    ui_input_open(&(wi_request_t){.kind = WI_K_TEXT, .title = "Network name", .max_chars = HAL_WIFI_SSID_MAX, .done = on_text_done});
}

static void ask_password(const char *ssid) {
    g_flow.next = NEXT_CONNECT_NEW;
    if (ssid != g_flow.target) {
        snprintf(g_flow.target, sizeof g_flow.target, "%s", ssid);
    }
    ui_input_open(&(wi_request_t){.kind = WI_K_TEXT, .title = "Password", .max_chars = HAL_WIFI_PSK_MAX, .masked = true, .done = on_text_done});
}

static void tick_flow(wifi_screen_t *w) {
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
        (void)wifi_connect_request(g_flow.target, g_flow.psk, true);
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
            net_t *n = find_net(w, g_flow.target);
            if (n != NULL) {
                n->saved = false;
                n->needs_pw = false;
            }
        } else if (cur) {
            wifi_disconnect_request();
        } else {
            (void)wifi_connect_request(g_flow.target, NULL, false);
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
        if (set.enabled && !w->scanning && wifi_scan_request()) {
            w->scanning = true;
            w->nnet = 0;
            rebuild_saved(w);
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
    if (w->nnet == 0 || sel < ROWS_FIXED) {
        return true;
    }
    const net_t *n = &w->net[sel - ROWS_FIXED];
    snprintf(g_flow.target, sizeof g_flow.target, "%s", n->ssid);
    wifi_info_t info;
    wifi_get_info(&info);
    bool cur = info.state == HAL_WIFI_UP && strcmp(info.ssid, n->ssid) == 0;
    if (n->saved && (!n->needs_pw || cur)) {
        ui_input_open(&(wi_request_t){.kind = WI_K_PICKER, .title = n->ssid, .items = cur ? k_cur_actions : k_net_actions, .count = 2,
                                      .done = on_action_done});
    } else if (n->sec == HAL_WIFI_SEC_OPEN) {
        (void)wifi_connect_request(n->ssid, "", true);
    } else {
        ask_password(n->ssid); /* a new secured network, or a saved one whose password must be typed again */
    }
    return true;
}

static void wifi_tick(ui_screen_t *s) {
    wifi_screen_t *w = (wifi_screen_t *)s;
    tick_flow(w);
    bool changed = false;
    if (w->scanning) {
        hal_wifi_ap_t ap;
        bool done = false;
        while (wifi_scan_next(&ap, &done)) {
            net_t *n = add_net(w, ap.ssid);
            if (n != NULL) {
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
        ui_list_set_count(w->list, count(w));
    }
}

static void wifi_scroll(ui_screen_t *s, int32_t steps) { ui_list_move(((wifi_screen_t *)s)->list, steps); }
static void wifi_destroy(ui_screen_t *s) { ui_list_destroy(((wifi_screen_t *)s)->list); }
static void list_suspend(ui_screen_t *x) { ui_list_suspend(((wifi_screen_t *)x)->list); }
static void list_resume(ui_screen_t *x) { ui_list_resume(((wifi_screen_t *)x)->list); }

ui_screen_t *ui_screen_wifi_create(void) {
    wifi_screen_t *w = (wifi_screen_t *)ui_screen_new(sizeof *w, "Wi-Fi");
    if (w == NULL) {
        return NULL;
    }
    w->base.on_scroll = wifi_scroll;
    w->base.on_button = wifi_button;
    w->base.on_tick = wifi_tick;
    w->base.destroy = wifi_destroy;
    w->base.suspend = list_suspend;
    w->base.resume = list_resume;
    memset(&g_flow, 0, sizeof g_flow);
    g_pending_scan_open = false;
    rebuild_saved(w);
    w->list = ui_list_create(w->base.root, UI_W, &(ui_list_model_t){.count = count(w), .ctx = w, .get = wifi_get});
    (void)k_onoff;
    return &w->base;
}
