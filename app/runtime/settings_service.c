// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "nfs.h"
#include "os.h"
#include "player.h"
#include "settings_service.h"
#include "settings_store.h"
#include "storage_service.h"

#define SAVE_DELAY_MS 1500u /* after the first unsaved change: coalesces a burst of wheel clicks into one write */
#define RETRY_MS 5000u      /* after a failed save */

static os_mutex_t *g_mu;

static void lock_fn(void *ctx) {
    (void)ctx;
    (void)os_mutex_lock(g_mu, OS_WAIT_FOREVER);
}
static void unlock_fn(void *ctx) {
    (void)ctx;
    os_mutex_unlock(g_mu);
}

void settings_service_init(void) {
    g_mu = os_mutex_create();
    static const settings_lock_t lk = {.lock = lock_fn, .unlock = unlock_fn, .ctx = NULL};
    settings_init(g_mu != NULL ? &lk : NULL); // without a mutex (out of memory) run unlocked rather than not at all
}

static const char *load_name(settings_load_result_t r) {
    switch (r) {
    case SETTINGS_LOAD_NONE: return "not loaded";
    case SETTINGS_LOAD_FRESH: return "no file yet (defaults)";
    case SETTINGS_LOAD_OK: return "loaded";
    case SETTINGS_LOAD_RECOVERED: return "recovered an interrupted save";
    case SETTINGS_LOAD_CORRUPT: return "file damaged (defaults)";
    case SETTINGS_LOAD_IO_ERROR: return "card read error (RAM-only)";
    }
    return "?";
}

void settings_service_storage_result(bool card_ok) {
    settings_attach_storage(card_ok);
    if (!card_ok) {
        printf("settings: no usable card, settings are RAM-only this session\n");
        return;
    }
    // All card access after boot goes through the storage task (it also does the writes), so the load is queued to it.
    if (!storage_settings_load_request()) {
        settings_attach_storage(false);
        printf("settings: could not queue the load, settings are RAM-only this session\n");
    }
}

void settings_service_load_now(void) {
    settings_load_result_t r = settings_load();
    settings_status_t st;
    settings_get_status(&st);
    printf("settings: %s (%u items, seq %u)\n", load_name(r), (unsigned)st.items, (unsigned)st.seq);
    uint8_t v;
    if (settings_get_u8(SETTING_VOLUME_ATT_DB, &v)) {
        player_restore_volume(v);
    }
}

void settings_service_poll(bool quiet_now) {
    static uint32_t dirty_since, retry_at;
    static bool pending;
    if (!settings_dirty()) {
        pending = false;
        return;
    }
    uint32_t now = os_tick_ms();
    if (!pending) {
        pending = true;
        dirty_since = now;
    }
    if (now - dirty_since < SAVE_DELAY_MS || (int32_t)(now - retry_at) < 0 || !quiet_now) {
        return;
    }
    int e = settings_flush();
    if (e == 0) {
        pending = false;
        return;
    }
    printf("settings: save failed (%s)\n", nfs_err_str((nfs_err_t)e));
    retry_at = now + RETRY_MS;
    settings_status_t st;
    settings_get_status(&st);
    if (!st.persistent) {
        printf("settings: giving up on the card, settings are RAM-only until restart\n");
    }
}
