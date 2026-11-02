// SPDX-License-Identifier: MPL-2.0
/*
 * Wi-Fi station (hal_radio.h) on the CYW43439 with lwIP, RTOS build only. Everything runs through the SDK's lwIP-on-FreeRTOS
 * cyw43 arch: calls from tasks are bracketed with cyw43_arch_lwip_begin()/end(); lwIP callbacks run on the radio worker, the
 * same thread BTstack uses (docs/wifi.md, "Coexistence").
 *
 * WPA3: the driver can join WPA3-SAE, but its scan result cannot tell WPA3 from WPA2 (both show an RSN element), so a join
 * tries WPA2, then WPA2/WPA3 mixed, then WPA3-only, moving on when an attempt fails (hal_wifi_poll() steps it).
 */
#include <stdio.h>
#include <string.h>

#include "hal_radio.h"
#include "hal_system.h"
#include "lwip/apps/http_client.h"
#include "lwip/apps/sntp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/stats.h"
#include "pico/cyw43_arch.h"

#define SCAN_MAX 16u

static bool g_on;
static bool g_joining;
static hal_wifi_status g_result = HAL_WIFI_IDLE; /* outcome of the last attempt when not joining */

/* ---- scan ---- */
static hal_wifi_ap_t g_scan[SCAN_MAX];
static uint32_t g_scan_n, g_scan_read;
static bool g_scan_running;

static int scan_cb(void *env, const cyw43_ev_scan_result_t *r) {
    (void)env;
    if (r->ssid_len == 0 || r->ssid_len > HAL_WIFI_SSID_MAX) {
        return 0; /* hidden network: found by typing its name ("Other network...") */
    }
    for (uint32_t i = 0; i < g_scan_n; i++) { /* the same network on several access points: keep the strongest */
        if (strlen(g_scan[i].ssid) == r->ssid_len && memcmp(g_scan[i].ssid, r->ssid, r->ssid_len) == 0) {
            if (r->rssi > g_scan[i].rssi) {
                g_scan[i].rssi = (int8_t)r->rssi;
                g_scan[i].channel = (uint8_t)r->channel;
                memcpy(g_scan[i].bssid, r->bssid, 6);
            }
            return 0;
        }
    }
    if (g_scan_n >= SCAN_MAX) {
        return 0;
    }
    hal_wifi_ap_t *a = &g_scan[g_scan_n++];
    memset(a, 0, sizeof *a);
    memcpy(a->ssid, r->ssid, r->ssid_len);
    memcpy(a->bssid, r->bssid, 6);
    a->rssi = (int8_t)r->rssi;
    a->channel = (uint8_t)r->channel;
    a->sec = r->auth_mode == 0 ? HAL_WIFI_SEC_OPEN : (r->auth_mode & 4u) ? HAL_WIFI_SEC_WPA2_WPA3 : HAL_WIFI_SEC_OTHER; /* WPA3 is not distinguishable here */
    return 0;
}

hal_status_t hal_wifi_start(void) {
    if (g_on) {
        return HAL_OK;
    }
    cyw43_arch_lwip_begin();
    cyw43_arch_enable_sta_mode();
    cyw43_arch_lwip_end();
    g_on = true;
    g_result = HAL_WIFI_IDLE;
    return HAL_OK;
}

void hal_wifi_stop(void) {
    if (!g_on) {
        return;
    }
    hal_wifi_sntp_stop();
    cyw43_arch_lwip_begin();
    cyw43_arch_disable_sta_mode();
    cyw43_arch_lwip_end();
    g_on = false;
    g_joining = false;
    g_scan_running = false;
}

hal_status_t hal_wifi_scan_start(void) {
    if (!g_on) {
        return HAL_ENODEV;
    }
    if (g_scan_running || cyw43_wifi_scan_active(&cyw43_state)) {
        return HAL_EBUSY;
    }
    g_scan_n = g_scan_read = 0;
    cyw43_wifi_scan_options_t opts = {0};
    cyw43_arch_lwip_begin();
    int rc = cyw43_wifi_scan(&cyw43_state, &opts, NULL, scan_cb);
    cyw43_arch_lwip_end();
    g_scan_running = rc == 0;
    return rc == 0 ? HAL_OK : HAL_ERR;
}

bool hal_wifi_scan_next(hal_wifi_ap_t *out, bool *done) {
    if (g_scan_read < g_scan_n) {
        *out = g_scan[g_scan_read++];
        *done = false;
        return true;
    }
    if (g_scan_running && !cyw43_wifi_scan_active(&cyw43_state)) {
        g_scan_running = false;
    }
    *done = !g_scan_running;
    return false;
}

/* ---- join ---- */
static char g_ssid[HAL_WIFI_SSID_MAX + 1], g_psk[HAL_WIFI_PSK_MAX + 1];
static uint8_t g_attempt;
static const uint32_t k_auth[] = {CYW43_AUTH_WPA2_AES_PSK, CYW43_AUTH_WPA3_WPA2_AES_PSK, CYW43_AUTH_WPA3_SAE_AES_PSK};

static void begin_attempt(void) {
    uint32_t auth = g_psk[0] == '\0' ? CYW43_AUTH_OPEN : k_auth[g_attempt];
    cyw43_arch_lwip_begin();
    int rc = cyw43_arch_wifi_connect_async(g_ssid, g_psk[0] != '\0' ? g_psk : NULL, auth);
    cyw43_arch_lwip_end();
    if (rc != 0) {
        g_joining = false;
        g_result = HAL_WIFI_FAILED;
    }
}

hal_status_t hal_wifi_join(const char *ssid, const char *psk) {
    if (!g_on) {
        return HAL_ENODEV;
    }
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) > HAL_WIFI_SSID_MAX || (psk != NULL && strlen(psk) > HAL_WIFI_PSK_MAX)) {
        return HAL_EINVAL;
    }
    snprintf(g_ssid, sizeof g_ssid, "%s", ssid);
    snprintf(g_psk, sizeof g_psk, "%s", psk != NULL ? psk : "");
    g_attempt = 0;
    g_joining = true;
    g_result = HAL_WIFI_JOINING;
    begin_attempt();
    return g_joining ? HAL_OK : HAL_ERR;
}

void hal_wifi_leave(void) {
    if (!g_on) {
        return;
    }
    g_joining = false;
    g_result = HAL_WIFI_IDLE;
    cyw43_arch_lwip_begin();
    (void)cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    cyw43_arch_lwip_end();
}

hal_wifi_status hal_wifi_state(void) {
    if (!g_on) {
        return HAL_WIFI_DOWN;
    }
    int l = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (l == CYW43_LINK_UP) {
        return HAL_WIFI_UP;
    }
    if (g_joining) {
        return l == CYW43_LINK_NOIP || l == CYW43_LINK_JOIN ? HAL_WIFI_NO_IP : HAL_WIFI_JOINING;
    }
    if (l == CYW43_LINK_NOIP || l == CYW43_LINK_JOIN) {
        return HAL_WIFI_NO_IP;
    }
    return g_result;
}

void hal_wifi_poll(void) {
    if (!g_on || !g_joining) {
        return;
    }
    int l = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (l == CYW43_LINK_UP) {
        g_joining = false;
        g_result = HAL_WIFI_UP;
    } else if (l == CYW43_LINK_FAIL || l == CYW43_LINK_NONET || l == CYW43_LINK_BADAUTH) {
        if (l != CYW43_LINK_NONET && g_psk[0] != '\0' && g_attempt + 1u < sizeof k_auth / sizeof k_auth[0]) {
            g_attempt++; /* maybe a WPA3 network: try the next security mode */
            begin_attempt();
        } else {
            g_joining = false;
            g_result = l == CYW43_LINK_BADAUTH ? HAL_WIFI_BAD_AUTH : HAL_WIFI_FAILED;
        }
    }
}

int hal_wifi_rssi(void) {
    int32_t r = 0;
    if (!g_on || cyw43_wifi_get_rssi(&cyw43_state, &r) != 0) {
        return 0;
    }
    return (int)r;
}

bool hal_wifi_ip(char out[16]) {
    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    if (!g_on || !netif_is_up(n) || ip4_addr_isany_val(*netif_ip4_addr(n))) {
        return false;
    }
    ip4addr_ntoa_r(netif_ip4_addr(n), out, 16);
    return true;
}

bool hal_wifi_ssid(char out[33]) {
    if (!g_on || g_ssid[0] == '\0') {
        return false;
    }
    memcpy(out, g_ssid, sizeof g_ssid);
    return true;
}

/* ---- SNTP ---- */
static hal_wifi_time_fn g_time_cb;
static bool g_sntp_on;

void neupod_sntp_set_time(uint32_t sec) { /* lwIP's SNTP_SET_SYSTEM_TIME hook, on the radio worker */
    if (g_time_cb != NULL) {
        g_time_cb(sec);
    }
}

hal_status_t hal_wifi_sntp_start(const char *server, hal_wifi_time_fn cb) {
    if (!g_on || server == NULL) {
        return HAL_ENODEV;
    }
    g_time_cb = cb;
    cyw43_arch_lwip_begin();
    if (g_sntp_on) {
        sntp_stop();
    }
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, server); /* the name is not copied by lwIP: the caller keeps it alive (the service's static string) */
    sntp_init();
    cyw43_arch_lwip_end();
    g_sntp_on = true;
    return HAL_OK;
}

void hal_wifi_sntp_stop(void) {
    if (!g_sntp_on) {
        return;
    }
    cyw43_arch_lwip_begin();
    sntp_stop();
    cyw43_arch_lwip_end();
    g_sntp_on = false;
}

/* ---- HTTP GET (diagnostics) ---- */
static hal_http_data_fn g_on_data;
static hal_http_done_fn g_on_done;
static void *g_http_ctx;
static volatile bool g_http_abort;
static uint32_t g_http_total;
static httpc_connection_t g_http_settings;

static err_t http_recv(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err) {
    (void)arg;
    (void)err;
    if (p == NULL) {
        return ERR_OK;
    }
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        g_http_total += q->len;
        if (!g_http_abort && g_on_data != NULL) {
            g_on_data(g_http_ctx, q->payload, q->len);
        }
    }
    altcp_recved(conn, p->tot_len);
    pbuf_free(p);
    return g_http_abort ? ERR_ABRT : ERR_OK;
}

static void http_result(void *arg, httpc_result_t res, u32_t len, u32_t srv, err_t err) {
    (void)arg;
    (void)len;
    int e = g_http_abort ? -1 : res == HTTPC_RESULT_OK && srv == 200 ? 0 : res == HTTPC_RESULT_OK ? (int)srv : 1000 + (int)res + (err != 0 ? 100 : 0);
    hal_http_done_fn done = g_on_done;
    g_on_done = NULL;
    if (done != NULL) {
        done(g_http_ctx, e, g_http_total);
    }
}

hal_status_t hal_wifi_http_get(const char *host, uint16_t port, const char *path, hal_http_data_fn on_data, hal_http_done_fn on_done, void *ctx) {
    if (!g_on || g_on_done != NULL) {
        return g_on ? HAL_EBUSY : HAL_ENODEV;
    }
    g_on_data = on_data;
    g_on_done = on_done;
    g_http_ctx = ctx;
    g_http_total = 0;
    g_http_abort = false;
    memset(&g_http_settings, 0, sizeof g_http_settings);
    g_http_settings.result_fn = http_result;
    cyw43_arch_lwip_begin();
    err_t e = httpc_get_file_dns(host, port, path, &g_http_settings, http_recv, NULL, NULL);
    cyw43_arch_lwip_end();
    if (e != ERR_OK) {
        g_on_done = NULL;
        return HAL_ERR;
    }
    return HAL_OK;
}

void hal_wifi_http_abort(void) { g_http_abort = true; }

void hal_wifi_mem(hal_wifi_mem_t *o) {
    memset(o, 0, sizeof *o);
#if LWIP_STATS && MEM_STATS && MEMP_STATS
    o->mem_used = lwip_stats.mem.used;
    o->mem_max = lwip_stats.mem.max;
    o->mem_size = lwip_stats.mem.avail;
    o->pbuf_used = lwip_stats.memp[MEMP_PBUF_POOL]->used;
    o->pbuf_max = lwip_stats.memp[MEMP_PBUF_POOL]->max;
    o->pbuf_size = lwip_stats.memp[MEMP_PBUF_POOL]->avail;
    o->errors = lwip_stats.mem.err;
    for (int i = 0; i < MEMP_MAX; i++) {
        o->errors += lwip_stats.memp[i]->err;
    }
#endif
}
