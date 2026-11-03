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
