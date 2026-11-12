// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 radio. Wi-Fi: there is no Wi-Fi driver we can run, so it is "not present" (hal_wifi_present() is false, every action answers
 * HAL_ENODEV, every state is down, there are no scan results). Bluetooth is real: hal/common/bt_btstack/bt_btstack.c with
 * bt_port_stpbt.c (docs/bluetooth.md), and hal_system_late_init() reports the radio as present only if /dev/stpbt opened.
 */
#include "hal_radio.h"

bool hal_wifi_present(void) { return false; }
hal_status_t hal_wifi_start(void) { return HAL_ENODEV; }
void hal_wifi_stop(void) {}
hal_wifi_status hal_wifi_state(void) { return HAL_WIFI_DOWN; }
hal_status_t hal_wifi_scan_start(void) { return HAL_ENODEV; }

bool hal_wifi_scan_next(hal_wifi_ap_t *out, bool *done) {
    (void)out;
    if (done != NULL) {
        *done = true;
    }
    return false;
}

hal_status_t hal_wifi_join(const char *ssid, const char *psk) {
    (void)ssid;
    (void)psk;
    return HAL_ENODEV;
}

void hal_wifi_leave(void) {}
void hal_wifi_abort_join(void) {}
int hal_wifi_rssi(void) { return HAL_WIFI_RSSI_UNKNOWN; }

bool hal_wifi_ip(char out[16]) {
    out[0] = '\0';
    return false;
}

bool hal_wifi_ssid(char out[33]) {
    out[0] = '\0';
    return false;
}

hal_status_t hal_wifi_sntp_start(const char *server, hal_wifi_time_fn cb) {
    (void)server;
    (void)cb;
    return HAL_ENODEV;
}

void hal_wifi_sntp_stop(void) {}

hal_status_t hal_wifi_http_get(const char *host, uint16_t port, const char *path, hal_http_data_fn on_data,
                               hal_http_done_fn on_done, void *ctx) {
    (void)host;
    (void)port;
    (void)path;
    (void)on_data;
    (void)on_done;
    (void)ctx;
    return HAL_ENODEV;
}

void hal_wifi_http_abort(void) {}

void hal_wifi_mem(hal_wifi_mem_t *out) {
    *out = (hal_wifi_mem_t){0};
}

void hal_wifi_poll(void) {}
