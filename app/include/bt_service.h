// SPDX-License-Identifier: MPL-2.0
/**
 * @file bt_service.h
 * @brief Bluetooth for the app: starts the radio stack, and hands scan results to the UI through a queue.
 *
 * The HAL (hal_radio.h) reports events from the radio worker task; this module turns them into queue items the UI task
 * polls (the UI never calls into the radio stack from its own context except the non-blocking start/stop of a scan).
 * Stage 1 (docs/bluetooth.md): bring-up and discovery. Stage 2a: pairing and the one remembered headset (bt_saved.h): the Pair /
 * Connect / Disconnect / Forget actions of Settings > Bluetooth. Audio is stage 2b.
 */
#ifndef NEUPOD_BT_SERVICE_H
#define NEUPOD_BT_SERVICE_H

#include <stdbool.h>

#include "hal_radio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_SCAN_SECONDS 10u /**< length of one inquiry */

/** Create the event queue (before the scheduler). */
void bt_service_init(void);

/**
 * Power the Bluetooth controller on and give the stack the SD-backed storage for link keys (radio boot check, after the radio
 * driver is up). Returns once requested; hal_bt_state() reports the outcome.
 */
hal_status_t bt_service_start(void);

/** Next event from the radio (state change, device found or named, scan finished), if any. */
bool bt_service_event_poll(hal_bt_event_t *out);
/** Drop everything queued (a new scan starts from nothing). */
void bt_service_event_flush(void);

/**
 * The Pair action: pair with @p dev (a device from the Devices list) and connect. If it ends CONNECTED the device becomes the
 * remembered headset (replacing any other, whose link key is deleted); if it fails or is cancelled nothing is remembered.
 * Same return codes as hal_bt_pair() (HAL_EBUSY while scanning or while a link exists / is being set up).
 */
hal_status_t bt_service_pair(const hal_bt_device_t *dev);
/** Connect to the remembered headset (HAL_ENODEV if there is none). Never pairs: a headset that lost its key fails with HAL_BT_FAIL_AUTH. */
hal_status_t bt_service_connect(void);
/** Cancel a Pair / Connect in progress, or drop the link. */
void bt_service_disconnect(void);
/** Forget the remembered headset: drop the link, delete its link key, delete the record. */
void bt_service_forget(void);

/** The headset is being reconnected automatically (after a lost link, or at power-up): "Reconnecting..." (bt_reconnect.h). */
bool bt_service_reconnecting(void);
/** Counts the times the automatic reconnect gave up (5 minutes of attempts, headset still remembered, the user did nothing): the UI says "Couldn't reconnect - paused". */
uint32_t bt_service_gave_up_seq(void);

/** Counts the times a connected headset was lost and the automatic reconnect started (not at power-up): the UI says "<name> disconnected - reconnecting". */
uint32_t bt_service_lost_seq(void);
/** Counts the times the automatic reconnect (after a loss or at power-up) ended with the headset connected again: the UI says "<name> connected". */
uint32_t bt_service_reconnected_seq(void);

/** Advance the simulator on the host; no-op on hardware. Cheap; called from the UI tick. */
void bt_service_tick(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BT_SERVICE_H
