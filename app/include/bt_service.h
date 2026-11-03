// SPDX-License-Identifier: MPL-2.0
/**
 * @file bt_service.h
 * @brief Bluetooth for the app: starts the radio stack, and hands scan results to the UI through a queue.
 *
 * The HAL (hal_radio.h) reports events from the radio worker task; this module turns them into queue items the UI task
 * polls (the UI never calls into the radio stack from its own context except the non-blocking start/stop of a scan).
 * Stage 1 (docs/bluetooth.md): bring-up and discovery only; pairing and A2DP are stage 2.
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

/** Advance the simulator on the host; no-op on hardware. Cheap; called from the UI tick. */
void bt_service_tick(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_BT_SERVICE_H
