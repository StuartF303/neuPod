// SPDX-License-Identifier: MPL-2.0

/// The firmware's wheel constants, copied from the C headers so the app and the
/// device behave the same. Keep in step with:
///
/// - `hal/include/hal_input.h`: [halWheelPositions];
/// - `app/include/runtime_config.h`: [uiCountsPerStep], [uiAccelWindowMs],
///   [uiAccelT1], [uiAccelT2], [uiAccelT3];
/// - `app/ui/ui_logic.h`: the Now Playing timings ([uiSeekIdleMs] and on);
/// - `app/include/wheel_input.h`: the letter-jump overlay ([wiLjFastSteps] and
///   on).
///
/// `test/domain/wheel/firmware_constants_test.dart` reads the headers and
/// fails if a value here drifts from the firmware.
library;

/// Discrete finger positions around the wheel (`HAL_WHEEL_POSITIONS`).
const int halWheelPositions = 96;

/// Wheel counts (positions) per list step (`UI_COUNTS_PER_STEP`).
const int uiCountsPerStep = 4;

/// Acceleration: recent-movement energy decays to zero over this window
/// (`UI_ACCEL_WINDOW_MS`).
const int uiAccelWindowMs = 150;

/// Counts in the window for 2x (`UI_ACCEL_T1`).
const int uiAccelT1 = 12;

/// Counts in the window for 4x (`UI_ACCEL_T2`).
const int uiAccelT2 = 24;

/// Counts in the window for 8x (`UI_ACCEL_T3`).
const int uiAccelT3 = 40;

/// Seek mode is cancelled (nothing applied) this long after its last input
/// (`UI_SEEK_IDLE_MS`).
const int uiSeekIdleMs = 10000;

/// A Previous / Next scrub is applied this long after it stops
/// (`UI_SEEK_COMMIT_MS`).
const int uiSeekCommitMs = 350;

/// Select held this long on Now Playing enters seek mode (`UI_SELHOLD_MS`).
const int uiSelHoldMs = 500;

/// Previous / Next held this long scans instead of skipping (`UI_HOLD_MS`).
const int uiHoldMs = 600;

/// Scan speed: track seconds per second held (`UI_FF_SPEED`).
const int uiFfSpeed = 10;

/// A hold ends itself this long after the key went down (`UI_HOLD_MAX_MS`).
const int uiHoldMaxMs = 30000;

/// Letter-jump overlay: steps inside the window that count as fast
/// (`WI_LJ_FAST_STEPS`).
const int wiLjFastSteps = 6;

/// Letter-jump overlay: a gap longer than this restarts the step count
/// (`WI_LJ_WINDOW_MS`).
const int wiLjWindowMs = 400;

/// Letter-jump overlay: hidden after this long without scrolling
/// (`WI_LJ_HIDE_MS`).
const int wiLjHideMs = 250;

/// Letter-jump overlay fade-out (`FADE_MS` in `app/ui/ui_rail.c`).
const int uiRailFadeMs = 150;

/// Wraps [v] to an unsigned 32-bit value, as C's `uint32_t` arithmetic does.
int u32(int v) => v & 0xFFFFFFFF;

/// Wraps [v] to a signed 32-bit value, as C's `int32_t` (two's complement).
int i32(int v) => v.toSigned(32);
