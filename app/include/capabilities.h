// SPDX-License-Identifier: MPL-2.0
/**
 * @file capabilities.h
 * @brief Which subsystems passed their boot self-check ("capabilities") and why not otherwise.
 *
 * Policy (the user's): boot always continues. A subsystem that is not fully correct is marked
 * DEGRADED, and the UI shows its icon with a cross and disables the dependent features. The
 * first resolution of a capability wins, so a check that finishes after its timeout cannot
 * flip the answer the UI has already acted on.
 */
#ifndef NEUPOD_CAPABILITIES_H
#define NEUPOD_CAPABILITIES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAP_DISPLAY,
    CAP_STORAGE,
    CAP_BATTERY,
    CAP_HAPTICS,
    CAP_WHEEL,
    CAP_AUDIO,
    CAP_BLUETOOTH,
    CAP_WIFI,
    CAP_COUNT
} cap_id_t;

typedef enum {
    CAP_PENDING = 0, /**< check still running */
    CAP_OK,          /**< fully correct */
    CAP_DEGRADED,    /**< absent, failed, timed out, or not implemented yet */
} cap_state_t;

#define CAP_TEXT_LEN 64
#define CAP_BRIEF_LEN 32

typedef struct {
    cap_state_t state[CAP_COUNT];
    char text[CAP_COUNT][CAP_TEXT_LEN];   /**< reason when DEGRADED, a short fact when OK (UART) */
    char brief[CAP_COUNT][CAP_BRIEF_LEN]; /**< <= 24-char screen caption for a DEGRADED capability */
} capabilities_t;

/** Reset everything to PENDING and create the lock. Call once before any other caps_* call. */
void caps_init(void);

/**
 * Record the outcome of one check (first call per capability wins; later ones are ignored and
 * return false). @p brief is the short screen caption for a failure (NULL when @p ok); @p fmt
 * describes the full reason (or a short fact when @p ok). Prints one console line, which is the
 * per-failure reason the boot policy asks for.
 */
bool caps_resolve(cap_id_t id, bool ok, const char *brief, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/**
 * Like caps_resolve(id, false, ...) for a check that missed its deadline, but a later caps_resolve(ok)
 * for the same capability may still upgrade it to OK (logged as a late result). No-op unless the
 * capability is still pending.
 */
bool caps_resolve_timeout(cap_id_t id, const char *brief, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/** Copy of the current state. */
void caps_get(capabilities_t *out);

/** True only when the capability is OK (PENDING counts as not available). */
bool caps_available(cap_id_t id);

/** True once no capability is PENDING. */
bool caps_all_resolved(void);

const char *cap_name(cap_id_t id);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_CAPABILITIES_H
