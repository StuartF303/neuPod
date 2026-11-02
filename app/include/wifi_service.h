// SPDX-License-Identifier: MPL-2.0
/**
 * @file wifi_service.h
 * @brief Wi-Fi for the app (docs/wifi.md): scan, join, remembered networks, auto-reconnect with backoff, SNTP, power policy.
 *
 * One RTOS task ("wifi", core 0) owns the radio's station side. Other tasks (the UI) post requests and read a snapshot; nothing
 * here blocks them. Passwords are stored sealed (crypto_secret.h) in the settings store; the SSIDs are stored in the clear.
 */
#ifndef NEUPOD_WIFI_SERVICE_H
#define NEUPOD_WIFI_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_radio.h"
#include "sys_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_SAVED 4u
#define WIFI_NTP_MAX 40u

typedef struct {
    bool enabled;          /**< master switch */
    bool during_playback;  /**< keep Wi-Fi on while audio plays (default off: the radio is shared and audio comes first) */
    uint8_t idle_off_min;  /**< switch the station off after this many idle minutes; 0 = never */
    clock_zone_t zone;
    char ntp_server[WIFI_NTP_MAX];
} wifi_settings_t;

typedef struct {
    hal_wifi_status state;
    bool radio_on;
    bool paused_for_playback;
    char ssid[HAL_WIFI_SSID_MAX + 1];
    char ip[16];
    int rssi;
    bool ntp_synced;
    uint32_t reconnects, failures;
    uint32_t saved;
} wifi_info_t;

typedef struct {
    enum { WT_IDLE, WT_RUNNING, WT_DONE, WT_FAILED } state;
    uint32_t bytes, ms, kbps;
    int err;
} wifi_test_t;

void wifi_service_init(void);
void wifi_task(void *arg);

void wifi_get_info(wifi_info_t *out);
void wifi_get_settings(wifi_settings_t *out);
void wifi_set_settings(const wifi_settings_t *in); /**< persists; takes effect within a second */

/** Ask for a scan (switches the radio on if needed). Results are read with wifi_scan_next(). */
bool wifi_scan_request(void);
bool wifi_scan_next(hal_wifi_ap_t *out, bool *done);

/** Join @p ssid. @p psk NULL = use the saved password of that network (or none for an open one). @p save remembers it on success. */
bool wifi_connect_request(const char *ssid, const char *psk, bool save);
void wifi_disconnect_request(void);

uint32_t wifi_saved_count(void);
bool wifi_saved_get(uint32_t i, char ssid[HAL_WIFI_SSID_MAX + 1], bool *has_psk);
void wifi_forget(uint32_t i);
/** The saved password for @p ssid does not decrypt (another board, edited card) or never worked: ask again. */
bool wifi_saved_needs_password(const char *ssid);

/* Throughput test: HTTP GET of the URL in settings (http://host[:port]/path) into /dev/null, one at a time. */
void wifi_test_get_url(char *out, uint32_t cap);
void wifi_test_set_url(const char *url);
bool wifi_test_start(void);
void wifi_test_get(wifi_test_t *out);

/** Boot check: true when the driver/stack is up (not "joined"). */
bool wifi_service_stack_up(void);

#ifdef __cplusplus
}
#endif

#endif
