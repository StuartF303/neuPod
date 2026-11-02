// SPDX-License-Identifier: MPL-2.0
/*
 * Host simulation of the Bluetooth part of hal_radio.h. No real Bluetooth: bring-up takes a moment, and a scan "finds"
 * a fixed handful of devices (headsets, a speaker, a car kit whose name arrives late, a phone, an unnamed device) so
 * the UI and its tests run on a PC. Events are delivered from hal_bt_poll(), i.e. from the calling task, never from a
 * foreign thread. Times are shortened so the scripted tour reaches the interesting states quickly.
 */
#include <string.h>

#include "hal_radio.h"
#include "hal_system.h"

#define SIM_BRING_UP_MS 250u
#define SIM_SCAN_MS 2400u

typedef struct {
    uint32_t at_ms;      /* when it is reported, relative to the scan start */
    uint8_t addr[6];
    uint32_t cod;
    int8_t rssi;
    const char *name;    /* NULL: no name */
    uint32_t name_at_ms; /* when the name arrives (0: with the first report) */
} sim_dev_t;

static const sim_dev_t k_devs[] = {
    {200, {0x28, 0xCD, 0xC1, 0x01, 0x02, 0x03}, 0x240404, -52, "AirPods Pro 3", 0},
    {500, {0x00, 0x1B, 0x7C, 0x4A, 0x10, 0x22}, 0x240404, -61, "WH-1000XM5", 0},
    {800, {0x70, 0x99, 0x1C, 0x0B, 0x55, 0x66}, 0x240414, -70, "Flip 6", 0},
    {1100, {0x04, 0x52, 0xC7, 0x31, 0x32, 0x33}, 0x200420, -78, "Car audio", 1900},
    {1400, {0xF4, 0x0E, 0x22, 0x9A, 0x8B, 0x7C}, 0x5A020C, -66, "Pixel 9", 0},
    {1700, {0x3C, 0x22, 0xFB, 0xDE, 0xAD, 0x01}, 0x000000, -85, NULL, 0},
};
#define N_DEVS (sizeof k_devs / sizeof k_devs[0])

static hal_bt_event_fn g_cb;
static void *g_ctx;
static hal_bt_state_t g_state = HAL_BT_OFF;
static uint32_t g_t0;            /* start of bring-up or of the current scan */
static uint32_t g_reported;      /* devices reported so far in this scan */
static bool g_named[N_DEVS];

static void emit_state(hal_bt_state_t s) {
    g_state = s;
    if (g_cb != NULL) {
        hal_bt_event_t ev = {.type = HAL_BT_EVT_STATE, .state = s};
        g_cb(&ev, g_ctx);
    }
}

static void emit_dev(uint32_t i, bool with_name) {
    if (g_cb == NULL) {
        return;
    }
    const sim_dev_t *d = &k_devs[i];
    hal_bt_event_t ev = {.type = HAL_BT_EVT_DEVICE, .state = g_state};
    memcpy(ev.dev.addr, d->addr, 6);
    ev.dev.cod = d->cod;
    ev.dev.rssi = d->rssi;
    ev.dev.audio_sink = ((d->cod >> 8) & 0x1Fu) == HAL_BT_COD_MAJOR_AUDIO_VIDEO;
    if (with_name && d->name != NULL) {
        strncpy(ev.dev.name, d->name, HAL_BT_NAME_MAX);
    }
    g_cb(&ev, g_ctx);
}

hal_status_t hal_bt_init(hal_bt_event_fn cb, void *ctx, const hal_bt_storage_t *storage) {
    (void)storage;
    if (g_state != HAL_BT_OFF && g_state != HAL_BT_FAILED) {
        return HAL_EBUSY;
    }
    g_cb = cb;
    g_ctx = ctx;
    g_t0 = hal_millis();
    emit_state(HAL_BT_STARTING);
    return HAL_OK;
}

hal_bt_state_t hal_bt_state(void) { return g_state; }

bool hal_bt_local_address(uint8_t addr[6]) {
    static const uint8_t k_local[6] = {0xB8, 0x27, 0xEB, 0x00, 0x00, 0x01};
    if (g_state == HAL_BT_OFF || g_state == HAL_BT_STARTING || g_state == HAL_BT_FAILED) {
        return false;
    }
    memcpy(addr, k_local, 6);
    return true;
}

hal_status_t hal_bt_inquiry_start(uint8_t duration_s) {
    (void)duration_s;
    if (g_state == HAL_BT_SCANNING) {
        return HAL_EBUSY;
    }
    if (g_state != HAL_BT_ON) {
        return HAL_ENODEV;
    }
    g_t0 = hal_millis();
    g_reported = 0;
    memset(g_named, 0, sizeof g_named);
    emit_state(HAL_BT_SCANNING);
    return HAL_OK;
}

void hal_bt_inquiry_stop(void) {
    if (g_state != HAL_BT_SCANNING) {
        return;
    }
    emit_state(HAL_BT_ON);
    if (g_cb != NULL) {
        hal_bt_event_t ev = {.type = HAL_BT_EVT_SCAN_DONE, .state = g_state};
        g_cb(&ev, g_ctx);
    }
}

void hal_bt_poll(void) {
    uint32_t dt = hal_millis() - g_t0;
    if (g_state == HAL_BT_STARTING) {
        if (dt >= SIM_BRING_UP_MS) {
            emit_state(HAL_BT_ON);
        }
        return;
    }
    if (g_state != HAL_BT_SCANNING) {
        return;
    }
    for (uint32_t i = 0; i < N_DEVS; i++) {
        if (i == g_reported && dt >= k_devs[i].at_ms) {
            bool late = k_devs[i].name_at_ms != 0;
            emit_dev(i, !late);
            g_named[i] = !late;
            g_reported++;
        }
        if (g_named[i] == false && i < g_reported && k_devs[i].name_at_ms != 0 && dt >= k_devs[i].name_at_ms) {
            emit_dev(i, true);
            g_named[i] = true;
        }
    }
    if (dt >= SIM_SCAN_MS && g_reported == N_DEVS) {
        hal_bt_inquiry_stop();
    }
}

hal_status_t hal_radio_a2dp_connect(const uint8_t addr[6]) {
    (void)addr;
    return HAL_ENOTSUP; /* stage 2 */
}

void hal_radio_a2dp_disconnect(void) {}

/* ============================ Wi-Fi simulation ============================ */
/* Scan "finds" five networks after a moment; a join succeeds for any of them with a non-empty password except "wrongpass"
 * (bad password); a network that is not in the list fails. DHCP and SNTP answer shortly after. Events/callbacks happen in
 * hal_wifi_poll(), i.e. on the calling task. */
#include <stdio.h>

static const hal_wifi_ap_t k_aps[] = {
    {"HomeNet", {0x10, 0x20, 0x30, 0x40, 0x50, 0x60}, -48, 6, HAL_WIFI_SEC_WPA2_WPA3},
    {"BT-Hub-7F2K", {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, -63, 1, HAL_WIFI_SEC_WPA2_WPA3},
    {"Cafe Guest", {0x12, 0x22, 0x33, 0x44, 0x55, 0x77}, -71, 11, HAL_WIFI_SEC_OPEN},
    {"VM-Office", {0x13, 0x22, 0x33, 0x44, 0x55, 0x88}, -80, 3, HAL_WIFI_SEC_OTHER},
    {"Neighbour's WiFi", {0x14, 0x22, 0x33, 0x44, 0x55, 0x99}, -88, 9, HAL_WIFI_SEC_WPA2_WPA3},
};
#define N_APS (sizeof k_aps / sizeof k_aps[0])

static bool g_wifi_on;
static hal_wifi_status g_wstate = HAL_WIFI_DOWN;
static uint32_t g_scan_t0, g_scan_given;
static bool g_scanning;
static char g_wssid[HAL_WIFI_SSID_MAX + 1];
static uint32_t g_join_t0;
static bool g_join_ok;
static hal_wifi_status g_join_end;
static hal_wifi_time_fn g_wtime_cb;
static uint32_t g_sntp_t0;
static bool g_sntp_on, g_sntp_done;
static hal_http_data_fn g_hd;
static hal_http_done_fn g_hdone;
static void *g_hctx;
static uint32_t g_h_t0, g_h_sent;
#define SIM_HTTP_BYTES (2u * 1024u * 1024u)

hal_status_t hal_wifi_start(void) {
    g_wifi_on = true;
    if (g_wstate == HAL_WIFI_DOWN) {
        g_wstate = HAL_WIFI_IDLE;
    }
    return HAL_OK;
}

void hal_wifi_stop(void) {
    g_wifi_on = false;
    g_wstate = HAL_WIFI_DOWN;
    g_scanning = false;
    g_sntp_on = false;
}

hal_wifi_status hal_wifi_state(void) { return g_wstate; }

hal_status_t hal_wifi_scan_start(void) {
    if (!g_wifi_on) {
        return HAL_ENODEV;
    }
    if (g_scanning) {
        return HAL_EBUSY;
    }
    g_scanning = true;
    g_scan_t0 = hal_millis();
    g_scan_given = 0;
    return HAL_OK;
}

bool hal_wifi_scan_next(hal_wifi_ap_t *out, bool *done) {
    uint32_t dt = hal_millis() - g_scan_t0;
    uint32_t avail = dt / 250u;
    if (avail > N_APS) {
        avail = N_APS;
    }
    if (g_scanning && g_scan_given < avail) {
        *out = k_aps[g_scan_given++];
        *done = false;
        return true;
    }
    if (g_scanning && g_scan_given == N_APS) {
        g_scanning = false;
    }
    *done = !g_scanning;
    return false;
}

hal_status_t hal_wifi_join(const char *ssid, const char *psk) {
    if (!g_wifi_on) {
        return HAL_ENODEV;
    }
    if (ssid == NULL || ssid[0] == '\0') {
        return HAL_EINVAL;
    }
    snprintf(g_wssid, sizeof g_wssid, "%s", ssid);
    bool known = false, open = false;
    for (uint32_t i = 0; i < N_APS; i++) {
        if (strcmp(ssid, k_aps[i].ssid) == 0) {
            known = true;
            open = k_aps[i].sec == HAL_WIFI_SEC_OPEN;
        }
    }
    g_join_ok = known && (open || (psk != NULL && psk[0] != '\0' && strcmp(psk, "wrongpass") != 0));
    g_join_end = !known ? HAL_WIFI_FAILED : g_join_ok ? HAL_WIFI_UP : HAL_WIFI_BAD_AUTH;
    g_join_t0 = hal_millis();
    g_wstate = HAL_WIFI_JOINING;
    return HAL_OK;
}

void hal_wifi_leave(void) {
    if (g_wifi_on) {
        g_wstate = HAL_WIFI_IDLE;
        g_sntp_done = false;
    }
}

int hal_wifi_rssi(void) { return g_wstate == HAL_WIFI_UP ? -55 : 0; }

bool hal_wifi_ip(char out[16]) {
    if (g_wstate != HAL_WIFI_UP) {
        return false;
    }
    snprintf(out, 16, "192.168.1.42");
    return true;
}

bool hal_wifi_ssid(char out[33]) {
    if (!g_wifi_on || g_wssid[0] == '\0') {
        return false;
    }
    memcpy(out, g_wssid, sizeof g_wssid);
    return true;
}

hal_status_t hal_wifi_sntp_start(const char *server, hal_wifi_time_fn cb) {
    (void)server;
    g_wtime_cb = cb;
    g_sntp_on = true;
    g_sntp_done = false;
    g_sntp_t0 = hal_millis();
    return HAL_OK;
}

void hal_wifi_sntp_stop(void) { g_sntp_on = false; }

hal_status_t hal_wifi_http_get(const char *host, uint16_t port, const char *path, hal_http_data_fn on_data, hal_http_done_fn on_done, void *ctx) {
    (void)host; (void)port; (void)path;
    if (g_wstate != HAL_WIFI_UP) {
        return HAL_ENODEV;
    }
    if (g_hdone != NULL) {
        return HAL_EBUSY;
    }
    g_hd = on_data;
    g_hdone = on_done;
    g_hctx = ctx;
    g_h_t0 = hal_millis();
    g_h_sent = 0;
    return HAL_OK;
}

void hal_wifi_http_abort(void) { g_h_sent = SIM_HTTP_BYTES; }

void hal_wifi_mem(hal_wifi_mem_t *o) {
    memset(o, 0, sizeof *o);
    o->mem_used = 1200; o->mem_max = 2600; o->mem_size = 6000; o->pbuf_used = 1; o->pbuf_max = 5; o->pbuf_size = 12;
}

void hal_wifi_poll(void) {
    uint32_t now = hal_millis();
    if (g_wstate == HAL_WIFI_JOINING && now - g_join_t0 >= 800u) {
        g_wstate = g_join_end;
        if (g_wstate == HAL_WIFI_UP) {
            g_sntp_t0 = now;
        }
    }
    if (g_wstate == HAL_WIFI_UP && g_sntp_on && !g_sntp_done && now - g_sntp_t0 >= 500u) {
        g_sntp_done = true;
        if (g_wtime_cb != NULL) {
            g_wtime_cb(1793655000u + now / 1000u); /* 2026-11-02 21:30 UTC plus uptime, on a PC with no clock to trust */
        }
    }
    if (g_hdone != NULL) { /* 1 MB/s simulated transfer of 2 MB */
        static uint8_t chunk[1024];
        uint32_t want = (now - g_h_t0) * 1024u; /* bytes so far */
        while (g_h_sent < want && g_h_sent < SIM_HTTP_BYTES) {
            if (g_hd != NULL) {
                g_hd(g_hctx, chunk, sizeof chunk);
            }
            g_h_sent += sizeof chunk;
        }
        if (g_h_sent >= SIM_HTTP_BYTES) {
            hal_http_done_fn d = g_hdone;
            g_hdone = NULL;
            d(g_hctx, 0, g_h_sent);
        }
    }
}

void hal_host_wifi_drop_link(void) {
    if (g_wstate == HAL_WIFI_UP) {
        g_wstate = HAL_WIFI_IDLE; /* what the hardware HAL reports after the driver drops the link */
        g_sntp_done = false;
    }
}
