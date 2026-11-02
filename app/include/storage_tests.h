// SPDX-License-Identifier: MPL-2.0
/**
 * @file storage_tests.h
 * @brief Interactive microSD bring-up test menu (portable; HAL-only).
 *
 * Output goes to stdout (UART on the device), keys come from hal_console. Stage 1 implements
 * test 1 (detect, read-only); nothing in this stage writes to the card.
 */
#ifndef NEUPOD_STORAGE_TESTS_H
#define NEUPOD_STORAGE_TESTS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run the console-driven test menu forever. hal_system_init() must have been called.
 * Keys: 1-9 run a test, a = all (with summary table), q = stop, h/Enter = menu.
 */
void storage_tests_run_menu(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_STORAGE_TESTS_H
