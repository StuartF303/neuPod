# ADR-0021: Power: sleep when idle, power off after a day, power off when the battery is empty

Status: **accepted** (owner decisions 15 Nov 2026; the four rules in "Decision" were set by the lead and are marked as such)

## Context

The player must be able to sit on a shelf for a week and still power up, and the owner must never have to remember to switch it off. [ADR-0020](0020-hold.md)
turns the screen off and locks after a few minutes, but the rest of the machine keeps running: the CPU, the Bluetooth controller, the card. The Y2 testbed draws
tens of milliamps in that state, which is a flat battery in a few days.

## Decision

**Idle means suspend, a day of suspend means power off, an empty battery means power off.** One pure policy decides; each platform says what it can do.

* **Idle.** After **Sleep after** with nothing happening the player sleeps. Settings > Display > **Sleep after**: Never, 10, **30 (default)**, 60 min, saved
  (`NPsl`; an older settings file has no such item and gives the default, so the settings file version is unchanged). Idle is counted by `app/power/power_policy.c`
  (unit tested, `power_policy`) from the last input of any kind (a key, the wheel, the touch panel, including the input that wakes the screen).
  * **Paused counts as idle** (lead decision): a paused player sleeps; its place is already saved (ADR resume), and Play after the wake continues from it.
  * **Playing does not**: while audio plays the count is held at zero, so after a two-hour album the 30 minutes start when it ends, not when the last key was pressed.
  * **Busy does not**: a library build or scan, a Bluetooth pairing, connect or scan, a Wi-Fi join, a haptic calibration, or a card request or write in flight.
    There is no USB file transfer on any platform yet; it belongs in the same list when one appears.
  * **Never on external power** (lead decision): USB or a charger holds the count at zero (`hal_power_external()`).
* **A day asleep: power off** (lead decision). The sleep is asked for with a 24 hour limit; when the limit passes (`HAL_WAKE_TIMER`) the core takes the normal
  power-off path. The resume position is saved across boots, so the next power-on comes back where the player was.
* **Empty battery: clean power-off** (lead decision). Off external power, a valid battery reading at or below **3 %** (or a cell below 3.3 V) for **30 s in a row**
  shows "Battery empty" for 3 s, writes the settings out and takes the normal power-off path. An unknown reading, a missing gauge or a charger resets the streak:
  unknown is never empty. The rule does not depend on idleness.
* **Where the policy meets the platform.** `hal_power.h`: `hal_power_can_sleep()`, `hal_power_sleep(max_ms, &why)` (blocks; why = key, timer, external power, other),
  `hal_power_external()`. A platform that cannot sleep answers `can_sleep` false and the core, after the same idle time, calls `hal_system_poweroff()` where
  `hal_system_can_poweroff()` is true (the RP2350 AMOLED board and the ESP32-S3 board have a soft latch, so they power off), and does nothing where it is not (the policy
  does not even ask: `power_service_can_act()`).

### The sleep sequence (UI task, `ui_frame.c` and `power_service.c`)

The UI task owns it: it owns the idle clock, Hold and the display. In order:

1. `power_service_sleep_prepare()`: nothing is playing (checked again); the Bluetooth stream stops and the controller is powered off through the stack-neutral radio
   interface (`hal_bt_power(false)`, new in `hal_radio.h`; BTstack: `hci_power_control(OFF)`, which on the Y2 closes `/dev/stpbt` and so powers the chip's Bluetooth
   function off), remembering whether a headset was linked; the settings (which hold the resume position) are written out and the card synced, waiting at most 4 s for the
   storage task to be idle. If the card stays busy the sleep is dropped (a write in flight must not be cut by a suspend), Bluetooth is put back, and the count restarts.
   Wired audio needs nothing: the output stages power down a few seconds after the last sound, long before 10 minutes (`docs/audio.md`).
2. The screen goes off exactly as the idle stage does it (forced off, Hold on under Auto-lock), then `hal_display_sleep(true)`.
3. `hal_power_sleep(24 h)`.
4. The wake: `hal_display_sleep(false)`; the Bluetooth controller back on if it was, and, **if a headset was linked**, the existing power-up auto-reconnect runs (its 5 minutes
   of tries, "<name> connected" toast); the idle count restarts. Then by reason:
   * **Key**: the screen stays dark and locked as it was. The input that woke the system is still queued for the input task and arrives like any input that wakes a dark
     screen: it only lights the screen and is **not delivered**, locked or not ([ADR-0020](0020-hold.md)). If it never arrives (2 s) the screen lights by itself.
   * **Timer**: the normal power-off path. If the platform cannot power off, it carries on awake.
   * **External power / other / the system would not sleep**: nobody pressed anything, so the screen lights at once.

### Y2 (Linux 3.4.67, MediaTek, Android-style power management)

`hal_power_can_sleep()` is true when `NEUPOD_SUSPEND` is not `0` (`/etc/neupod-fw.env`, the kill switch: `0` makes the core power off instead), `/sys/power/state` offers
`mem`, and `/dev/alarm` opens. The firmware holds the wake lock `neupod` while awake. A sleep, on a helper thread (not an RTOS task; the signal and RTOS rules of the Bluetooth
thread apply): arm `/dev/alarm` with `ANDROID_ALARM_RTC_WAKEUP` at now + 24 h (`ANDROID_ALARM_SET`; the ioctl numbers are written out locally from the driver's uapi
layout, `y2_suspend.h`), `wake_unlock neupod`, then `mem` to `/sys/power/state`; wait on the key, touch and ring nodes (own descriptors, `poll`) and on the two clocks
(CLOCK_BOOTTIME keeps counting while suspended, CLOCK_MONOTONIC does not: the difference is the time slept). The thaw: `wake_lock neupod` first, then `on` to state (cancels
the standing suspend request and late-resumes the panel), then the alarm is cleared. The race this answers: after any wake the kernel re-suspends as soon as the waking
device's own wake lock lets go, because the request `mem` still stands; our lock, retaken within microseconds of the thaw (the helper is runnable the moment userspace
thaws, and the input event holds its device's lock until read), is what stops that, and `on` ends the request. If nothing suspends within 12 s (another wake lock) the lock and `on`
are put back and the call answers `HAL_EBUSY`. Only a key *press* counts as input during the first seconds, because a controller powering down may emit releases or positions.
`hal_millis()`, `hal_micros()` and the FreeRTOS tick (a thread sleeping in `usleep`) follow CLOCK_MONOTONIC and so never see the sleep: nothing jumps, nothing stalls. External
power is `power_supply/{usb,ac}/online`; a plug-in during sleep ends it (if the hardware wakes on it, a bonus).
Plugged into a PC over USB the Y2 reports external power and therefore never sleeps: test the suspend on battery.

### Other platforms

* **Host**: simulated (`hal/host/hal_power.c`): the sleep logs and blocks until something is pressed at the window, or `NEUPOD_HOST_SLEEP_WAKE_MS` (an injected Select press),
  or `NEUPOD_HOST_SLEEP_MAX_MS` (timer); `NEUPOD_SUSPEND=0` turns it off; `NEUPOD_HOST_EXTERNAL_POWER=1` simulates a charger. Tour tokens `idlesleep:N` (sleep once after N s of idleness),
  `batt:N` (the gauge reads N %) and `ext:on` / `ext:off`.
* **RP2350 AMOLED board, ESP32-S3**: `hal/common/hal_power_none.c`: no suspend yet, so after the idle time they power off (soft latch).
* **Pico 2 W** (frozen, about 100 B of static RAM spare): the whole feature is compiled out (`NEUPOD_POWER_MGMT=0` from the top-level `CMakeLists.txt`: no setting, no row, no
  policy, no sequence, no `hal_bt_power`). It can neither sleep nor switch itself off, so there is nothing for it to do.

### Failure paths (review fixes)

* **A sleep that did not happen** (`HAL_EBUSY` / `HAL_ERR`): the screen is not lit for it. Everything is put back as it was before the sleep (Bluetooth on, the screen off and locked if
  it was, otherwise lit and unlocked again), and the count restarts. **Two failures in a row stop further tries for an hour** (`power_guard_*`, unit tested). Only an
  external-power or unknown wake lights the screen by itself.
* **A last look before the screen goes**: if an input is queued or a charger appeared during the preparation (which can take seconds), the sleep is abandoned, Bluetooth is put back and the
  count restarts; the input is handled as usual.
* **A power-off the platform refuses** (`hal_system_poweroff()` false although it says it can: a USB-powered dev board, where the latch cannot cut power) is latched as "external
  power": nothing tries again (no more GPIO pulses or blocked UI) until the gauge shows a discharge (rate of at least -2000 milli-percent/h, the UI's own "discharging" threshold, or the
  cell 100 mV lower than at the refusal) or 6 h pass. The same latch, and a gauge rate above +2000 (the Y2's rate is synthetic, plus or minus 10 %/h from the supply status; the MAX17048's is its CRATE
  register: both are the figures the UI already uses for "charging"), count as external power for the battery rule too. A refused critical power-off withdraws the "Battery empty" notice and logs
  "on external power".
* **Y2, a firmware stopped mid-request.** `state` stays `mem` if the firmware is killed after the request and before the thaw, and the next start would find the panel early-suspended.
  `hal_power_init()` therefore writes `on` right after taking the wake lock (idempotent), and the exit handler writes `on` before releasing the lock.
* **Y2, the timer.** Decided by CLOCK_BOOTTIME reaching the limit, or, if that clock did not run on across the sleep, by the alarm driver's own clock (`ANDROID_ALARM_GET_TIME`) reaching the
  armed time. Any event at all from the wheel node (`mtk-tpd`) after the system has been seen to sleep is a key wake (dark, locked, not delivered); before that only a press counts.
* **Stale events.** An event queued shortly before the sleep may light the screen just before the wake key does: harmless (the wake key is swallowed either way).
* `NEUPOD_SLEEP_MAX_S` (Y2, bench only, in `/etc/neupod-fw.env`) shortens the 24 h limit so the timer wake can be tried in minutes.
* With no card (RAM-only settings) the preparation does not wait for a flush; it re-checks that the storage task is idle after the card sync, right before suspending.

## Consequences

* **Failure stance.** Unknown battery, a failed gauge, an unreadable charger sensor: never power off or sleep *because of it* (the battery rule needs a valid reading; "external power
  unknown" counts as not on a charger, which only matters for the sleep, and the sleep is always safe to repeat). A sleep that cannot be prepared or entered is abandoned, nothing
  half done is left (Bluetooth is put back), and the count restarts. The kill switch fails to the power-off path.
* **RAM budget:** hosted and ESP32 builds: a few hundred bytes of statics in the UI (wake and notice state) and the power service (policy state, 4 bytes of flags); the setting
  sits in the padding of `ui_state_t`. Pico 2 W: 0 (compiled out). Measured sizes are in the pull request.
* **Unverified on the device** (the owner does the bench step with the unit on battery): that `mem` behaves as early suspend or as a plain suspend, that the key, touch and ring
  nodes wake the system, that the panel and backlight come back after `on` (the display HAL re-pushes the whole picture and re-writes the backlight on wake), the Bluetooth
  chip's power cycle, the real draw in suspend, and the length of the whole cycle. See `docs/bench.md`.
* Tests: `power_policy` (every rule, the wrap, a stamp ahead, a clock that jumps back), `ui_frame_keys` (the screen's part of the sequence with fakes), `y2_suspend` (kill switch,
  sysfs writes, ioctl numbers, the verdicts from the clocks), `y2_power` (external power), and the host tours `power_tour`, `power_off_tour`, `power_nosuspend_tour`,
  `power_charger_tour`, `power_battery_tour`.
