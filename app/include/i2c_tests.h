// SPDX-License-Identifier: MPL-2.0
/**
 * @file i2c_tests.h
 * @brief Interactive I2C bring-up test menu (MAX17048 fuel gauge, DRV2605L haptic driver).
 *
 * Output goes to stdout (UART on the device), keys come from hal_console. Tests: 1 scan, 2 gauge,
 * 3 gauge under load, 4 haptic ID, 5 haptic calibrate, 6 effect sweep, 7 click latency, 8 RTP, 9 soak.
 */
#ifndef NEUPOD_I2C_TESTS_H
#define NEUPOD_I2C_TESTS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run the console-driven test menu forever. hal_system_init() must have been called.
 * Keys: 1-9 run a test, a = all (with summary table), q = stop a running test / back to menu (any key stops the
 * sweep), e/l = ERM/LRA actuator, h/Enter = menu.
 */
void i2c_tests_run_menu(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_I2C_TESTS_H
