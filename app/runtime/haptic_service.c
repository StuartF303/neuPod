// SPDX-License-Identifier: MPL-2.0
#include "capabilities.h"
#include "drv2605l.h"
#include "hal_haptic.h"
#include "haptic_logic.h"
#include "haptic_service.h"
#include "os.h"
#include "runtime.h"
#include "runtime_config.h"
#include "settings_store.h"

enum { MSG_CLICK = 1, MSG_WAKE = 2 };
enum { PEND_RELOAD = 1u, PEND_CAL = 2u };

static os_queue_t *g_q;
static volatile uint32_t g_pending;         // PEND_* bits: control requests, so a burst of clicks can never push one out of the queue
static volatile uint8_t g_act_req = NEUPOD_HAPTIC_ACTUATOR;
static volatile uint8_t g_cal_state = HAPTIC_CAL_UNKNOWN;
static bool g_auto_tried[2];                // per actuator: the automatic calibration was attempted since boot (a failure is not retried in a loop)

void haptic_service_init(void) {
    g_q = os_queue_create(sizeof(uint8_t), 2);
    drv2605l_set_delay_fn(os_delay_ms); // the calibration polls with the scheduler's delay, not a busy wait
}
bool haptic_service_ready(void) { return g_q != NULL; }

static void wake_task(uint8_t msg) {
    if (g_q != NULL) {
        (void)os_queue_send(g_q, &msg, OS_NO_WAIT); // a full queue is fine: the task is about to run anyway
    }
}

void rt_haptic_click(void) { wake_task(MSG_CLICK); }

void rt_piezo_click(void) {} // no piezo driver yet (docs/i2c-peripherals.md): selecting Piezo is stored but silent

hal_haptic_actuator_t rt_haptic_actuator(void) { return (hal_haptic_actuator_t)g_act_req; }
haptic_cal_state_t rt_haptic_cal_state(void) { return (haptic_cal_state_t)g_cal_state; }

static void post(uint32_t bit) {
    __atomic_fetch_or(&g_pending, bit, __ATOMIC_ACQ_REL);
    wake_task(MSG_WAKE);
}

void rt_haptic_set_actuator(hal_haptic_actuator_t act) {
    if (act != HAL_HAPTIC_ERM && act != HAL_HAPTIC_LRA) {
        return;
    }
    g_act_req = (uint8_t)act;
    settings_set_u8(SETTING_HAPTIC_ACTUATOR, (uint8_t)act);
    post(PEND_RELOAD);
}

void rt_haptic_request_calibration(void) {
    if (g_cal_state != HAPTIC_CAL_RUNNING) {
        post(PEND_CAL);
    }
}

void rt_haptic_settings_loaded(void) { post(PEND_RELOAD); }

static void standby(void) {
    if (os_mutex_lock(rt_i2c_mutex(), 100)) {
        (void)hal_haptic_standby(true);
        os_mutex_unlock(rt_i2c_mutex());
    }
}

static bool needs_calibration(hal_haptic_actuator_t act) {
    return !(act == HAL_HAPTIC_ERM && hal_haptic_get_erm_loop() == HAL_HAPTIC_LOOP_OPEN);
}

// Persisted settings are known: either the card's values were loaded, or storage is settled as absent/failed.
static bool settings_settled(void) {
    settings_status_t st;
    settings_get_status(&st);
    if (st.loaded) {
        return true;
    }
    capabilities_t c;
    caps_get(&c);
    return c.state[CAP_STORAGE] != CAP_PENDING;
}

// Runs the datasheet auto-calibration and stores the result. Bus mutex held by the caller.
static void calibrate_and_store(hal_haptic_actuator_t act) {
    hal_haptic_cal_t cal = {0};
    g_cal_state = HAPTIC_CAL_RUNNING;
    hal_status_t st = hal_haptic_calibrate(&cal); // ~1 s, scheduler delays in between
    if (st == HAL_ENOTSUP) {
        g_cal_state = HAPTIC_CAL_NOT_NEEDED;
    } else if (st == HAL_OK && cal.valid) {
        hal_haptic_lra_params_t lra;
        uint8_t rec[HAPTIC_CAL_RECORD_LEN];
        hal_haptic_get_lra_params(&lra);
        haptic_cal_encode(act, &lra, &cal, rec);
        settings_put(SETTING_HAPTIC_CAL, rec, sizeof rec);
        g_cal_state = HAPTIC_CAL_OK;
    } else {
        g_cal_state = HAPTIC_CAL_FAILED; // the driver already restored the previous good values
    }
}

// Apply the stored actuator and calibration to the chip; calibrate if this actuator has none. Bus mutex held.
static void apply_settings(uint32_t pend) {
    uint8_t v;
    if ((pend & PEND_RELOAD) && settings_get_u8(SETTING_HAPTIC_ACTUATOR, &v) && v <= 1u) {
        g_act_req = v;
    }
    const hal_haptic_actuator_t act = (hal_haptic_actuator_t)g_act_req;
    if (act != hal_haptic_get_actuator()) {
        (void)hal_haptic_set_actuator(act); // a different type: its calibration is dropped, the chip reconfigured
    }
    hal_haptic_cal_t cal;
    uint8_t rec[SETTINGS_VALUE_MAX];
    hal_haptic_lra_params_t lra;
    hal_haptic_get_lra_params(&lra);
    const uint32_t n = settings_get(SETTING_HAPTIC_CAL, rec, sizeof rec);
    const bool have = haptic_cal_decode(rec, n, act, &lra, &cal);
    if (have) {
        (void)hal_haptic_set_calibration(&cal);
    }
    if (hal_haptic_init() != HAL_OK) { // reset + configure with the values above
        (void)hal_haptic_safe_state();
        g_cal_state = HAPTIC_CAL_FAILED;
        return;
    }
    if (!needs_calibration(act)) {
        g_cal_state = HAPTIC_CAL_NOT_NEEDED;
    } else if (have) {
        g_cal_state = HAPTIC_CAL_OK;
    } else {
        g_cal_state = HAPTIC_CAL_UNKNOWN;
        if (!g_auto_tried[act] && settings_settled()) {
            g_auto_tried[act] = true; // once per actuator per boot; Settings > Calibrate retries
            pend |= PEND_CAL;
        }
    }
    if (pend & PEND_CAL) {
        calibrate_and_store(act);
    }
    (void)hal_haptic_standby(true);
}

static void handle_control(uint32_t pend) {
    if (!caps_available(CAP_HAPTICS)) {
        return;
    }
    if (!os_mutex_lock(rt_i2c_mutex(), RT_HAPTIC_CAL_LOCK_MS)) {
        post(pend); // bus busy: try again
        return;
    }
    if (pend & PEND_RELOAD) {
        apply_settings(pend);
    } else if (pend & PEND_CAL) {
        if (hal_haptic_standby(false) == HAL_OK) {
            calibrate_and_store((hal_haptic_actuator_t)g_act_req);
        } else {
            g_cal_state = HAPTIC_CAL_FAILED;
        }
        (void)hal_haptic_standby(true);
    }
    os_mutex_unlock(rt_i2c_mutex());
}

void haptic_task(void *arg) {
    (void)arg;
    bool awake = false;
    haptic_gate_t gate = {0};
    for (;;) {
        uint8_t req;
        if (!os_queue_recv(g_q, &req, awake ? RT_HAPTIC_IDLE_MS : OS_WAIT_FOREVER)) {
            if (awake) {
                standby(); // idle: back to the low-power state
                awake = false;
            }
            continue;
        }
        const uint32_t pend = __atomic_exchange_n(&g_pending, 0u, __ATOMIC_ACQ_REL);
        if (pend != 0) {
            handle_control(pend); // leaves the chip in standby
            awake = false;
        }
        if (req != MSG_CLICK || g_cal_state == HAPTIC_CAL_RUNNING || !caps_available(CAP_HAPTICS)) {
            continue;
        }
        if (!haptic_gate_pass(&gate, os_tick_ms(), RT_HAPTIC_MIN_GAP_MS)) {
            continue; // too soon after the last click: dropped, never queued
        }
        if (!os_mutex_lock(rt_i2c_mutex(), 20)) {
            continue; // bus busy (gauge read): skip this click rather than delay anything
        }
        const uint8_t effect = hal_haptic_get_actuator() == HAL_HAPTIC_LRA ? RT_HAPTIC_CLICK_EFFECT_LRA : RT_HAPTIC_CLICK_EFFECT;
        if (!awake) {
            if (hal_haptic_standby(false) == HAL_OK && hal_haptic_click_prepare(effect) == HAL_OK) {
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
    }
}
