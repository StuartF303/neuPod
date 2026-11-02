// SPDX-License-Identifier: MPL-2.0
#include "capabilities.h"
#include "hal_haptic.h"
#include "haptic_service.h"
#include "os.h"
#include "runtime.h"
#include "runtime_config.h"

static os_queue_t *g_q;

void haptic_service_init(void) { g_q = os_queue_create(sizeof(uint8_t), 2); }
bool haptic_service_ready(void) { return g_q != NULL; }

void rt_haptic_click(void) {
    uint8_t b = 1;
    if (g_q != NULL) {
        (void)os_queue_send(g_q, &b, OS_NO_WAIT);
    }
}

static void standby(void) {
    if (os_mutex_lock(rt_i2c_mutex(), 100)) {
        (void)hal_haptic_standby(true);
        os_mutex_unlock(rt_i2c_mutex());
    }
}

void haptic_task(void *arg) {
    (void)arg;
    bool awake = false;
    uint32_t last_ms = 0;
    for (;;) {
        uint8_t req;
        if (!os_queue_recv(g_q, &req, awake ? RT_HAPTIC_IDLE_MS : OS_WAIT_FOREVER)) {
            if (awake) {
                standby(); // idle: back to the low-power state
                awake = false;
            }
            continue;
        }
        uint32_t now = os_tick_ms();
        if (!caps_available(CAP_HAPTICS) || now - last_ms < RT_HAPTIC_MIN_GAP_MS) {
            continue;
        }
        if (!os_mutex_lock(rt_i2c_mutex(), 20)) {
            continue; // bus busy (gauge read): skip this click rather than delay anything
        }
        if (!awake) {
            if (hal_haptic_standby(false) == HAL_OK && hal_haptic_click_prepare(RT_HAPTIC_CLICK_EFFECT) == HAL_OK) {
                awake = true;
            } else {
                (void)hal_haptic_safe_state();
            }
        }
        if (awake && hal_haptic_click() != HAL_OK) {
            (void)hal_haptic_safe_state(); // never leave the motor driven after an error
            awake = false;
        }
        os_mutex_unlock(rt_i2c_mutex());
        last_ms = now;
    }
}
