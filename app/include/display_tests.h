// SPDX-License-Identifier: MPL-2.0
/**
 * @file display_tests.h
 * @brief Interactive display bring-up test menu (portable; HAL-only).
 *
 * Output goes to stdout (UART on the device), keys come from hal_console. See docs/display-tests.md.
 */
#ifndef NEUPOD_DISPLAY_TESTS_H
#define NEUPOD_DISPLAY_TESTS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run the console-driven test menu forever. hal_system_init() and hal_display_init() must
 * have been called. Keys: 1-8 run a test, a = all (about 3 s each), q = back to the menu,
 * b/i/r/m/p = bring-up toggles (BGR, inversion, rotation, show state, corner pattern).
 */
void display_tests_run_menu(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_DISPLAY_TESTS_H
