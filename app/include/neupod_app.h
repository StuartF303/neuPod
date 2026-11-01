// SPDX-License-Identifier: MPL-2.0
/**
 * @file neupod_app.h
 * @brief Portable application entry point, called by each platform's main().
 */
#ifndef NEUPOD_APP_H
#define NEUPOD_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/** Run the application. hal_system_init() has already been called. Does not return. */
void neupod_app_run(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_APP_H
