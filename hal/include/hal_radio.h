// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_radio.h
 * @brief Wi-Fi and Bluetooth.
 *
 * Bluetooth (classic, via the CYW43439): stage 1 of docs/bluetooth.md provides bring-up, the local address and
 * device discovery (inquiry). Pairing and the A2DP source arrive in stage 2; hal_radio_a2dp_connect() is declared
 * for that and not implemented yet. Wi-Fi is not implemented.
 *
 * Threading: every hal_bt_* call may be made from any RTOS task on core 0. The event callback runs on the radio
 * worker (the CYW43 async-context task) with BTstack's lock held: it must be quick, must not block and must not call
 * back into hal_bt_*. Never call any of this from core 1.
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
 * Audio then flows via a hal_audio sink selected by the app. Stage 2: not implemented.
 */
hal_status_t hal_radio_a2dp_connect(const uint8_t addr[6]);

/** Disconnect the A2DP sink. */
void hal_radio_a2dp_disconnect(void);

/* ---- Bluetooth: state, discovery ------------------------------------------------------------------------ */

typedef enum {
    HAL_BT_OFF = 0,  /**< not started */
    HAL_BT_STARTING, /**< controller firmware loading / HCI initialising */
    HAL_BT_ON,       /**< HCI up, idle */
    HAL_BT_SCANNING, /**< inquiry running */
    HAL_BT_FAILED,   /**< the stack or the controller did not come up */
} hal_bt_state_t;

#define HAL_BT_NAME_MAX 32u         /**< bytes kept of a remote name (UTF-8, NUL-terminated copy) */
#define HAL_BT_RSSI_UNKNOWN 127     /**< rssi value when the inquiry result carried none */
#define HAL_BT_COD_MAJOR_AUDIO_VIDEO 0x04u

typedef struct {
    uint8_t addr[6];            /**< MSB first */
    uint32_t cod;               /**< 24-bit Class of Device */
    int8_t rssi;                /**< dBm, HAL_BT_RSSI_UNKNOWN if not reported */
    bool audio_sink;            /**< major class Audio/Video (CoD bits 12..8 == 4): a headset, speaker, car kit ... */
    char name[HAL_BT_NAME_MAX + 1]; /**< "" until the remote name is known */
} hal_bt_device_t;

typedef enum {
    HAL_BT_EVT_STATE,     /**< state changed: ev.state */
    HAL_BT_EVT_DEVICE,    /**< a device was found, or its name arrived: ev.dev (the full record, repeated updates) */
    HAL_BT_EVT_SCAN_DONE, /**< inquiry (and the name requests that followed) finished or was stopped */
} hal_bt_event_type_t;

typedef struct {
    hal_bt_event_type_t type;
    hal_bt_state_t state;
    hal_bt_device_t dev;
} hal_bt_event_t;

typedef void (*hal_bt_event_fn)(const hal_bt_event_t *ev, void *ctx);

/**
 * Persistent tag/value/length storage for the Bluetooth stack (link keys, device database). Same contract as
 * BTstack's btstack_tlv_t, without its types: get returns the number of bytes copied (at most @p size; a NULL
 * @p buf asks for the value length instead), 0 if the tag is absent; put returns 0 on success; del removes the tag.
 * Provided by app/settings (SD-card backed, never the internal flash).
 */
typedef struct {
    int (*get)(void *ctx, uint32_t tag, uint8_t *buf, uint32_t size);
    int (*put)(void *ctx, uint32_t tag, const uint8_t *data, uint32_t size);
    void (*del)(void *ctx, uint32_t tag);
    void *ctx;
} hal_bt_storage_t;

/**
 * Bring Bluetooth up: register @p cb (may be NULL), give the stack its storage (NULL = none: pairing data is then
 * not kept), and power the controller on. Returns once the request is made; HAL_BT_EVT_STATE reports HAL_BT_ON when HCI
 * is up (about a second after the radio driver is). Needs the radio driver initialised (hal_system_late_init()).
 * On the host build this is a simulation.
 */
hal_status_t hal_bt_init(hal_bt_event_fn cb, void *ctx, const hal_bt_storage_t *storage);

hal_bt_state_t hal_bt_state(void);

/** The local Bluetooth device address (MSB first). false until the stack is up. */
bool hal_bt_local_address(uint8_t addr[6]);

/**
 * Start device discovery for @p duration_s seconds (rounded up to 1.28 s units, 1..48 s). Every device found is
 * reported with HAL_BT_EVT_DEVICE (twice for devices whose name had to be requested: first with an empty name),
 * then HAL_BT_EVT_SCAN_DONE. HAL_EBUSY if a scan runs, HAL_ENODEV if the stack is not up.
 */
hal_status_t hal_bt_inquiry_start(uint8_t duration_s);

/** Stop a running inquiry (HAL_BT_EVT_SCAN_DONE follows). No-op when idle. */
void hal_bt_inquiry_stop(void);

/**
 * Give the HAL a chance to advance. Hardware: a no-op, events arrive by themselves from the radio worker. Host
 * simulator: moves the simulated bring-up and scan along (and delivers their events from the calling task). The runtime
 * calls it from the UI tick and the radio boot check; it must be cheap.
 */
void hal_bt_poll(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_RADIO_H
