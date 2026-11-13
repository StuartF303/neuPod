// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_charge.h
 * @brief Charging-only mode (the Y2 was powered on by USB while "off": MediaTek boot mode 8 or 9), portable half:
 *        charger presence from /sys/class/power_supply, the unplug debounce and the decision logic. No device access
 *        except sysfs reads under a root argument, so it is unit-tested on the PC. The loop that drives it, the
 *        backlight and the framebuffer are in platforms/y2/charging.c.
 *
 * Behaviour: Power held Y2_CHARGE_REBOOT_HOLD_MS asks for a reboot (once per press); the charger being offline continuously for
 * Y2_CHARGE_UNPLUG_MS asks for a power-off (once); a short Power press does nothing here (it wakes the screen: y2_chargeview.h).
 */
#ifndef NEUPOD_Y2_CHARGE_H
#define NEUPOD_Y2_CHARGE_H

#include <stdbool.h>
#include <stdint.h>

#include "y2_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The charger must have been offline this long, without a single online reading, before power-off is requested. */
#define Y2_CHARGE_UNPLUG_MS 3000u
/** Poll period of the charging loop (it sleeps in between, so the process idles near 0 % CPU). */
#define Y2_CHARGE_POLL_MS 250u

typedef enum {
    Y2_CHARGE_ONLINE_UNKNOWN = -1, /**< no readable charger supply */
    Y2_CHARGE_OFFLINE = 0,
    Y2_CHARGE_ONLINE = 1,
} y2_charge_online_t;

/**
 * Look at every supply under @p ps_root that is not the battery (type Mains, USB, Wireless ...: every non-battery supply)
 * and read its "online" file. ONLINE if any says 1, OFFLINE if at least one was readable and all said 0, UNKNOWN if none
 * could be read (a value other than 0 or 1 counts as unreadable) (the debounce treats UNKNOWN as "not proven offline", so a missing node never powers the device off).
 */
y2_charge_online_t y2_charge_read_online(const char *ps_root);

/**
 * Unplug debounce: offline since a moment, until it has lasted Y2_CHARGE_UNPLUG_MS. Not armed until one ONLINE reading
 * has been seen: the charger can read online=0 early in a charger boot (BC1.2 detection lag), and a charger that never
 * reads online leaves the device up (fail-safe on).
 */
typedef struct {
    bool seen_online; /**< an ONLINE reading has been seen: until then the timer is never armed */
    bool offline;
    uint32_t since_ms;
} y2_unplug_t;

void y2_unplug_init(y2_unplug_t *u);

/** Feed a reading at @p now_ms. True once the charger has been continuously offline for Y2_CHARGE_UNPLUG_MS. */
bool y2_unplug_update(y2_unplug_t *u, y2_charge_online_t online, uint32_t now_ms);

/** Power held this long (while still held) asks for a reboot in charging mode. (The player's Power gestures live in the UI: ui_hold.h.) */
#define Y2_CHARGE_REBOOT_HOLD_MS 2000u

typedef enum {
    Y2_CHARGE_NONE = 0,
    Y2_CHARGE_REBOOT,   /**< Power held 2 s */
    Y2_CHARGE_POWEROFF, /**< charger gone for 3 s */
} y2_charge_action_t;

typedef struct {
    y2_input_t in;
    y2_unplug_t unplug;
    bool reboot_sent;
    bool poweroff_sent;
    bool pw_down;    /**< KEY_POWER is held (pw_ms valid) */
    bool pw_fired;   /**< this press already counted as a hold */
    bool pw_hold;    /**< a hold is waiting to be reported by the next y2_charge_step() */
    uint32_t pw_ms;
} y2_charge_t;

void y2_charge_init(y2_charge_t *c);

/** Feed one decoded evdev event (Power key and friends) at @p now_ms. */
void y2_charge_feed(y2_charge_t *c, const y2_event_t *ev, uint32_t now_ms);

/**
 * One poll at @p now_ms with the current charger reading. Returns the action to take, each at most once until
 * y2_charge_failed(). Reboot takes priority over poweroff when both fire in the same poll.
 */
y2_charge_action_t y2_charge_step(y2_charge_t *c, uint32_t now_ms, y2_charge_online_t online);

/** How long Power has been held at @p now_ms (0 when it is not down); drives the "hold to restart" bar. */
uint32_t y2_charge_hold_ms(const y2_charge_t *c, uint32_t now_ms);

/** The command for @p action could not be started: allow it again (next press / after another 3 s offline). */
void y2_charge_failed(y2_charge_t *c, y2_charge_action_t action, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_CHARGE_H
