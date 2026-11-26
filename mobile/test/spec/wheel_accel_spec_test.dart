// SPDX-License-Identifier: MPL-2.0

// Spec suite: wheel acceleration, a port of the firmware's ui_accel_feed()
// (app/ui/ui_logic.c, constants in app/include/runtime_config.h), as the
// device view design asks (docs/design/2026-11-21-app-device-view, section 5:
// "The app ports the firmware's logic rather than imitating it").
//
// The firmware behaviour, from the C:
// - 4 positions make one list step (UI_COUNTS_PER_STEP);
// - an energy value gains |positions| on every feed and decays linearly to
//   zero over 150 ms (UI_ACCEL_WINDOW_MS): energy = energy * (150 - dt) / 150
//   in integer arithmetic, and 0 once dt >= 150;
// - the multiplier comes from the energy after the feed: 12 -> 2x, 24 -> 4x,
//   40 -> 8x (UI_ACCEL_T1..T3), else 1x;
// - a feed in the other direction from the leftover positions drops them;
// - steps = leftover / 4, truncated toward zero; the rest stays as leftover;
// - a fresh state has last time 0, energy 0, leftover 0.
//
// The first three tests mirror tests/test_ui_logic.c exactly. The derived
// sequences were checked against the C compiled on the host (22 November
// 2026).
//
// WIRING: wiredWheelAccel is the app's port (lib/domain/wheel/wheel_accel.dart).
@Tags(['spec'])
library;

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/wheel_accel.dart' as app;

/// The app's port of ui_accel_t / ui_accel_feed().
abstract interface class WheelAccel {
  /// Feeds [positions] (signed whole wheel positions, clockwise positive) at
  /// [nowMs] (a monotonic clock in milliseconds) and returns the whole list
  /// steps to move (signed).
  int feed(int positions, int nowMs);
}

/// A fresh accelerator, as after ui_accel_init().
typedef WheelAccelFactory = WheelAccel Function();

/// The app's port (`lib/domain/wheel/wheel_accel.dart`), fresh as after
/// `ui_accel_init()`.
final class _AppAccel implements WheelAccel {
  final _a = app.WheelAccel();

  @override
  int feed(int positions, int nowMs) => _a.feed(positions, nowMs);
}

final WheelAccelFactory wiredWheelAccel = _AppAccel.new;

/// Feeds (positions, ms) pairs to a fresh accelerator; returns every result.
List<int> _run(List<(int, int)> feeds) {
  final a = wiredWheelAccel();
  return [for (final (c, t) in feeds) a.feed(c, t)];
}

void main() {
  group('ui_accel_feed port', () {
    group('mirrors tests/test_ui_logic.c', () {
      test('accel_slow_is_one_step_per_four_counts', () {
        final a = wiredWheelAccel();
        var t = 1000;
        var total = 0;
        for (var i = 0; i < 8; i++) {
          // one count every 400 ms: far below the acceleration window
          total += a.feed(1, t);
          t += 400;
        }
        expect(total, 2);
        expect(a.feed(4, t + 1000), 1, reason: 'a key press (+4) = one step');
        expect(a.feed(-4, t + 2000), -1);
      });

      test('accel_fast_multiplies', () {
        final a = wiredWheelAccel();
        var total = 0;
        var t = 5000;
        for (var i = 0; i < 20; i++) {
          // 4 counts every 10 ms
          total += a.feed(4, t);
          t += 10;
        }
        expect(total, greaterThan(20), reason: 'plain speed gives 20 steps');
        expect(total, lessThanOrEqualTo(20 * 8));
        expect(a.feed(4, t + 1000), 1, reason: 'after a pause, normal speed');
      });

      test('accel_reversal_drops_residue', () {
        final a = wiredWheelAccel();
        expect(a.feed(3, 1000), 0, reason: 'leftover +3');
        expect(
          a.feed(-1, 3000),
          0,
          reason: 'reversed: leftover restarts at -1, not +2',
        );
        expect(a.feed(-3, 5000), -1);
      });
    });

    group('exact values from the C', () {
      test('slow single counts: a step on every fourth', () {
        expect(_run([for (var i = 0; i < 8; i++) (1, 1000 + 400 * i)]), [
          0,
          0,
          0,
          1,
          0,
          0,
          0,
          1,
        ]);
      });

      test('4 counts every 10 ms: the multiplier climbs 1x, 2x, 4x', () {
        expect(_run([for (var i = 0; i < 20; i++) (4, 5000 + 10 * i)]), [
          1,
          1,
          1,
          2,
          2,
          2,
          2,
          2,
          4,
          4,
          4,
          4,
          4,
          4,
          4,
          4,
          4,
          4,
          4,
          4,
        ]);
      });

      test('4 counts every 10 ms: 61 steps in all, then 1 after a pause', () {
        final a = wiredWheelAccel();
        var total = 0;
        for (var i = 0; i < 20; i++) {
          total += a.feed(4, 5000 + 10 * i);
        }
        expect(total, 61);
        expect(a.feed(4, 6200), 1);
      });

      test('8 counts every 10 ms: 1x, 2x past 12, 4x past 24, never 40', () {
        // energies 8, 15, 22, 28, 34, 39
        expect(_run([for (var i = 0; i < 6; i++) (8, 1000 + 10 * i)]), [
          2,
          4,
          4,
          8,
          8,
          8,
        ]);
      });

      test('20 and 20 at the same instant: energy 40 gives 8x', () {
        expect(_run([(20, 1000), (20, 1000)]), [10, 40]);
      });

      test('2 counts every 16 ms stays at 1x (energy settles at 10)', () {
        expect(_run([for (var i = 0; i < 12; i++) (2, 1000 + 16 * i)]), [
          0,
          1,
          0,
          1,
          0,
          1,
          0,
          1,
          0,
          1,
          0,
          1,
        ]);
      });

      test('a slow full turn (96 positions) is 24 steps', () {
        final a = wiredWheelAccel();
        var total = 0;
        for (var i = 0; i < 96; i++) {
          total += a.feed(1, 1000 + 200 * i);
        }
        expect(total, 24);
      });
    });

    group('thresholds 12 / 24 / 40 give 2x / 4x / 8x', () {
      // One feed after rest: the energy is the feed itself.
      final cases = <(int, int, String)>[
        (11, 2, '11 at 1x: 11 / 4 = 2 (3 left over)'),
        (12, 6, '12 at 2x: 24 / 4'),
        (23, 11, '23 at 2x: 46 / 4 = 11 (2 left over)'),
        (24, 24, '24 at 4x: 96 / 4'),
        (39, 39, '39 at 4x: 156 / 4'),
        (40, 80, '40 at 8x: 320 / 4'),
        (-12, -6, '-12 at 2x: the energy counts the size, not the sign'),
      ];
      for (final (counts, steps, why) in cases) {
        test(why, () => expect(_run([(counts, 1000)]), [steps]));
      }
    });

    group('linear decay over 150 ms', () {
      // 10 counts (2 steps, 2 left over), then 6 more after dt ms.
      final cases = <(int, int, String)>[
        (0, 3, 'dt 0: energy 16, 2x: (2 + 12) / 4 = 3'),
        (30, 3, 'dt 30: energy 10 * 120 / 150 + 6 = 14, 2x'),
        (75, 2, 'dt 75: energy 10 * 75 / 150 + 6 = 11, 1x: (2 + 6) / 4'),
        (149, 2, 'dt 149: energy 10 * 1 / 150 = 0 (integer), + 6, 1x'),
        (150, 2, 'dt 150: energy back to 0, + 6, 1x'),
      ];
      for (final (dt, second, why) in cases) {
        test(
          why,
          () => expect(_run([(10, 1000), (6, 1000 + dt)]), [2, second]),
        );
      }
    });

    group('reversal and truncation', () {
      test('a reversal drops the leftover but keeps the energy', () {
        // +3 left over; -2 after 10 ms: energy 3 * 140 / 150 + 2 = 4, leftover
        // restarts at -2; -2 after 10 ms more: energy 5, leftover -4: a step.
        expect(_run([(3, 1000), (-2, 1010), (-2, 1020)]), [0, 0, -1]);
      });

      test('a fast reversal is accelerated at once', () {
        // 20 at 2x = 10 steps; -20 10 ms later: energy 20 * 140 / 150 + 20 =
        // 38, 4x: -80 / 4.
        expect(_run([(20, 1000), (-20, 1010)]), [10, -20]);
      });

      test('negative steps truncate toward zero', () {
        // -5: -1 step, -1 left over; -3 a second later: -4, one step.
        expect(_run([(-5, 1000), (-3, 2000)]), [-1, -1]);
      });

      test('a feed of 0 moves nothing', () {
        expect(_run([(0, 1000)]), [0]);
      });
    });
  });
}
