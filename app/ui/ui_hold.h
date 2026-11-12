// SPDX-License-Identifier: MPL-2.0
/**
 * @file ui_hold.h
 * @brief Hold (the iPod's lock switch) and the Power-key gestures, with no LVGL dependency (host-unit-tested; docs/adr/0020-hold.md).
 *
 * Power key: a tap (released before UI_HOLD_RING_DELAY_MS) puts the screen to sleep; a longer press shows a ring that fills towards the toggle and releasing early does nothing; held UI_HOLD_TOGGLE_MS it toggles Hold (once, while still
 * held); held on to UI_HOLD_SHUTDOWN_MS it asks for a power-off, with a countdown from UI_HOLD_COUNTDOWN_MS before that (releasing cancels). The
 * model only keeps the state and says what happened; the screen frame (ui_frame.c) draws the feedback and calls the HAL.
 * Locked state: set by the gesture, by the hardware slide switch (INPUT_HOLD_SWITCH), by the console key and by the screen going off with
 * auto-lock on. All times are the UI tick clock in milliseconds (wrap-safe).
 */
#ifndef NEUPOD_UI_HOLD_H
#define NEUPOD_UI_HOLD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_HOLD_TOGGLE_MS 2000u     /**< Power held this long toggles Hold */
#define UI_HOLD_SHUTDOWN_MS 10000u  /**< ... and this long asks for a power-off */
#define UI_HOLD_COUNTDOWN_MS 5000u  /**< the power-off countdown is shown for the last this-many ms before the shutdown (5 s left) */
#define UI_HOLD_RING_DELAY_MS 400u  /**< a press shorter than this is a tap (screen sleep); the progress ring appears after it, and releasing then does nothing */
#define UI_HOLD_TOAST_MS 1500u      /**< how long a lock / unlock confirmation or the "Locked" reminder stays */

/** What happened (a bit set returned by the calls below). */
enum {
    UI_HOLD_NONE = 0,
    UI_HOLD_LOCKED = 1u << 0,    /**< Hold turned on: confirm it (toast, haptic click) */
    UI_HOLD_UNLOCKED = 1u << 1,  /**< Hold turned off */
    UI_HOLD_POWEROFF = 1u << 2,  /**< the 10 s hold completed: ask the HAL to power off (only returned while the platform can) */
    UI_HOLD_SLEEP = 1u << 3,     /**< a short Power press: screen off now */
    UI_HOLD_CANCELLED = 1u << 4, /**< the key was released while a ring or countdown was showing: take the overlay down */
};

typedef struct {
    bool locked;
    bool down;          /**< Power is held (down_ms valid) */
    uint32_t down_ms;
    bool woke;          /**< this press woke the screen: its release must not put it back to sleep */
    bool toggled;       /**< this hold already toggled Hold */
    bool poweroff_sent; /**< this hold already asked for the power-off */
} ui_hold_t;

void ui_hold_init(ui_hold_t *h);

/** Power went down at @p now_ms; @p woke: the screen was off and this press turned it on. A second DOWN without an UP is ignored. */
uint32_t ui_hold_power_down(ui_hold_t *h, uint32_t now_ms, bool woke);
/**
 * Power went up. A hold that reached the toggle time without a tick (a late poll) still toggles; one that reached the shutdown time without a
 * tick does NOT power off (a release is never a power-off). A tap (shorter than UI_HOLD_RING_DELAY_MS) that did not wake the screen is UI_HOLD_SLEEP; a longer press released before the toggle time does nothing.
 */
uint32_t ui_hold_power_up(ui_hold_t *h, uint32_t now_ms);
/** Forget a held press with no side effects (the input source went away). */
void ui_hold_power_lost(ui_hold_t *h);
/** Advance the hold timers (every UI pass): the toggle at UI_HOLD_TOGGLE_MS and, when @p can_poweroff, the power-off request at UI_HOLD_SHUTDOWN_MS. */
uint32_t ui_hold_tick(ui_hold_t *h, uint32_t now_ms, bool can_poweroff);

/** Set the locked state from elsewhere (the slide switch, auto-lock): UI_HOLD_LOCKED / UI_HOLD_UNLOCKED, or NONE if it did not change. */
uint32_t ui_hold_set(ui_hold_t *h, bool locked);
/** Flip it (console key). */
uint32_t ui_hold_toggle(ui_hold_t *h);

typedef enum {
    UI_HOLD_P_NONE = 0,
    UI_HOLD_P_RING,      /**< Power is held towards a Hold toggle: pct 0..100 of UI_HOLD_TOGGLE_MS; @c to_lock says which way */
    UI_HOLD_P_COUNTDOWN, /**< Power is held towards a power-off: secs left (5..1), pct of the countdown remaining */
    UI_HOLD_P_POWEROFF,  /**< the power-off was requested: "Powering off" */
} ui_hold_phase_t;

typedef struct {
    ui_hold_phase_t phase;
    uint8_t pct;
    uint8_t secs;
    bool to_lock; /**< RING: the toggle will lock (it is unlocked now) */
} ui_hold_progress_t;

/** What the overlay should show for the current press. @p can_poweroff false hides the countdown (the key can then only toggle Hold). */
ui_hold_progress_t ui_hold_progress(const ui_hold_t *h, uint32_t now_ms, bool can_poweroff);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_UI_HOLD_H
