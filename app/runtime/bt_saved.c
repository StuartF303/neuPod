// SPDX-License-Identifier: MPL-2.0
#include "bt_saved.h"

#include <string.h>

#include "settings_store.h"

static hal_bt_device_t g_pending;
static volatile bool g_have_pending;
/* Addresses whose link key must be deleted (a replaced headset, a Pair that did not finish, a record that could not be saved). A small ring:
 * a Pair can end and the next begin before the UI tick that drains it. Written by on_event (radio worker or UI task), drained by the
 * UI task, so a plain head / tail with the worker as the only producer. */
#define STALE_MAX 4u
static uint8_t g_stale[STALE_MAX][6];
static volatile uint32_t g_stale_head, g_stale_tail;
static volatile bt_saved_notice_t g_notice;

static void stale_push(const uint8_t addr[6]) {
    uint32_t h = g_stale_head;
    for (uint32_t i = g_stale_tail; i != h; i++) {
        if (memcmp(g_stale[i % STALE_MAX], addr, 6) == 0) {
            return; /* already queued */
        }
    }
    if (h - g_stale_tail >= STALE_MAX) {
        g_stale_tail++; /* full: the oldest goes (four unprocessed forgets in a row is not a real case; the key it names is rotated out by BTstack's LRU) */
    }
    memcpy(g_stale[h % STALE_MAX], addr, 6);
    g_stale_head = h + 1u;
}

static bool addr_empty(const uint8_t a[6]) {
    static const uint8_t zero[6];
    return memcmp(a, zero, 6) == 0;
}

bool bt_saved_get(hal_bt_device_t *out) {
    uint8_t v[BT_SAVED_MAX_LEN + 1u];
    uint32_t len = settings_get(SETTING_BT_DEVICE, v, sizeof v);
    if (len < BT_SAVED_MIN_LEN || len > BT_SAVED_MAX_LEN || v[0] != BT_SAVED_VERSION) {
        return false; /* absent, damaged, or written by a newer layout */
    }
    hal_bt_device_t d;
    memset(&d, 0, sizeof d);
    memcpy(d.addr, &v[1], 6);
    if (addr_empty(d.addr)) {
        return false;
    }
    d.cod = (uint32_t)v[7] | (uint32_t)v[8] << 8 | (uint32_t)v[9] << 16;
    d.audio_sink = ((d.cod >> 8) & 0x1Fu) == HAL_BT_COD_MAJOR_AUDIO_VIDEO;
    d.rssi = HAL_BT_RSSI_UNKNOWN;
    uint32_t n = len - BT_SAVED_MIN_LEN;
    for (uint32_t i = 0; i < n; i++) { /* a name came from a remote device and then from a file: no control bytes, no NUL */
        d.name[i] = v[BT_SAVED_MIN_LEN + i] < 0x20u ? '?' : (char)v[BT_SAVED_MIN_LEN + i];
    }
    d.name[n] = '\0';
    if (out != NULL) {
        *out = d;
    }
    return true;
}

bool bt_saved_put(const hal_bt_device_t *dev) {
    if (dev == NULL || addr_empty(dev->addr)) {
        return false;
    }
    uint8_t v[BT_SAVED_MAX_LEN];
    v[0] = BT_SAVED_VERSION;
    memcpy(&v[1], dev->addr, 6);
    v[7] = (uint8_t)(dev->cod & 0xFFu);
    v[8] = (uint8_t)((dev->cod >> 8) & 0xFFu);
    v[9] = (uint8_t)((dev->cod >> 16) & 0xFFu);
    uint32_t n = 0;
    while (n < HAL_BT_NAME_MAX && dev->name[n] != '\0') {
        v[BT_SAVED_MIN_LEN + n] = (uint8_t)dev->name[n];
        n++;
    }
    return settings_put(SETTING_BT_DEVICE, v, BT_SAVED_MIN_LEN + n);
}

void bt_saved_clear(void) { settings_delete(SETTING_BT_DEVICE); }

void bt_saved_pair_begin(const hal_bt_device_t *dev) {
    g_pending = *dev;
    g_have_pending = true;
    g_notice = BT_SAVED_NOTICE_NONE;
}

bt_saved_notice_t bt_saved_notice(void) { return g_notice; }

void bt_saved_pair_abort(void) { g_have_pending = false; }

void bt_saved_on_event(const hal_bt_event_t *ev) {
    if (ev->type != HAL_BT_EVT_LINK) {
        return;
    }
    switch (ev->link) {
    case HAL_BT_LINK_CONNECTED:
        if (g_have_pending && memcmp(ev->dev.addr, g_pending.addr, 6) == 0) {
            hal_bt_device_t old;
            bool had_old = bt_saved_get(&old);
            if (!bt_saved_put(&g_pending)) {
                /* No room: the headset cannot be remembered, so its key must not stay either. Queueing it for deletion also drops the link
                 * (it is the current peer) on the next tick; the screens say why. */
                stale_push(g_pending.addr);
                g_notice = BT_SAVED_NOTICE_FULL;
            } else if (had_old && memcmp(old.addr, g_pending.addr, 6) != 0) {
                stale_push(old.addr);
            }
            g_have_pending = false;
        }
        break;
    case HAL_BT_LINK_FAILED:
    case HAL_BT_LINK_DISCONNECTED:
        if (g_have_pending) {
            /* The Pair did not finish (or was cancelled after the key was made): do not leave a key for a device nobody remembers. */
            hal_bt_device_t cur;
            if (!bt_saved_get(&cur) || memcmp(cur.addr, g_pending.addr, 6) != 0) {
                stale_push(g_pending.addr);
            } else {
                /* Re-pairing the remembered headset: BTstack deletes its old key when the bonding starts, so after a failure or a cancel the
                 * record would name a headset without a key. Forget the record too. */
                bt_saved_clear();
            }
            g_have_pending = false;
        }
        break;
    default:
        break;
    }
}

void bt_saved_queue_delete(const uint8_t addr[6]) { stale_push(addr); }

bool bt_saved_peek_stale(uint8_t addr[6]) {
    if (g_stale_tail == g_stale_head) {
        return false;
    }
    memcpy(addr, g_stale[g_stale_tail % STALE_MAX], 6);
    return true;
}

void bt_saved_drop_stale(void) {
    if (g_stale_tail != g_stale_head) {
        g_stale_tail++;
    }
}

hal_status_t bt_saved_delete_key(const uint8_t addr[6]) {
    hal_status_t st = hal_bt_forget(addr);
    if (st == HAL_ENODEV) {
        /* The stack is not up (it never started, or failed): BTstack reads its keys from the store on demand, so deleting the record there is
         * what forgetting means. */
        hal_bt_storage_delete_key(settings_bt_storage(), addr);
        return HAL_OK;
    }
    return st; /* HAL_EBUSY: ask again */
}

void bt_saved_drain_stale(void) {
    uint8_t a[6];
    while (bt_saved_peek_stale(a)) {
        if (bt_saved_delete_key(a) != HAL_OK) {
            break; /* the radio's request slots are full: the address stays queued and the next tick tries again */
        }
        bt_saved_drop_stale();
    }
}
