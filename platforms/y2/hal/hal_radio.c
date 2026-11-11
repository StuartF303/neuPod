// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 radio: there is no Wi-Fi or Bluetooth chip we can drive, so this is "not present". hal_system_late_init()
 * already reports HAL_ENODEV, which makes the boot check mark both capabilities unavailable (the UI greys the
 * features out); these stubs only keep any stray call safe: every action answers HAL_ENODEV, every state is
 * off/down, there are no scan results and no events.
 */
#include "hal_radio.h"

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

hal_status_t hal_radio_a2dp_connect(const uint8_t addr[6]) {
    (void)addr;
    return HAL_ENODEV;
}

void hal_radio_a2dp_disconnect(void) {}

hal_status_t hal_bt_init(hal_bt_event_fn cb, void *ctx, const hal_bt_storage_t *storage) {
    (void)cb;
    (void)ctx;
    (void)storage;
    return HAL_ENODEV;
}

hal_bt_state_t hal_bt_state(void) { return HAL_BT_OFF; }

bool hal_bt_local_address(uint8_t addr[6]) {
    for (int i = 0; i < 6; i++) {
        addr[i] = 0;
    }
    return false;
}

hal_status_t hal_bt_inquiry_start(uint8_t duration_s) {
    (void)duration_s;
    return HAL_ENODEV;
}

void hal_bt_inquiry_stop(void) {}

hal_status_t hal_bt_pair(const hal_bt_device_t *dev) {
    (void)dev;
    return HAL_ENODEV;
}

hal_status_t hal_bt_connect(const uint8_t addr[6]) {
    (void)addr;
    return HAL_ENODEV;
}

void hal_bt_disconnect(void) {}

hal_status_t hal_bt_forget(const uint8_t addr[6]) {
    (void)addr;
    return HAL_ENODEV;
}

hal_bt_link_state_t hal_bt_link_state(void) { return HAL_BT_LINK_DISCONNECTED; }
uint32_t hal_bt_link_attempt(void) { return 0; }
hal_bt_fail_t hal_bt_link_reason(void) { return HAL_BT_FAIL_NONE; }

bool hal_bt_link_device(hal_bt_device_t *out) {
    (void)out;
    return false;
}

bool hal_bt_accepting(void) { return false; }
void hal_bt_poll(void) {}
