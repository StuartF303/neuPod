// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_suspend.h
 * @brief The Y2's system sleep (ADR-0021): the pieces of hal_power.c that need no device, so the PC can test them. Kernel 3.4.67, MediaTek, Android-style power
 *        management (/sys/power/{state,wake_lock,wake_unlock}, the /dev/alarm driver).
 *
 * How a sleep goes (hal_power.c runs it on a helper thread; this header holds its rules):
 *   awake:  the wake lock "neupod" is held (taken at hal_power_init()), so nothing else can suspend the system under the player.
 *   sleep:  1. /dev/alarm is armed with ANDROID_ALARM_RTC_WAKEUP at now + the limit (the 24 h power-off timer; the RTC runs while the system sleeps).
 *           2. wake_unlock "neupod" (no userspace lock left), then "mem" to state. With early suspend that REQUESTS a suspend: the early-suspend handlers
 *              (the panel, the touch controller) run, then the kernel suspends as soon as no wake lock is held. On a kernel where the write itself suspends and
 *              returns after the resume, it simply returns later. Unlocking first means the request is the last thing that changes, so there is no instant at
 *              which the state says "suspend" and our own lock still blocks it, nor one where the lock is gone and the state still says "on" (harmless either way).
 *           3. The helper watches: its own descriptors on the key, touch and ring input nodes (poll), and the two clocks. CLOCK_BOOTTIME keeps counting while the
 *              system is suspended and CLOCK_MONOTONIC (which hal_millis() and the FreeRTOS tick follow) does not: a growing difference is the time spent asleep.
 *   wake:   the first thing after the thaw is wake_lock "neupod", then "on" to state, then the alarm is cleared. After a wake the kernel would otherwise suspend
 *           again as soon as the waking device's own wake lock (the input event's, the alarm's 5 s lock) lets go, because the request "mem" still stands:
 *           our lock is what stops that, and "on" cancels the request and late-resumes the panel. The helper thread is runnable the moment the system thaws and the
 *           input event that woke it holds its device's wake lock until it is read, so the window is a few hundred microseconds inside a lock of seconds.
 *   If nothing suspends (some other wake lock is held), the step function gives up after Y2_SUSP_REFUSE_MS: the lock and "on" are put back, the alarm cleared,
 *   and hal_power_sleep() answers HAL_EBUSY. A key during the first seconds also ends it at once (the key is real: the core lights the screen for it).
 * hal_millis() and the FreeRTOS tick follow CLOCK_MONOTONIC and so do not count the sleep: nothing in the firmware sees a jump. (The POSIX port's tick is a thread
 * that sleeps in usleep(): after the thaw it resumes where it was.)
 *
 * UNVERIFIED on the device (the owner will do the bench step): that "mem" behaves as early suspend (or as a plain suspend), that the listed nodes wake the system,
 * that the panel and backlight come back after "on", and how long the whole cycle takes.
 */
#ifndef NEUPOD_Y2_SUSPEND_H
#define NEUPOD_Y2_SUSPEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>

#include "hal_power.h"

#ifdef __cplusplus
extern "C" {
#endif

#define Y2_SUSP_ENV "NEUPOD_SUSPEND"       /**< "0" (in /etc/neupod-fw.env) switches sleeping off: hal_power_can_sleep() is then false and the core powers off instead */
#define Y2_POWER_DIR "/sys/power"
#define Y2_ALARM_DEV "/dev/alarm"
#define Y2_WAKELOCK_NAME "neupod"

#define Y2_SUSP_GAP_MS 1500u        /**< time asleep (the clock difference) that counts as having been suspended */
#define Y2_SUSP_REFUSE_MS 12000u    /**< no suspend after this long awake since the request: something holds the system awake */
#define Y2_SUSP_TIMER_SLACK_MS 3000u /**< a wake this close to the limit is the alarm */

/** true unless @p value is "0": NEUPOD_SUSPEND unset, empty or anything else leaves sleeping on. */
bool y2_susp_env_enabled(const char *value);

/** Does the text of /sys/power/state (e.g. "mem disk") offer "mem"? */
bool y2_susp_state_has_mem(const char *text);

/** Write @p text to @p dir/@p file (open, write, close; no newline added). false on any failure. */
bool y2_susp_write(const char *dir, const char *file, const char *text);

/* ---- /dev/alarm (the Android alarm driver; ioctl numbers written out from its uapi header's layout: 'a', command | type << 4, argument a 32-bit-time timespec) ---- */

typedef struct {
    int32_t sec;
    int32_t nsec;
} y2_alarm_ts_t; /**< the kernel's struct timespec on this 32-bit ARM kernel (8 bytes) */

#define Y2_ALARM_RTC_WAKEUP 0 /**< alarm type: the real-time clock, wakes the system */
#define Y2_ALARM_CLEAR(type) _IO('a', 0 | ((type) << 4))
#define Y2_ALARM_SET(type) _IOW('a', 2 | ((type) << 4), y2_alarm_ts_t)
#define Y2_ALARM_GET_TIME(type) _IOW('a', 4 | ((type) << 4), y2_alarm_ts_t)

/**
 * Does the raw evdev data (16-byte records, see y2_input.h) hold a key or button PRESS (EV_KEY, value 1)? Releases, repeats, positions and sync records do
 * not count: a touch controller or key matrix may emit some of those while it powers down, and they must not abort a sleep that has only just begun.
 */
bool y2_susp_has_press(const uint8_t *buf, size_t len);

/** @p now plus @p add_ms, nanoseconds normalised. */
y2_alarm_ts_t y2_alarm_after(y2_alarm_ts_t now, uint32_t add_ms);

/* ---- the wait ---- */

typedef struct {
    uint32_t max_ms;     /**< the limit: asleep this long (on the boot clock) ends it with HAL_WAKE_TIMER */
    int64_t mono0_ms;    /**< CLOCK_MONOTONIC at the request */
    int64_t boot0_ms;    /**< CLOCK_BOOTTIME at the request */
    bool ext0;           /**< external power at the request */
    int64_t deadline_rtc_ms; /**< the alarm's time on the alarm driver's clock (the real-time clock), -1 if it could not be read */
} y2_susp_t;

typedef enum {
    Y2_SUSP_WAIT = 0, /**< keep waiting */
    Y2_SUSP_WOKE,     /**< finished: see y2_susp_verdict_t.why (slept is true if the system really suspended in between) */
    Y2_SUSP_REFUSED,  /**< the system never suspended (a wake lock elsewhere, or a request the kernel dropped) */
} y2_susp_state_t;

typedef struct {
    y2_susp_state_t state;
    hal_wake_reason_t why;
    bool slept;
    uint32_t slept_ms; /**< time asleep so far (the clock difference), for the log */
} y2_susp_verdict_t;

/** @p rtc_ms: the alarm driver's clock now in ms (ANDROID_ALARM_GET_TIME), or -1 if unreadable. */
void y2_susp_begin(y2_susp_t *s, uint32_t max_ms, int64_t mono_ms, int64_t boot_ms, int64_t rtc_ms, bool ext_online);

/**
 * One look: the clocks now (@p rtc_ms as for y2_susp_begin()), whether a watched node holds a key PRESS (@p input_ready), whether the wheel node (mtk-tpd) produced
 * anything at all (@p wheel_activity: counted as a key only once the system has been seen to sleep, because the panel may twitch while it powers down), whether
 * external power is online. The timer is decided by CLOCK_BOOTTIME against the limit, or, if that clock did not run on across the sleep, by the real-time clock
 * reaching the alarm's time. Pure.
 */
y2_susp_verdict_t y2_susp_step(const y2_susp_t *s, int64_t mono_ms, int64_t boot_ms, int64_t rtc_ms, bool input_ready, bool wheel_activity, bool ext_online);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_SUSPEND_H
