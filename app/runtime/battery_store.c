// SPDX-License-Identifier: MPL-2.0
#include "battery_store.h"
#include "os.h"

static hal_battery_status_t g_latest;
static bool g_have;
static os_mutex_t *g_lock;

void battery_store_init(void) {
    if (g_lock == NULL) {
        g_lock = os_mutex_create();
    }
}

void battery_store_publish(const hal_battery_status_t *st) {
    if (!st->valid) {
        return;
    }
    if (os_mutex_lock(g_lock, OS_WAIT_FOREVER)) {
        g_latest = *st;
        g_have = true;
        os_mutex_unlock(g_lock);
    }
}

bool battery_store_get(hal_battery_status_t *out) {
    bool have = false;
    if (os_mutex_lock(g_lock, OS_WAIT_FOREVER)) {
        *out = g_latest;
        have = g_have;
        os_mutex_unlock(g_lock);
    }
    return have;
}
