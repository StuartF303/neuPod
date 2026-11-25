// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 haptics: implements hal_haptic.h on the ERM motor at /sys/class/timed_output/vibrator/enable instead of the
 * DRV2605L (nothing is faked over I2C). Linked into the executable ahead of neupod_drivers, so the DRV2605L driver's
 * definitions of the same symbols are never pulled in (drv2605l_set_delay_fn is provided here for the same reason).
 *
 * The app's haptic task (haptic_service.c) already owns the queueing: the UI only drops a byte in a non-blocking
 * queue, and this HAL is called from that task, so even a slow sysfs write never reaches the input or UI path. The
 * write itself is one short call that starts a kernel timer. On top of the task's 35 ms request gap, the gate in
 * y2_haptic.c keeps a rest period after each pulse, so a fast wheel scroll gives separate taps, not a buzz.
 *
 * What the DRV2605L offers that has no meaning for a bare motor (calibration, ROM library, RTP, register info, LRA,
 * sequences) answers HAL_ENOTSUP; the actuator is always ERM, open loop.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "drv2605l.h"
#include "hal_haptic.h"
#include "y2_haptic.h"

static y2_haptic_table_t g_table;
static y2_haptic_gate_t g_gate;
static bool g_ready;
static uint8_t g_prepared = 3u; // effect for hal_haptic_click(); 3 = the app's click effect (a tick)
static uint32_t g_last_us;
// Only the haptic task and, at boot, the boot check call in here, both under the runtime's bus mutex.

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

void drv2605l_set_delay_fn(void (*fn)(uint32_t ms)) { (void)fn; }

hal_status_t hal_haptic_safe_state(void) { return y2_haptic_write(Y2_HAPTIC_PATH, 0) ? HAL_OK : HAL_ERR; }

hal_status_t hal_haptic_init(void) {
    y2_haptic_table_default(&g_table);
    const int bad = y2_haptic_table_parse(&g_table, getenv(Y2_HAPTIC_ENV));
    if (bad > 0) {
        printf("y2: haptics: %d bad field(s) in %s ignored (defaults kept)\n", bad, Y2_HAPTIC_ENV);
    }
    g_gate = (y2_haptic_gate_t){0};
    g_ready = false;
    if (!y2_haptic_node_ok(Y2_HAPTIC_PATH)) {
        printf("y2: haptics: %s: cannot open for writing\n", Y2_HAPTIC_PATH);
        return HAL_ENODEV;
    }
    g_ready = true;
    printf("y2: haptics: timed_output vibrator, tick %u ms, click %u ms, strong %u ms\n",
           (unsigned)g_table.ms[Y2_HAPTIC_TICK], (unsigned)g_table.ms[Y2_HAPTIC_CLICK],
           (unsigned)g_table.ms[Y2_HAPTIC_STRONG]);
    return HAL_OK;
}

static hal_status_t pulse(uint8_t effect_id) {
    if (!g_ready) {
        return HAL_ERR;
    }
    if (effect_id == 0u || effect_id > HAL_HAPTIC_MAX_EFFECT) {
        return HAL_EINVAL;
    }
    const uint16_t ms = g_table.ms[y2_haptic_kind_for_effect(effect_id)];
    const uint32_t t = now_ms();
    if (!y2_haptic_gate_pass(&g_gate, t, ms)) {
        return HAL_OK; // coalesced into the pulse still running
    }
    const bool ok = y2_haptic_write(Y2_HAPTIC_PATH, ms);
    g_last_us = (now_ms() - t) * 1000u;
    return ok ? HAL_OK : HAL_ERR;
}

hal_status_t hal_haptic_set_actuator(hal_haptic_actuator_t act) { return act == HAL_HAPTIC_ERM ? HAL_OK : HAL_ENOTSUP; }
hal_haptic_actuator_t hal_haptic_get_actuator(void) { return HAL_HAPTIC_ERM; }

hal_status_t hal_haptic_set_lra_params(const hal_haptic_lra_params_t *p) {
    (void)p;
    return HAL_ENOTSUP;
}
void hal_haptic_get_lra_params(hal_haptic_lra_params_t *out) {
    if (out != NULL) {
        *out = (hal_haptic_lra_params_t)HAL_HAPTIC_LRA_LD0832AA;
    }
}

hal_status_t hal_haptic_set_erm_loop(hal_haptic_loop_t loop) { return loop == HAL_HAPTIC_LOOP_OPEN ? HAL_OK : HAL_ENOTSUP; }
// Open loop: the haptic service then reports "calibration not needed".
hal_haptic_loop_t hal_haptic_get_erm_loop(void) { return HAL_HAPTIC_LOOP_OPEN; }

hal_status_t hal_haptic_read_info(hal_haptic_info_t *out) {
    (void)out;
    return HAL_ENOTSUP;
}
hal_status_t hal_haptic_set_library(uint8_t lib) {
    (void)lib;
    return HAL_ENOTSUP;
}
hal_status_t hal_haptic_calibrate(hal_haptic_cal_t *out) {
    (void)out;
    return HAL_ENOTSUP;
}
void hal_haptic_get_calibration(hal_haptic_cal_t *out) {
    if (out != NULL) {
        *out = (hal_haptic_cal_t){0};
    }
}
hal_status_t hal_haptic_set_calibration(const hal_haptic_cal_t *cal) {
    (void)cal;
    return HAL_ENOTSUP;
}

hal_status_t hal_haptic_play(uint8_t effect_id) { return pulse(effect_id); }
hal_status_t hal_haptic_play_sequence(const uint8_t *entries, size_t count) {
    (void)entries;
    (void)count;
    return HAL_ENOTSUP;
}
hal_status_t hal_haptic_stop(void) { return hal_haptic_safe_state(); }
hal_status_t hal_haptic_is_playing(bool *playing) {
    if (playing == NULL) {
        return HAL_EINVAL;
    }
    *playing = false; // the kernel timer ends the pulse; nothing to poll
    return HAL_OK;
}

hal_status_t hal_haptic_rtp_begin(void) { return HAL_ENOTSUP; }
hal_status_t hal_haptic_rtp_set(int8_t amplitude) {
    (void)amplitude;
    return HAL_ENOTSUP;
}
hal_status_t hal_haptic_rtp_end(void) { return HAL_ENOTSUP; }

hal_status_t hal_haptic_standby(bool standby) {
    (void)standby; // the motor is off between pulses; there is no low-power state to enter
    return g_ready ? HAL_OK : HAL_ERR;
}

hal_status_t hal_haptic_click_prepare(uint8_t effect_id) {
    if (effect_id == 0u || effect_id > HAL_HAPTIC_MAX_EFFECT) {
        return HAL_EINVAL;
    }
    g_prepared = effect_id;
    return HAL_OK;
}
hal_status_t hal_haptic_click(void) { return pulse(g_prepared); }
uint32_t hal_haptic_last_click_us(void) { return g_last_us; }
