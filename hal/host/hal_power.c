// SPDX-License-Identifier: MPL-2.0
/*
 * Host power simulation (ADR-0021): external power is a switch (env NEUPOD_HOST_EXTERNAL_POWER=1, or the tour's ext: token), and sleep is simulated, so
 * ctest and the tours can run the whole sequence (the app's preparation, the screen off, the wake and what the wake input does) with nothing stopping.
 *
 *   NEUPOD_SUSPEND=0            hal_power_can_sleep() is false (the same switch as on the Y2): the core then simulates a power-off after the idle time.
 *   NEUPOD_HOST_SLEEP_WAKE_MS=N the sleep ends after N ms as if a key had woken it (a Select press is injected, so the input that wakes is real);
 *                               unset or 0: it ends when a person presses, turns or touches something at the window, or the charger is plugged in.
 *   NEUPOD_HOST_SLEEP_MAX_MS=N  the sleep limit is N ms instead of the 24 hours the core asks for (HAL_WAKE_TIMER after that).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal_input.h"
#include "hal_power.h"
#include "hal_system.h"
#include "host_internal.h"

static volatile bool g_external;
static bool g_inited;

void hal_host_external_power(bool on) { g_external = on; }

static uint32_t env_ms(const char *name) {
    const char *v = getenv(name);
    return v != NULL && v[0] != '\0' ? (uint32_t)strtoul(v, NULL, 10) : 0u;
}

hal_status_t hal_power_init(void) {
    if (!g_inited) {
        g_inited = true;
        const char *e = getenv("NEUPOD_HOST_EXTERNAL_POWER");
        g_external = e != NULL && e[0] == '1';
    }
    return HAL_OK;
}

bool hal_power_can_sleep(void) {
    const char *v = getenv("NEUPOD_SUSPEND");
    return v == NULL || strcmp(v, "0") != 0;
}

bool hal_power_external(void) { return g_external; }

hal_status_t hal_power_sleep(uint32_t max_ms, hal_wake_reason_t *why) {
    if (why != NULL) {
        *why = HAL_WAKE_NONE;
    }
    if (!hal_power_can_sleep()) {
        return HAL_ENOTSUP;
    }
    const uint32_t cap = env_ms("NEUPOD_HOST_SLEEP_MAX_MS");
    if (cap != 0u && cap < max_ms) {
        max_ms = cap;
    }
    const uint32_t wake_ms = env_ms("NEUPOD_HOST_SLEEP_WAKE_MS");
    printf("host: sleep (simulated), limit %u ms\n", (unsigned)max_ms);
    const uint32_t t0 = hal_millis();
    const uint32_t seen = host_input_activity();
    const bool ext0 = g_external;
    hal_wake_reason_t reason;
    for (;;) {
        host_pump_events();
        const uint32_t waited = hal_millis() - t0;
        if (wake_ms != 0u && waited >= wake_ms) {
            // The key that wakes a real player is queued for the input task: inject one, held long enough to be polled.
            host_input_button(HAL_BTN_SELECT, true);
            hal_delay_ms(40);
            host_input_button(HAL_BTN_SELECT, false);
            hal_delay_ms(10);
            reason = HAL_WAKE_KEY;
            break;
        }
        if (host_input_activity() != seen) {
            reason = HAL_WAKE_KEY;
            break;
        }
        if (g_external && !ext0) {
            reason = HAL_WAKE_EXTERNAL_POWER;
            break;
        }
        if (waited >= max_ms) {
            reason = HAL_WAKE_TIMER;
            break;
        }
        hal_delay_ms(20);
    }
    static const char *const names[] = {"none", "key", "timer", "external power", "other"};
    printf("host: wake (%s)\n", names[reason]);
    if (why != NULL) {
        *why = reason;
    }
    return HAL_OK;
}
