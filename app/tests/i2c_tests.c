// SPDX-License-Identifier: MPL-2.0
/*
 * I2C bring-up test runner for the fuel gauge (MAX17048, 0x36) and haptic driver (DRV2605L, 0x5A).
 * Keys during long tests: 'q' stops (any key stops the effect sweep). See docs/i2c-peripherals.md.
 */
#include "i2c_tests.h"

#include <stdio.h>
#include <string.h>

#include "drv2605l.h"
#include "hal_battery.h"
#include "hal_console.h"
#include "hal_display.h"
#include "hal_haptic.h"
#include "hal_i2c.h"
#include "hal_system.h"
#include "max17048.h"

typedef enum { RES_PASS, RES_FAIL, RES_SKIP, RES_STOP } result_t;

#define ADDR_GAUGE 0x36u
#define ADDR_HAPTIC 0x5Au
#define FAST_HZ 400000u
#define SLOW_HZ 100000u

static bool s_run_all;       // true while the "a" summary run is active (shortens the sweep)
static bool s_haptic_up;     // hal_haptic_init() succeeded since the last actuator change/failure
static bool s_haptic_awake;  // chip taken out of standby by a haptic test (put back after it)

// --- helpers -------------------------------------------------------------------------------

static const char *known_name(uint8_t addr) {
    switch (addr) {
    case ADDR_GAUGE: return "MAX17048 fuel gauge";
    case ADDR_HAPTIC: return "DRV2605L haptic driver";
    default: return NULL;
    }
}

static void print_stats(void) {
    hal_i2c_stats_t s;
    hal_i2c_get_stats(&s);
    printf("I2C stats: transactions %u, nack_addr %u, nack_data %u, timeouts %u, arb_lost %u, "
           "bus_stuck %u, probe_nacks %u, recoveries %u (failed %u)\n",
           (unsigned)s.transactions, (unsigned)s.nack_addr, (unsigned)s.nack_data,
           (unsigned)s.timeouts, (unsigned)s.arb_lost, (unsigned)s.bus_stuck,
           (unsigned)s.probe_nacks, (unsigned)s.recoveries, (unsigned)s.recovery_failures);
}

// --- test 1: bus scan ------------------------------------------------------------------------

#define MAX_FOUND 32
typedef struct {
    uint8_t addr[MAX_FOUND];
    size_t n;
} found_t;

static void on_found(uint8_t addr, void *ctx) {
    found_t *f = ctx;
    if (f->n < MAX_FOUND) {
        f->addr[f->n++] = addr;
    }
}

static bool has(const found_t *f, uint8_t addr) {
    for (size_t i = 0; i < f->n; i++) {
        if (f->addr[i] == addr) {
            return true;
        }
    }
    return false;
}

// Print lines levels; returns false if a line is low.
static bool check_lines(const char *when) {
    bool sda = false, scl = false;
    hal_i2c_err_t e = hal_i2c_bus_state(&sda, &scl);
    if (e != HAL_I2C_OK) {
        printf("  lines (%s): cannot read (%s)\n", when, hal_i2c_strerror(e));
        return false;
    }
    printf("  lines (%s): SDA %s, SCL %s\n", when, sda ? "high" : "LOW", scl ? "high" : "LOW");
    return sda && scl;
}

// One scan pass at the current speed. Returns true if no timeouts/bus errors occurred.
static bool scan_pass(found_t *f) {
    f->n = 0;
    uint32_t t0 = (uint32_t)hal_micros();
    size_t found = 0;
    hal_i2c_err_t e = hal_i2c_scan(HAL_I2C_ADDR_FIRST, HAL_I2C_ADDR_LAST, on_found, f, &found);
    printf("  scan 0x%02X-0x%02X at %u Hz: %u device(s) in %u us%s%s\n", HAL_I2C_ADDR_FIRST,
           HAL_I2C_ADDR_LAST, (unsigned)hal_i2c_get_baud(), (unsigned)found,
           (unsigned)((uint32_t)hal_micros() - t0), e == HAL_I2C_OK ? "" : ", error: ",
           e == HAL_I2C_OK ? "" : hal_i2c_strerror(e));
    for (size_t i = 0; i < f->n; i++) {
        const char *name = known_name(f->addr[i]);
        printf("    0x%02X  %s\n", f->addr[i], name ? name : "(unknown device)");
    }
    return e == HAL_I2C_OK;
}

static void motor_safe(void);

static result_t test_scan(void) {
    printf("\n== Test 1: I2C bus scan (I2C0, SDA GP20, SCL GP21; internal pulls off) ==\n");
    uint32_t t_start = hal_millis();

    hal_i2c_err_t e = hal_i2c_init(FAST_HZ);
    printf("init %u Hz requested: %s, actual %u Hz\n", (unsigned)FAST_HZ, hal_i2c_strerror(e),
           (unsigned)hal_i2c_get_baud());
    hal_i2c_stats_t st;
    hal_i2c_get_stats(&st);
    if (st.recoveries) {
        printf("  SDA/SCL was held low at init: recovery attempted %u time(s), %u failed\n",
               (unsigned)st.recoveries, (unsigned)st.recovery_failures);
    }
    if (e != HAL_I2C_OK && e != HAL_I2C_BUS_STUCK) {
        printf("FAIL: init failed (%s)\n", hal_i2c_strerror(e));
        return RES_FAIL;
    }
    bool idle = check_lines("before scan");
    if (!idle) {
        printf("  line(s) not idle-high: trying bus recovery\n");
        e = hal_i2c_recover();
        printf("  recovery: %s\n", hal_i2c_strerror(e));
        idle = check_lines("after recovery");
        if (!idle) {
            printf("FAIL: bus stuck. Check pull-ups (breakouts), wiring to GP20/GP21, and that no "
                   "device is holding a line low (power-cycle the breakouts).\n");
            print_stats();
            return RES_FAIL;
        }
    }

    // First pass at 400 kHz. It only counts as "clean" if both devices answered and nothing but
    // plain NACKs (absent addresses) happened.
    motor_safe();
    found_t f;
    bool clean = scan_pass(&f);
    bool both = has(&f, ADDR_GAUGE) && has(&f, ADDR_HAPTIC);
    unsigned first_hz = hal_i2c_get_baud();
    bool fell_back = false;
    if (!(both && clean)) {
        // Retry slower. Not silent: reported below, and the bus is left at the speed that worked.
        printf("  %s at %u Hz: retrying at %u Hz\n",
               clean ? "expected device(s) missing" : "bus errors", first_hz, (unsigned)SLOW_HZ);
        if (hal_i2c_set_baud(SLOW_HZ) == HAL_I2C_OK) {
            fell_back = true;
            (void)scan_pass(&f);
        }
    }

    bool gauge = has(&f, ADDR_GAUGE), haptic = has(&f, ADDR_HAPTIC);
    printf("MAX17048 (0x36): %s    DRV2605L (0x5A): %s\n", gauge ? "found" : "MISSING",
           haptic ? "found" : "MISSING");
    if (!gauge) {
        printf("  hint: MAX17048 is powered from the battery side — connect a LiPo\n");
    }
    if (!haptic) {
        printf("  hint: check the DRV2605L breakout power (3.3 V), SDA/SCL wiring and EN pin\n");
    }
    check_lines("after scan");
    printf("elapsed %u ms\n", (unsigned)(hal_millis() - t_start));
    print_stats();

    if (gauge && haptic) {
        if (!fell_back) {
            printf("PASS: both devices answered at %u Hz\n", first_hz);
        } else {
            printf("PASS: both devices answered at %u Hz only (%u Hz had %s): use 100 kHz\n",
                   (unsigned)hal_i2c_get_baud(), first_hz,
                   clean ? "a missing device" : "bus errors");
        }
        return RES_PASS;
    }
    printf("FAIL: expected device(s) not found\n");
    return RES_FAIL;
}

// --- shared helpers for tests 2-9 -------------------------------------------------------------

// Sleep up to @p ms; wakes early on a key. Returns true if the user asked to stop ('q', or any
// key when @p any_key).
static bool wait_ms(uint32_t ms, bool any_key) {
    uint32_t t0 = hal_millis();
    for (;;) {
        uint32_t el = hal_millis() - t0;
        if (el >= ms) {
            return false;
        }
        uint32_t rem = ms - el;
        int c = hal_console_getc_timeout(rem > 20u ? 20u : rem);
        if (c >= 0 && (any_key || c == 'q' || c == 'Q')) {
            return true;
        }
    }
}

// First thing after the bus comes up: if a DRV2605L answers, make sure the motor is not driven
// (a previous run may have left RTP or playback active): RTP 0, GO 0, standby.
static void motor_safe(void) {
    if (hal_i2c_probe(ADDR_HAPTIC) == HAL_I2C_OK) {
        hal_status_t st = hal_haptic_safe_state();
        if (st != HAL_OK) {
            printf("WARNING: could not put the haptic driver in standby (status %d); the motor may "
                   "still be running: pull EN low or power-cycle the breakout\n", (int)st);
        }
        s_haptic_up = false;
        s_haptic_awake = false;
    }
}

static void drain_input(void) {
    while (hal_console_getc_timeout(0) >= 0) {
    }
}

static bool bus_ready(void) {
    if (hal_i2c_get_baud() != 0) {
        return true;
    }
    hal_i2c_err_t e = hal_i2c_init(FAST_HZ);
    printf("[I2C initialised at %u Hz: %s]\n", (unsigned)hal_i2c_get_baud(), hal_i2c_strerror(e));
    if (e == HAL_I2C_OK) {
        motor_safe();
    }
    return e == HAL_I2C_OK;
}

static bool gauge_ready(void) {
    if (!bus_ready()) {
        return false;
    }
    hal_status_t st = hal_battery_init();
    if (st != HAL_OK) {
        printf("MAX17048 not available (status %d).\n  hint: it is powered from the battery side: "
               "connect a charged LiPo (a flat one makes 0x36 vanish)\n", (int)st);
        return false;
    }
    return true;
}

static bool haptic_ready(void) {
    if (!bus_ready()) {
        return false;
    }
    if (!s_haptic_up) {
        hal_status_t st = hal_haptic_init();
        if (st != HAL_OK) {
            printf("DRV2605L init failed (status %d). Run test 1 / 4: address 0x5A, wiring, EN pin, "
                   "3.3 V.\n", (int)st);
            return false;
        }
        s_haptic_up = true;
        s_haptic_awake = true; // init leaves standby
    } else if (!s_haptic_awake) {
        if (hal_haptic_standby(false) != HAL_OK) { // woken once per test, never per click
            printf("DRV2605L wake failed\n");
            return false;
        }
        s_haptic_awake = true;
    }
    hal_haptic_info_t inf;
    bool erm = hal_haptic_get_actuator() == HAL_HAPTIC_ERM;
    bool open = hal_haptic_get_erm_loop() == HAL_HAPTIC_LOOP_OPEN;
    if (erm && hal_haptic_read_info(&inf) == HAL_OK) {
        uint32_t mv = open ? drv2605l_erm_ol_od_clamp_mv(inf.od_clamp)
                           : drv2605l_erm_od_clamp_mv(inf.od_clamp, DRV_ERM_DRIVE_TIME,
                                                      DRV_ERM_BLANKING_SEL, DRV_ERM_IDISS_SEL);
        printf("[haptic: ERM %s loop, library %u, OD_CLAMP %u = %u mV%s]\n", open ? "OPEN" : "CLOSED",
               (unsigned)(inf.library & 7u), (unsigned)inf.od_clamp, (unsigned)mv,
               open ? " (21.59 mV/step; RATED ignored)" : "");
    } else {
        printf("[haptic: %s, library %u]\n", erm ? "ERM" : "LRA",
               erm ? (unsigned)DRV_LIB_ERM_B : (unsigned)DRV_LIB_LRA);
    }
    return true;
}

static void print_rate(int32_t mpct_per_h) {
    uint32_t a = (uint32_t)(mpct_per_h < 0 ? -mpct_per_h : mpct_per_h);
    printf("%c%u.%03u", mpct_per_h < 0 ? '-' : '+', (unsigned)(a / 1000u), (unsigned)(a % 1000u));
}

static void print_gauge_line(unsigned t, const hal_battery_status_t *b) {
    printf("  t=%2us  %4u mV  SOC %3u.%02u %%  rate ", t, (unsigned)b->mv,
           (unsigned)(b->soc_x256 / 256u), (unsigned)((b->soc_x256 % 256u) * 100u / 256u));
    print_rate(b->rate_mpct_per_h);
    printf(" %%/h  alerts 0x%02X%s\n", b->alerts, b->present ? "" : "  (no battery)");
}

// --- test 2: gauge ---------------------------------------------------------------------------

static result_t test_gauge(void) {
    printf("\n== Test 2: fuel gauge MAX17048 ==\n");
    if (!gauge_ready()) {
        printf("FAIL: gauge not answering\n");
        return RES_FAIL;
    }
    uint16_t ver = 0, cfg = 0, hib = 0;
    (void)hal_battery_get_version(&ver);
    (void)hal_battery_get_config(&cfg);
    (void)hal_battery_get_hibrt(&hib);
    printf("VERSION 0x%04X, CONFIG 0x%04X (RCOMP 0x%02X, SLEEP %u, ALSC %u, ALRT %u, low-SOC alert "
           "at %u %%), HIBRT 0x%04X\n", ver, cfg, cfg >> 8, !!(cfg & MAX17048_CONFIG_SLEEP),
           !!(cfg & MAX17048_CONFIG_ALSC), !!(cfg & MAX17048_CONFIG_ALRT),
           max17048_config_athd_pct(cfg), hib);
    unsigned fails = 0;
    bool present = false;
    for (unsigned t = 0; t < 10; t++) {
        hal_battery_status_t b;
        if (hal_battery_read(&b) != HAL_OK) {
            printf("  t=%2us  read failed\n", t);
            fails++;
        } else {
            print_gauge_line(t, &b);
            present = b.present;
        }
        if (wait_ms(1000, false)) {
            return RES_STOP;
        }
    }
    print_stats();
    if (fails) {
        printf("FAIL: %u read(s) failed\n", fails);
        return RES_FAIL;
    }
    if (!present) {
        printf("SKIP: no battery (cell voltage below %u mV)\n", (unsigned)HAL_BATTERY_PRESENT_MV);
        return RES_SKIP;
    }
    printf("PASS\n");
    return RES_PASS;
}

// --- test 3: gauge under load ----------------------------------------------------------------

typedef struct {
    bool ok;
    bool stop;
    uint32_t avg_mv;
    int32_t last_rate;
} phase_t;

static phase_t sample_phase(const char *name, unsigned secs, bool busy) {
    phase_t p = {true, false, 0, 0};
    uint32_t sum = 0, n = 0;
    printf(" %s (%u s)\n", name, secs);
    for (unsigned t = 0; t < secs && !p.stop; t++) {
        hal_battery_status_t b;
        if (hal_battery_read(&b) != HAL_OK) {
            printf("  t=%2us  read failed\n", t);
            p.ok = false;
        } else {
            print_gauge_line(t, &b);
            sum += b.mv;
            n++;
            p.last_rate = b.rate_mpct_per_h;
        }
        uint32_t t0 = hal_millis();
        if (busy) {
            volatile uint32_t sink = 0;
            while (hal_millis() - t0 < 1000u && !p.stop) {
                for (unsigned i = 0; i < 20000u; i++) {
                    sink += i * 2654435761u; // CPU load
                }
                int c = hal_console_getc_timeout(0);
                p.stop = (c == 'q' || c == 'Q');
            }
        } else {
            p.stop = wait_ms(1000, false);
        }
    }
    p.avg_mv = n ? sum / n : 0;
    return p;
}

static result_t test_gauge_load(void) {
    printf("\n== Test 3: gauge under load (backlight 100 %% + CPU busy-loop) ==\n");
    if (!gauge_ready()) {
        printf("FAIL: gauge not answering\n");
        return RES_FAIL;
    }
    hal_battery_status_t b0;
    if (hal_battery_read(&b0) != HAL_OK) {
        printf("FAIL: gauge read failed\n");
        return RES_FAIL;
    }
    if (!b0.present) {
        printf("SKIP: no battery (%u mV)\n", (unsigned)b0.mv);
        return RES_SKIP;
    }
    bool disp = hal_display_init() == HAL_OK;
    if (!disp) {
        printf("  (display init failed: CPU load only)\n");
    }
    if (disp) {
        hal_display_backlight(0);
    }
    phase_t idle = sample_phase("baseline: backlight off, CPU idle", 5, false);
    phase_t load = {false, true, 0, 0};
    if (!idle.stop) {
        if (disp) {
            hal_display_backlight(255);
        }
        load = sample_phase("load: backlight 255, CPU busy", 10, true);
    }
    if (disp) {
        hal_display_backlight(0); // always restore
    }
    if (idle.stop || load.stop) {
        return RES_STOP;
    }
    printf("avg voltage: idle %u mV, load %u mV (delta %d mV)\n", (unsigned)idle.avg_mv,
           (unsigned)load.avg_mv, (int)load.avg_mv - (int)idle.avg_mv);
    printf("last rate: idle ");
    print_rate(idle.last_rate);
    printf(" %%/h, load ");
    print_rate(load.last_rate);
    printf(" %%/h (the gauge's rate estimate reacts slowly)\n");
    int dmv = (int)load.avg_mv - (int)idle.avg_mv;
    if (dmv >= -2 && dmv <= 2 && idle.last_rate == load.last_rate) {
        printf("NOTE: no change seen. Load current must flow from the battery through the gauge for "
               "this test to show anything; with the Pico powered from USB it does not.\n");
    }
    print_stats();
    if (!idle.ok || !load.ok) {
        printf("FAIL: gauge reads failed\n");
        return RES_FAIL;
    }
    printf("PASS\n");
    return RES_PASS;
}

// --- test 4: haptic ID -----------------------------------------------------------------------

static const char *device_name(uint8_t id) {
    switch (id) {
    case 3: return "DRV2605";
    case 4: return "DRV2604";
    case 6: return "DRV2604L";
    case 7: return "DRV2605L";
    default: return "unknown";
    }
}

static result_t test_haptic_id(void) {
    printf("\n== Test 4: DRV2605L identification ==\n");
    if (!bus_ready()) {
        return RES_FAIL;
    }
    hal_haptic_info_t i;
    hal_status_t st = hal_haptic_read_info(&i);
    if (st != HAL_OK) {
        printf("FAIL: DRV2605L not answering (status %d). Check 0x5A, wiring, EN pin.\n", (int)st);
        return RES_FAIL;
    }
    printf("STATUS 0x%02X: device ID %u (%s), DIAG_RESULT %u, OVER_TEMP %u, OC_DETECT %u\n", i.status,
           i.device_id, device_name(i.device_id), !!(i.status & 0x08), !!(i.status & 0x02),
           i.status & 1u);
    printf("MODE 0x%02X (%s, mode %u), LIBRARY 0x%02X (library %u), RTP 0x%02X\n", i.mode,
           (i.mode & 0x40) ? "standby" : "active", i.mode & 7u, i.library, i.library & 7u, i.rtp);
    printf("RATED 0x%02X, OD_CLAMP 0x%02X, A_CAL_COMP 0x%02X, A_CAL_BEMF 0x%02X, FEEDBACK 0x%02X\n",
           i.rated, i.od_clamp, i.a_cal_comp, i.a_cal_bemf, i.feedback);
    printf("CONTROL1 0x%02X, CONTROL2 0x%02X, CONTROL3 0x%02X, CONTROL4 0x%02X (state as found)\n",
           i.control1, i.control2, i.control3, i.control4);
    if (i.device_id != DRV_DEVICE_ID_DRV2605L) {
        printf("FAIL: DEVICE_ID is %u, expected 7 (DRV2605L)\n", i.device_id);
        return RES_FAIL;
    }
    printf("PASS\n");
    return RES_PASS;
}

// --- test 5: calibration ---------------------------------------------------------------------

static void print_cal(const hal_haptic_cal_t *c) {
    static const uint32_t erm_gain_x10000[4] = {2550, 7875, 13650, 30000};
    static const uint32_t lra_gain_x10000[4] = {37500, 75000, 150000, 225000};
    bool erm = hal_haptic_get_actuator() == HAL_HAPTIC_ERM;
    uint32_t g = (erm ? erm_gain_x10000 : lra_gain_x10000)[c->bemf_gain & 3u];
    uint32_t bemf_mv = (uint32_t)((uint64_t)c->a_cal_bemf * 1220u * 10000u / (255u * (uint64_t)g));
    uint32_t coef_milli = 1000u + (uint32_t)c->a_cal_comp * 1000u / 255u;
    printf("  A_CAL_COMP 0x%02X (compensation x%u.%03u)\n", c->a_cal_comp,
           (unsigned)(coef_milli / 1000u), (unsigned)(coef_milli % 1000u));
    printf("  A_CAL_BEMF 0x%02X (rated back-EMF about %u mV)\n", c->a_cal_bemf, (unsigned)bemf_mv);
    printf("  BEMF_GAIN  %u (%u.%04ux)\n", c->bemf_gain & 3u, (unsigned)(g / 10000u),
           (unsigned)(g % 10000u));
    printf("  DIAG_RESULT: %s\n", c->diag_failed ? "FAILED (did not converge)" : "ok");
}

static result_t test_haptic_cal(void) {
    printf("\n== Test 5: DRV2605L auto-calibration (motor runs for about 1 s) ==\n");
    if (hal_haptic_get_actuator() == HAL_HAPTIC_ERM && hal_haptic_get_erm_loop() == HAL_HAPTIC_LOOP_OPEN) {
        printf("auto-calibration not applicable (ERM open loop); press 'o' to switch to closed loop\n");
        return RES_SKIP;
    }
    if (!haptic_ready()) {
        return RES_FAIL;
    }
    if (hal_haptic_get_actuator() == HAL_HAPTIC_ERM) {
        printf("ERM closed loop: rated %u mV -> RATED_VOLTAGE 0x%02X, OD clamp max %u mV -> OD_CLAMP "
               "0x%02X\n", (unsigned)DRV_ERM_RATED_MV, drv2605l_erm_rated_reg(DRV_ERM_RATED_MV),
               (unsigned)DRV_ERM_OD_MAX_MV,
               drv2605l_erm_od_clamp_reg(DRV_ERM_OD_MAX_MV, DRV_ERM_DRIVE_TIME, DRV_ERM_BLANKING_SEL,
                                         DRV_ERM_IDISS_SEL));
    }
    hal_haptic_cal_t cal;
    uint32_t t0 = hal_millis();
    hal_status_t st = hal_haptic_calibrate(&cal);
    printf("calibration took %u ms, status %d\n", (unsigned)(hal_millis() - t0), (int)st);
    if (st != HAL_OK || cal.diag_failed) {
        s_haptic_up = false; // driver rebuilt the chip from reset; re-init on next use
    }
    if (st != HAL_OK) {
        printf("FAIL: calibration did not complete (GO never cleared or I2C error). Check ERM/LRA "
               "setting, motor wiring/seating, supply.\n");
        return RES_FAIL;
    }
    printf("measured values (read right after GO cleared, before any restore):\n");
    print_cal(&cal);
    if (cal.diag_failed) {
        hal_haptic_cal_t kept;
        hal_haptic_get_calibration(&kept);
        printf("restored previous calibration: %s\n", kept.valid ? "yes" : "none (chip reset to defaults)");
    }
    if (cal.diag_failed) {
        printf("FAIL: calibration did not converge: wrong ERM/LRA setting or motor not seated.\n");
        return RES_FAIL;
    }
    printf("stored in RAM (re-applied by init). To make it the build default, add:\n"
           "  -DNEUPOD_HAPTIC_CAL_COMP=0x%02X -DNEUPOD_HAPTIC_CAL_BEMF=0x%02X "
           "-DNEUPOD_HAPTIC_CAL_BEMF_GAIN=%u\n", cal.a_cal_comp, cal.a_cal_bemf, cal.bemf_gain & 3u);
    printf("PASS\n");
    return RES_PASS;
}

// --- test 6: effect sweep --------------------------------------------------------------------

static result_t test_haptic_sweep(void) {
    unsigned last = s_run_all ? 10u : HAL_HAPTIC_MAX_EFFECT;
    printf("\n== Test 6: effect sweep 1-%u, 300 ms apart (any key stops)%s ==\n", last,
           s_run_all ? " [shortened in run-all; run alone for 1-123]" : "");
    if (!haptic_ready()) {
        return RES_FAIL;
    }
    drain_input(); // stale keypresses must not stop the sweep at once
    for (unsigned id = 1; id <= last; id++) {
        printf("effect %3u\n", id);
        if (hal_haptic_stop() != HAL_OK || hal_haptic_play((uint8_t)id) != HAL_OK) {
            printf("FAIL: I2C error playing effect %u\n", id);
            return RES_FAIL;
        }
        if (wait_ms(300, true)) {
            (void)hal_haptic_stop();
            printf("stopped at effect %u\n", id);
            return RES_STOP;
        }
    }
    (void)hal_haptic_stop();
    printf("PASS (all effects played; judge feel by ID, see docs/i2c-peripherals.md)\n");
    return RES_PASS;
}

// --- test 7: click latency -------------------------------------------------------------------

static result_t test_click_latency(void) {
    printf("\n== Test 7: click latency (20 clicks) ==\n");
    if (!haptic_ready()) {
        return RES_FAIL;
    }
    if (hal_haptic_click_prepare(1) != HAL_OK) {
        printf("FAIL: could not preload click\n");
        return RES_FAIL;
    }
    printf("(chip woken once before the loop; the standby wake-up time is not part of the numbers)\n");
    uint32_t mn = 0xFFFFFFFFu, mx = 0, sum = 0;
    const unsigned n = 20;
    for (unsigned i = 0; i < n; i++) {
        if (hal_haptic_click() != HAL_OK) {
            printf("FAIL: click %u: I2C error\n", i);
            return RES_FAIL;
        }
        uint32_t us = hal_haptic_last_click_us();
        mn = us < mn ? us : mn;
        mx = us > mx ? us : mx;
        sum += us;
        if (wait_ms(150, false)) {
            return RES_STOP;
        }
    }
    printf("call -> GO write complete at %u Hz: min %u us, avg %u us, max %u us\n",
           (unsigned)hal_i2c_get_baud(), (unsigned)mn, (unsigned)(sum / n), (unsigned)mx);
    printf("motor current / mechanical onset: not measurable on this testbed (no current sense or "
           "scope hook)\n");
    print_stats();
    printf("PASS\n");
    return RES_PASS;
}

// --- test 8: RTP -----------------------------------------------------------------------------

static result_t test_haptic_rtp(void) {
    printf("\n== Test 8: real-time playback ramp 0 -> 127 -> 0 over 2 s (signed format) ==\n");
    if (!haptic_ready()) {
        return RES_FAIL;
    }
    uint32_t t0 = hal_millis();
    hal_status_t st = hal_haptic_rtp_begin();
    bool stop = false;
    for (int i = 0; i <= 254 && st == HAL_OK && !stop; i++) {
        st = hal_haptic_rtp_set((int8_t)(i <= 127 ? i : 254 - i));
        stop = wait_ms(8, false);
    }
    hal_status_t end = hal_haptic_rtp_end(); // always back to internal trigger, amplitude 0
    if (end != HAL_OK) {
        end = hal_haptic_rtp_end(); // retry once
    }
    if (end != HAL_OK) {
        printf("WARNING: could not stop real-time playback (status %d): the motor may still be "
               "running. Pull EN low or power-cycle the DRV2605L breakout.\n", (int)end);
    }
    printf("ramp took %u ms\n", (unsigned)(hal_millis() - t0));
    if (st != HAL_OK || end != HAL_OK) {
        printf("FAIL: I2C error (%d / %d)\n", (int)st, (int)end);
        return RES_FAIL;
    }
    if (stop) {
        return RES_STOP;
    }
    printf("PASS\n");
    return RES_PASS;
}

// --- test 9: soak ----------------------------------------------------------------------------

static result_t test_soak(void) {
    const uint32_t duration_ms = 60000u;
    printf("\n== Test 9: soak 60 s (gauge read every 500 ms, click every 2 s) ==\n");
    if (!haptic_ready()) {
        return RES_FAIL;
    }
    bool gauge = hal_battery_init() == HAL_OK;
    if (!gauge) {
        printf("(gauge absent: only clicks are exercised)\n");
    }
    (void)hal_haptic_click_prepare(1);
    hal_i2c_reset_stats();
    unsigned reads = 0, read_fail = 0, clicks = 0, click_fail = 0;
    uint32_t t0 = hal_millis();
    for (unsigned tick = 0;; tick++) {
        uint32_t el = hal_millis() - t0;
        if (el >= duration_ms) {
            break;
        }
        if (gauge) {
            hal_battery_status_t b;
            reads++;
            read_fail += hal_battery_read(&b) != HAL_OK;
        }
        if (tick % 4u == 0) {
            clicks++;
            click_fail += hal_haptic_click() != HAL_OK;
        }
        if (tick % 20u == 19u) {
            printf("  %2u s: %u reads (%u failed), %u clicks (%u failed)\n", (unsigned)(el / 1000u),
                   reads, read_fail, clicks, click_fail);
        }
        uint32_t next = (tick + 1u) * 500u;
        uint32_t now = hal_millis() - t0;
        if (next > now && wait_ms(next - now, false)) {
            return RES_STOP;
        }
    }
    hal_i2c_stats_t s;
    hal_i2c_get_stats(&s);
    unsigned errors = s.nack_addr + s.nack_data + s.timeouts + s.arb_lost + s.bus_stuck;
    printf("%u gauge reads (%u failed), %u clicks (%u failed)\n", reads, read_fail, clicks, click_fail);
    print_stats();
    if (errors || s.recoveries || read_fail || click_fail) {
        printf("FAIL: %u I2C error(s), %u recovery(ies)\n", errors, (unsigned)s.recoveries);
        return RES_FAIL;
    }
    printf(gauge ? "PASS: zero I2C errors and zero recoveries\n"
                 : "PASS (gauge absent: clicks only): zero I2C errors and zero recoveries\n");
    return RES_PASS;
}

// --- menu ----------------------------------------------------------------------------------

static const struct {
    const char *name;
    result_t (*run)(void);
} k_tests[] = {
    {"bus scan", test_scan},
    {"gauge read", test_gauge},
    {"gauge under load", test_gauge_load},
    {"haptic ID", test_haptic_id},
    {"haptic calibrate", test_haptic_cal},
    {"haptic sweep", test_haptic_sweep},
    {"click latency", test_click_latency},
    {"haptic RTP", test_haptic_rtp},
    {"soak (60 s)", test_soak},
};
#define NUM_TESTS (sizeof k_tests / sizeof k_tests[0])

static void print_menu(void) {
    printf("\n=== neuPod I2C tests (MAX17048 0x36, DRV2605L 0x5A) ===\n");
    for (size_t k = 0; k < NUM_TESTS; k++) {
        printf("  %u  %s\n", (unsigned)(k + 1), k_tests[k].name);
    }
    printf("  a  run all with summary (sweep shortened to 1-10)   q  stop a running test / back\n"
           "  o  ERM loop open/closed (now: %s)   e  actuator ERM   l  actuator LRA (disabled unless built\n"
           "     with NEUPOD_I2C_TEST_ALLOW_LRA; now: %s)   h  or Enter: this menu\nkey: ",
           hal_haptic_get_erm_loop() == HAL_HAPTIC_LOOP_OPEN ? "OPEN" : "CLOSED",
           hal_haptic_get_actuator() == HAL_HAPTIC_ERM ? "ERM" : "LRA");
    fflush(stdout);
}

static const char *result_str(result_t r) {
    return r == RES_PASS ? "PASS" : r == RES_FAIL ? "FAIL" : r == RES_SKIP ? "SKIP" : "STOP";
}

static result_t run_test(size_t idx) {
    result_t r = k_tests[idx].run();
    if (s_haptic_awake) { // leave the driver in standby between tests
        (void)hal_haptic_stop();
        if (hal_haptic_standby(true) != HAL_OK) {
            printf("WARNING: could not put the haptic driver in standby\n");
        }
        s_haptic_awake = false;
    }
    size_t stack_free = hal_stack_free_min();
    if (stack_free != 0) {
        printf("[stack: at least %u bytes were always free]\n", (unsigned)stack_free);
    } else if (strcmp(hal_platform_name(), "rp2350") == 0) { // 0 = unknown on other platforms
        printf("[STACK EXHAUSTED: the whole painted stack was used; results are not trustworthy]\n");
    }
    return r;
}

static void set_actuator(hal_haptic_actuator_t a) {
#ifndef NEUPOD_I2C_TEST_ALLOW_LRA
    if (a == HAL_HAPTIC_LRA) {
        printf("l\nfitted actuator is ERM (Adafruit 1201); LRA mode disabled in this build "
               "(define NEUPOD_I2C_TEST_ALLOW_LRA to enable)\n");
        return;
    }
#endif
    printf("%c\n", a == HAL_HAPTIC_ERM ? 'e' : 'l');
    s_haptic_up = false; // re-initialised (and re-configured) by the next haptic test
    s_haptic_awake = false;
    hal_status_t st = hal_haptic_set_actuator(a); // not initialised here, so only records the choice
    if (st != HAL_OK) {
        printf("could not set the actuator (status %d)\n", (int)st);
        return;
    }
    (void)hal_haptic_standby(true);
    printf("actuator set to %s; calibration cleared if the type changed, run test 5 again\n",
           a == HAL_HAPTIC_ERM ? "ERM" : "LRA");
}

void i2c_tests_run_menu(void) {
    print_menu();
    for (;;) {
        int c = hal_console_getc_timeout(100);
        if (c < 0) {
            continue;
        }
        if (c >= '1' && c < '1' + (int)NUM_TESTS) {
            printf("%c\n", c);
            s_run_all = false;
            uint32_t t0 = hal_millis();
            result_t r = run_test((size_t)(c - '1'));
            printf("-> %s (%u ms)\n", result_str(r), (unsigned)(hal_millis() - t0));
            print_menu();
        } else if (c == 'a' || c == 'A') {
            printf("a\n");
            s_run_all = true;
            result_t res[NUM_TESTS];
            uint32_t ms[NUM_TESTS];
            size_t done = 0;
            for (; done < NUM_TESTS; done++) {
                uint32_t t0 = hal_millis();
                res[done] = run_test(done);
                ms[done] = hal_millis() - t0;
                if (res[done] == RES_STOP) {
                    done++;
                    break;
                }
            }
            s_run_all = false;
            printf("\n=== Summary ===\n #  %-18s result   ms\n", "test");
            for (size_t k = 0; k < done; k++) {
                printf(" %u  %-18s %-6s %7u%s\n", (unsigned)(k + 1), k_tests[k].name,
                       result_str(res[k]), (unsigned)ms[k], k == 5 ? "  (effects 1-10 only)" : "");
            }
            print_menu();
        } else if (c == 'o' || c == 'O') {
            bool to_open = hal_haptic_get_erm_loop() != HAL_HAPTIC_LOOP_OPEN;
            s_haptic_up = false; // re-initialised by the next haptic test
            s_haptic_awake = false;
            hal_status_t st = hal_haptic_set_erm_loop(to_open ? HAL_HAPTIC_LOOP_OPEN : HAL_HAPTIC_LOOP_CLOSED);
            if (st == HAL_OK) {
                (void)hal_haptic_standby(true);
            }
            uint8_t od = to_open ? drv2605l_erm_ol_od_clamp_reg(DRV_ERM_OD_MAX_MV)
                                 : drv2605l_erm_od_clamp_reg(DRV_ERM_OD_MAX_MV, DRV_ERM_DRIVE_TIME,
                                                             DRV_ERM_BLANKING_SEL, DRV_ERM_IDISS_SEL);
            printf("o\nERM loop now %s (status %d); OD_CLAMP will be %u\n", to_open ? "OPEN" : "CLOSED",
                   (int)st, (unsigned)od);
            print_menu();
        } else if (c == 'e' || c == 'E') {
            set_actuator(HAL_HAPTIC_ERM);
            print_menu();
        } else if (c == 'l' || c == 'L') {
            set_actuator(HAL_HAPTIC_LRA);
            print_menu();
        } else if (c == 'q' || c == 'Q' || c == 'h' || c == 'H' || c == '?' || c == '\r' ||
                   c == '\n') {
            print_menu();
        }
    }
}
