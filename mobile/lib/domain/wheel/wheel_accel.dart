// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/wheel/firmware_constants.dart';

/// Wheel scroll acceleration: an exact port of `ui_accel_t`,
/// `ui_accel_init()` and `ui_accel_feed()` from `app/ui/ui_logic.c`.
///
/// The firmware's integer behaviour is kept: [energy] and the clock are
/// unsigned 32-bit (a clock that wraps past 2^32 ms, or goes backwards, reads
/// as a long pause), [residue] is signed 32-bit, and a step is
/// [uiCountsPerStep] counts, truncated toward zero. Counts are whole wheel
/// positions; the caller keeps any fraction of a position (the device's wheel
/// reports whole positions too).
///
/// [multiplier] is not in the C struct: it is the multiplier the last
/// [feed] used, kept for the caller (the event stream reports it). It does not
/// change what [feed] returns.
class WheelAccel {
  /// `ui_accel_init()`.
  WheelAccel();

  /// Counts not yet worth a step (signed).
  int residue = 0;

  /// Decaying count of recent movement (unsigned 32-bit).
  int energy = 0;

  /// The time of the last [feed], in milliseconds (unsigned 32-bit).
  int lastMs = 0;

  /// The multiplier (1, 2, 4 or 8) applied by the last [feed].
  int multiplier = 1;

  /// `ui_accel_init()`: back to rest.
  void reset() {
    residue = 0;
    energy = 0;
    lastMs = 0;
    multiplier = 1;
  }

  /// `ui_accel_feed()`: feeds [counts] signed wheel positions at [nowMs] and
  /// returns the whole list steps to move.
  int feed(int counts, int nowMs) {
    counts = i32(counts);
    nowMs = u32(nowMs);
    final dt = u32(nowMs - lastMs);
    lastMs = nowMs;
    // Linear decay of the recent-movement energy over the window.
    if (dt >= uiAccelWindowMs) {
      energy = 0;
    } else {
      energy = u32(energy * (uiAccelWindowMs - dt)) ~/ uiAccelWindowMs;
    }
    energy = u32(energy + (counts < 0 ? -counts : counts));
    final mult = energy >= uiAccelT3
        ? 8
        : energy >= uiAccelT2
        ? 4
        : energy >= uiAccelT1
        ? 2
        : 1;
    multiplier = mult;
    // A reversal drops the leftover counts of the old direction.
    if ((counts > 0 && residue < 0) || (counts < 0 && residue > 0)) {
      residue = 0;
    }
    residue = i32(residue + counts * mult);
    final steps = residue ~/ uiCountsPerStep; // truncates toward zero
    residue = i32(residue - steps * uiCountsPerStep);
    return steps;
  }
}
