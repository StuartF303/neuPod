// SPDX-License-Identifier: MPL-2.0
/**
 * @file ui_licence_data.h
 * @brief The third-party components and their licences, as the firmware's Settings > About > Licences screen lists them. No LVGL (host-unit-tested).
 *
 * Standing rule of the project: licence notes are kept up to date. This table is the on-device copy of the table in LICENSE.md; a host test
 * (tests/test_licences.c) checks every entry against LICENSE.md (the component is named there, on a row that also carries the licence token),
 * and that the notices fit on one screen at every UI profile. Add a component here when you add it to the build (and to LICENSE.md).
 */
#ifndef NEUPOD_UI_LICENCE_DATA_H
#define NEUPOD_UI_LICENCE_DATA_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;     /**< row label: "FreeRTOS kernel" */
    const char *licence;  /**< short licence name, the row's right-hand text and the notice's headline */
    const char *notice;   /**< what to know, plain sentences, at most UI_LICENCE_NOTICE_MAX characters (it is drawn without scrolling) */
    const char *key;      /**< how LICENSE.md names the component (checked by the test) */
    const char *token;    /**< a word that LICENSE.md's row for the component must contain (checked by the test) */
    bool rp2350_only;     /**< linked into the RP2350 firmware only (Wi-Fi stack, SD driver, SDK): hidden on the Y2 */
} ui_licence_t;

#define UI_LICENCE_NOTICE_MAX 400u

/** Components shown on @p platform ("rp2350", "y2", "host", ...): the RP2350-only ones appear on "rp2350" and "host" (the simulator carries them for the tour). */
size_t ui_licence_count(const char *platform);
/** The @p i-th component shown on @p platform, NULL if out of range. */
const ui_licence_t *ui_licence_get(const char *platform, size_t i);
/** Every entry, platform filter not applied (tests). */
const ui_licence_t *ui_licence_all(size_t *count);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_UI_LICENCE_DATA_H
