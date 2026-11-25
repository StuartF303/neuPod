// SPDX-License-Identifier: MPL-2.0
#include "radio.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "results.h"

static const char *TAG = "radio";
static volatile bool s_busy;

bool radio_busy(void) { return s_busy; }

// ---------------- Wi-Fi ----------------

#define WIFI_SCAN_DONE_BIT BIT0
#define MAX_AP 64

static EventGroupHandle_t s_wifi_ev;

static void wifi_ev_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_SCAN_DONE) xEventGroupSetBits(s_wifi_ev, WIFI_SCAN_DONE_BIT);
}

static const char *auth_name(wifi_auth_mode_t a)
{
    switch (a) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ent";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "other";
    }
}

// Runs one scan (blocking up to 15 s). Returns the record count, or -1 on failure.
static int wifi_scan_once(wifi_scan_type_t type, wifi_ap_record_t *recs)
{
    wifi_scan_config_t sc = { .show_hidden = true, .scan_type = type };
    sc.scan_time.active.min = 100;
    sc.scan_time.active.max = 300;
    sc.scan_time.passive = 360;
    xEventGroupClearBits(s_wifi_ev, WIFI_SCAN_DONE_BIT);
    if (esp_wifi_scan_start(&sc, false) != ESP_OK) return -1;
    if (!(xEventGroupWaitBits(s_wifi_ev, WIFI_SCAN_DONE_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(15000)) & WIFI_SCAN_DONE_BIT)) {
        esp_wifi_scan_stop();
        return -1;
    }
    uint16_t n = MAX_AP;
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) return -1;
    return n;
}

static void wifi_probe(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    s_wifi_ev = xEventGroupCreate();
    esp_netif_init();
    esp_err_t ee = esp_event_loop_create_default();
    if (e != ESP_OK || (ee != ESP_OK && ee != ESP_ERR_INVALID_STATE)) {
        res_set(R_WIFI, ST_FAIL, "nvs/event loop init: %s / %s", esp_err_to_name(e), esp_err_to_name(ee));
        return;
    }
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_ap_record_t *recs = calloc(MAX_AP, sizeof *recs);
    esp_event_handler_instance_t inst = NULL;
    e = recs ? esp_wifi_init(&cfg) : ESP_ERR_NO_MEM;
    if (e != ESP_OK) {
        res_set(R_WIFI, ST_FAIL, "esp_wifi_init: %s", esp_err_to_name(e));
        free(recs);
        return;
    }
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_ev_handler, NULL, &inst);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    e = esp_wifi_start();   // no esp_wifi_connect() anywhere: scan only
    if (e != ESP_OK) {
        res_set(R_WIFI, ST_FAIL, "esp_wifi_start: %s", esp_err_to_name(e));
    } else {
        int64_t t0 = esp_timer_get_time();
        int n = wifi_scan_once(WIFI_SCAN_TYPE_ACTIVE, recs);
        int64_t ms = (esp_timer_get_time() - t0) / 1000;
        if (n < 0) {
            res_set(R_WIFI, ST_FAIL, "active scan failed or timed out");
        } else {
            for (int i = 0; i < n; i++)          // sort by RSSI, strongest first
                for (int j = i + 1; j < n; j++)
                    if (recs[j].rssi > recs[i].rssi) { wifi_ap_record_t t = recs[i]; recs[i] = recs[j]; recs[j] = t; }
            printf("--- Wi-Fi active scan: %d networks in %lld ms, top 10 by RSSI ---\n", n, (long long)ms);
            for (int i = 0; i < n && i < 10; i++)
                printf("  %2d. %-32s ch %2d  %4d dBm  %s\n", i + 1, recs[i].ssid[0] ? (char *)recs[i].ssid : "(hidden)",
                       recs[i].primary, recs[i].rssi, auth_name(recs[i].authmode));
            int strongest = n ? recs[0].rssi : 0;
            int np = wifi_scan_once(WIFI_SCAN_TYPE_PASSIVE, recs);
            res_set(R_WIFI, n > 0 ? ST_PASS : ST_INFO, "active scan %d networks in %lld ms (strongest %d dBm); passive scan %d; radio off",
                    n, (long long)ms, strongest, np);
        }
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    if (inst) esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, inst);
    if (netif) esp_netif_destroy_default_wifi(netif);
    free(recs);
}

// ---------------- BLE ----------------

#define BLE_SYNC_BIT BIT0
#define BLE_DONE_BIT BIT1
#define MAX_DEV 64
#define BLE_SCAN_MS 10000

typedef struct { uint8_t type; uint8_t val[6]; int8_t rssi; char name[24]; } ble_dev_t;

static EventGroupHandle_t s_ble_ev;
static ble_dev_t s_dev[MAX_DEV];
static int s_ndev;
static uint32_t s_adv_events;

static int ble_gap_cb(struct ble_gap_event *ev, void *arg)
{
    if (ev->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *d = &ev->disc;
        s_adv_events++;
        int i;
        for (i = 0; i < s_ndev; i++)
            if (s_dev[i].type == d->addr.type && !memcmp(s_dev[i].val, d->addr.val, 6)) break;
        if (i == s_ndev) {
            if (s_ndev >= MAX_DEV) return 0;
            s_ndev++;
            s_dev[i].type = d->addr.type;
            memcpy(s_dev[i].val, d->addr.val, 6);
            s_dev[i].rssi = d->rssi;
            s_dev[i].name[0] = 0;
        } else if (d->rssi > s_dev[i].rssi) {
            s_dev[i].rssi = d->rssi;
        }
        struct ble_hs_adv_fields f;
        if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) == 0 && f.name && f.name_len) {
            size_t l = f.name_len < sizeof s_dev[i].name - 1 ? f.name_len : sizeof s_dev[i].name - 1;
            memcpy(s_dev[i].name, f.name, l);
            s_dev[i].name[l] = 0;
        }
    } else if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        xEventGroupSetBits(s_ble_ev, BLE_DONE_BIT);
    }
    return 0;
}

static void ble_on_sync(void) { xEventGroupSetBits(s_ble_ev, BLE_SYNC_BIT); }

static void ble_host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_probe(void)
{
    s_ble_ev = xEventGroupCreate();
    s_ndev = 0;
    s_adv_events = 0;
    esp_err_t e = nimble_port_init();
    if (e != ESP_OK) {
        res_set(R_BLE, ST_FAIL, "nimble_port_init: %s", esp_err_to_name(e));
        return;
    }
    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(ble_host_task);
    if (!(xEventGroupWaitBits(s_ble_ev, BLE_SYNC_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000)) & BLE_SYNC_BIT)) {
        res_set(R_BLE, ST_FAIL, "host did not sync within 5 s");
    } else {
        uint8_t own = BLE_OWN_ADDR_PUBLIC;
        struct ble_gap_disc_params dp = { .passive = 1, .filter_duplicates = 0 };
        int rc = ble_gap_disc(own, BLE_SCAN_MS, &dp, ble_gap_cb, NULL);
        if (rc != 0) {
            res_set(R_BLE, ST_FAIL, "ble_gap_disc rc=%d", rc);
        } else {
            bool done = xEventGroupWaitBits(s_ble_ev, BLE_DONE_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(BLE_SCAN_MS + 4000)) & BLE_DONE_BIT;
            if (!done) ble_gap_disc_cancel();
            int n = s_ndev;
            for (int i = 0; i < n; i++)
                for (int j = i + 1; j < n; j++)
                    if (s_dev[j].rssi > s_dev[i].rssi) { ble_dev_t t = s_dev[i]; s_dev[i] = s_dev[j]; s_dev[j] = t; }
            printf("--- BLE passive scan: %d devices (%lu advertisements) in %d s, top 10 by RSSI ---\n",
                   n, (unsigned long)s_adv_events, BLE_SCAN_MS / 1000);
            for (int i = 0; i < n && i < 10; i++)
                printf("  %2d. %02X:%02X:%02X:%02X:%02X:%02X  %4d dBm  %s\n", i + 1, s_dev[i].val[5], s_dev[i].val[4],
                       s_dev[i].val[3], s_dev[i].val[2], s_dev[i].val[1], s_dev[i].val[0], s_dev[i].rssi,
                       s_dev[i].name[0] ? s_dev[i].name : "(no name)");
            res_set(R_BLE, (done && n > 0) ? ST_PASS : (done ? ST_INFO : ST_FAIL), "%d devices, %lu advertisements%s; radio off",
                    n, (unsigned long)s_adv_events, done ? "" : " (scan did not complete)");
        }
    }
    if (nimble_port_stop() == 0) nimble_port_deinit();
}

static void radio_task(void *arg)
{
    s_busy = true;
    wifi_probe();
    ble_probe();
    s_busy = false;
    ESP_LOGI(TAG, "radios off");
    vTaskDelete(NULL);
}

void radio_start(void)
{
    xTaskCreate(radio_task, "radio", 8192, NULL, 4, NULL);
}
