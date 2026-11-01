// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_radio.h
 * @brief Wi-Fi and Bluetooth (provisional — shape will change once A2DP work starts).
 */
#ifndef NEUPOD_HAL_RADIO_H
#define NEUPOD_HAL_RADIO_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_WIFI_DOWN = 0,
    HAL_WIFI_CONNECTING,
    HAL_WIFI_UP,
    HAL_WIFI_FAILED,
} hal_wifi_status;

/** Power up the radio(s). */
hal_status_t hal_radio_init(void);

/** Start a station-mode connection; poll hal_radio_wifi_status() for the result. */
hal_status_t hal_radio_wifi_connect(const char *ssid, const char *psk);

/** Current Wi-Fi link state. */
hal_wifi_status hal_radio_wifi_status(void);

/** Disconnect and power down Wi-Fi. */
void hal_radio_wifi_disconnect(void);

/**
 * Connect to an A2DP sink by Bluetooth address (6 bytes, MSB first).
 * Audio then flows via a hal_audio sink selected by the app.
 */
hal_status_t hal_radio_a2dp_connect(const uint8_t addr[6]);

/** Disconnect the A2DP sink. */
void hal_radio_a2dp_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_RADIO_H
