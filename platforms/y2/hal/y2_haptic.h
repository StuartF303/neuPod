// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_haptic.h
 * @brief Y2 haptics logic: effect -> pulse length, the pulse-length table with its environment override, the
 *        coalescing gate and the timed_output write. Portable and host-tested; hal_haptic.c wires it to the HAL.
 *
 * The Y2 has an ERM motor behind /sys/class/timed_output/vibrator/enable: writing N switches it on for N ms (the
 * kernel stops it, nothing to switch off). An ERM needs time to spin up, so the table's defaults are a compromise
 * between "felt" and "snappy"; NEUPOD_Y2_HAPTIC_MS="tick,click,strong" retunes them without a rebuild.
 */
#ifndef NEUPOD_Y2_HAPTIC_H
#define NEUPOD_Y2_HAPTIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Y2_HAPTIC_PATH "/sys/class/timed_output/vibrator/enable"
#define Y2_HAPTIC_ENV "NEUPOD_Y2_HAPTIC_MS"

typedef enum { Y2_HAPTIC_TICK = 0, Y2_HAPTIC_CLICK = 1, Y2_HAPTIC_STRONG = 2, Y2_HAPTIC_KINDS = 3 } y2_haptic_kind_t;

/** Defaults (ms), one place: wheel tick, button click, strong. */
#define Y2_HAPTIC_DEFAULT_TICK_MS 15u
#define Y2_HAPTIC_DEFAULT_CLICK_MS 30u
#define Y2_HAPTIC_DEFAULT_STRONG_MS 60u
/** A configured pulse is limited to 1..Y2_HAPTIC_MAX_MS: a typo can never leave the motor running. */
#define Y2_HAPTIC_MAX_MS 500u
/** Motor rest after a pulse ends before the next may start, so a fast scroll is a run of separate taps, not a buzz. */
#define Y2_HAPTIC_REST_MS 45u

typedef struct {
    uint16_t ms[Y2_HAPTIC_KINDS];
} y2_haptic_table_t;

void y2_haptic_table_default(y2_haptic_table_t *t);

/**
 * Apply an override string "tick,click,strong" (decimal ms, 1..500). An empty or missing field keeps its default
 * ("20,,80" changes tick and strong). NULL or "" changes nothing. Returns how many fields were rejected (not a
 * number, out of range, or more than three fields); those keep their defaults.
 */
int y2_haptic_table_parse(y2_haptic_table_t *t, const char *s);

/** The DRV2605 ROM effect ids the app asks for, grouped: 30 % and tick families = tick, 100 % clicks and buzzes = strong. */
y2_haptic_kind_t y2_haptic_kind_for_effect(uint8_t effect_id);

/** Coalescing gate. A request is passed unless the previous pulse (plus the rest time) is still running. */
typedef struct {
    bool have;
    uint32_t free_at_ms; /**< earliest start of the next pulse */
} y2_haptic_gate_t;

/** True (and the gate advanced) when a pulse of @p ms may start at @p now_ms; false when it must be dropped. */
bool y2_haptic_gate_pass(y2_haptic_gate_t *g, uint32_t now_ms, uint16_t ms);

/** True when @p path can be opened for writing (the node exists and we may use it). */
bool y2_haptic_node_ok(const char *path);

/** Write "<ms>\n" to @p path (O_WRONLY | O_CLOEXEC, EINTR retried). One short write; does not block. */
bool y2_haptic_write(const char *path, unsigned ms);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_HAPTIC_H
