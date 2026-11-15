// SPDX-License-Identifier: MPL-2.0
#include "power_service.h"

#if NEUPOD_POWER_MGMT

#include <stdio.h>

#include "battery_store.h"
#include "bt_service.h"
#include "haptic_service.h"
#include "hal_radio.h"
#include "hal_storage.h"
#include "hal_system.h"
#include "library_service.h"
#include "os.h"
#include "player.h"
#include "settings_service.h"
#include "settings_store.h"
#include "storage_service.h"
#include "wifi_service.h"

#define POLL_MS 500u           /* the policy needs no finer clock, and hal_power_external() may read a file */
#define FLUSH_WAIT_MS 4000u    /* settings and the card: how long the sleep (or power-off) waits for them */

static power_policy_t g_pol;
static bool g_pol_started;
static uint32_t g_last_poll_ms;
static power_action_t g_last_action;
static uint8_t g_bt_flags; /* what bt_service_sleep() powered off: handed back at the wake */
static power_guard_t g_guard; /* the refused-power-off latch and the failed-sleep back-off */

bool power_service_can_act(void) {
    return (hal_power_can_sleep() || hal_system_can_poweroff()) && power_guard_allows(&g_guard, os_tick_ms());
}

static bool playing_now(void) {
    player_info_t pi;
    player_get(&pi);
    return pi.state == PLAYER_PLAYING || pi.state == PLAYER_OPENING || pi.state == PLAYER_VERIFYING;
}

// Work that must not be cut off by a sleep. There is no USB file transfer on any platform yet; when one appears it belongs here.
static bool busy_now(void) {
    if (!storage_service_idle()) { /* a request queued or a card write in flight: a listing, a library build, a settings save */
        return true;
    }
    library_progress_t lp;
    library_progress(&lp);
    if (lp.active) {
        return true;
    }
    const hal_bt_state_t bs = hal_bt_state();
    const hal_bt_link_state_t bl = hal_bt_link_state();
    if (bs == HAL_BT_STARTING || bs == HAL_BT_SCANNING || bl == HAL_BT_LINK_PAIRING || bl == HAL_BT_LINK_CONNECTING) {
        return true;
    }
    wifi_info_t wi;
    wifi_get_info(&wi);
    if (wi.state == HAL_WIFI_JOINING || wi.state == HAL_WIFI_NO_IP) {
        return true;
    }
    return rt_haptic_cal_state() == HAPTIC_CAL_RUNNING;
}

power_action_t power_service_poll(uint32_t now_ms, uint32_t last_activity_ms, uint32_t sleep_after_s) {
    if (!g_pol_started) {
        g_pol_started = true;
        power_policy_init(&g_pol, now_ms);
        g_last_poll_ms = now_ms;
        return POWER_ACT_NONE;
    }
    if ((uint32_t)(now_ms - g_last_poll_ms) < POLL_MS && (int32_t)(now_ms - g_last_poll_ms) >= 0) {
        return g_last_action == POWER_ACT_POWEROFF_CRITICAL ? g_last_action : POWER_ACT_NONE; /* the battery state holds between looks; a sleep is asked for once */
    }
    g_last_poll_ms = now_ms;
    hal_battery_status_t b = {0};
    const bool have = battery_store_get(&b);
    const bool gauge = have && b.valid && b.present;
    power_guard_observe(&g_guard, now_ms, gauge, b.rate_mpct_per_h, b.mv);
    /* A charger the sensor cannot see shows as a refused power-off (the board stays up on USB), or as the gauge reading a charge. The Y2's rate is
     * synthetic (+/- 10 %/h from the supply status), the MAX17048's is its CRATE register: both are compared with the UI's own "charging" threshold. */
    const bool charging = gauge && b.rate_mpct_per_h > POWER_CHARGE_RATE_MPCT_PER_H;
    power_inputs_t in = {
        .now_ms = now_ms,
        .last_activity_ms = last_activity_ms,
        .sleep_after_s = sleep_after_s,
        .audio_playing = playing_now(),
        .busy = busy_now(),
        .external_power = hal_power_external() || g_guard.refused || charging,
        .battery_valid = gauge,
        .battery_pct = b.soc_pct,
        .battery_mv = b.mv,
    };
    power_action_t a = power_policy_tick(&g_pol, &in);
    if (a == POWER_ACT_SLEEP && !power_service_can_act()) {
        power_policy_restart(&g_pol, now_ms); /* nothing can be done on this platform: look again after another full time */
        a = POWER_ACT_NONE;
    }
    g_last_action = a;
    return a;
}

void power_service_restart(uint32_t now_ms) {
    if (g_pol_started) {
        power_policy_restart(&g_pol, now_ms);
    }
}

// Wait until the settings are on the card and the storage task is idle. False if the card stayed busy (a write in flight must not be cut by a suspend).
// With no card (RAM-only settings) there is nothing to write and nothing to wait for.
static bool flush_storage(void) {
    settings_status_t st;
    settings_get_status(&st);
    if (!st.persistent) {
        return storage_service_idle();
    }
    settings_service_request_flush();
    const uint32_t t0 = os_tick_ms();
    while ((settings_service_flush_pending() || !storage_service_idle()) && os_tick_ms() - t0 < FLUSH_WAIT_MS) {
        os_delay_ms(20);
    }
    if (settings_service_flush_pending()) {
        printf("power: settings still waiting for the card after %u ms\n", (unsigned)FLUSH_WAIT_MS);
    }
    return storage_service_idle();
}

bool power_service_sleep_prepare(void) {
    if (playing_now()) {
        return false;
    }
    g_bt_flags = bt_service_sleep(); /* the stream goes with the controller; BT first: it takes the longest and its state changes may touch the settings */
    if (!flush_storage()) {
        printf("power: the card is busy, not going to sleep now\n");
        bt_service_wake(g_bt_flags); /* nothing else was done: put Bluetooth back */
        g_bt_flags = 0;
        return false;
    }
    (void)hal_storage_sync();
    if (!storage_service_idle() || playing_now()) { /* something queued meanwhile, or a headset key during the preparation */
        bt_service_wake(g_bt_flags);
        g_bt_flags = 0;
        return false;
    }
    return true;
}

hal_status_t power_service_sleep_block(hal_wake_reason_t *why) {
    printf("power: going to sleep\n");
    fflush(stdout);
    hal_status_t st = hal_power_sleep(POWER_OFF_AFTER_SLEEP_MS, why);
    printf("power: woke (status %d)\n", (int)st);
    return st;
}

void power_service_wake(uint32_t now_ms) {
    bt_service_wake(g_bt_flags);
    g_bt_flags = 0;
    power_service_restart(now_ms);
}

bool power_service_poweroff(void) {
    if (!hal_system_can_poweroff()) {
        return false;
    }
    (void)flush_storage(); /* the orderly stop of some platforms does not force out a change younger than a few seconds */
    if (hal_system_poweroff()) {
        return true;
    }
    // The platform says it can switch off and did not (a board that stays up on USB power): treat it as on external power until the gauge shows
    // otherwise, so nothing tries again every idle period (a GPIO pulse, a blocked UI).
    hal_battery_status_t b = {0};
    (void)battery_store_get(&b);
    power_guard_poweroff_refused(&g_guard, os_tick_ms(), b.mv);
    printf("power: power-off refused, assuming external power\n");
    return false;
}

void power_service_sleep_result(hal_status_t st, uint32_t now_ms) {
    if (st == HAL_OK) {
        power_guard_sleep_ok(&g_guard);
    } else if (power_guard_sleep_failed(&g_guard, now_ms)) {
        printf("power: sleep failed twice, not trying again for 1 h\n");
    }
}

#endif // NEUPOD_POWER_MGMT
