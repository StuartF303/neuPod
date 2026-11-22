// SPDX-License-Identifier: MPL-2.0

// The firmware's own tests for ui_accel_feed() (tests/test_ui_logic.c,
// accel_*), case for case, then the edge cases of the integer behaviour.

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/firmware_constants.dart';
import 'package:neupod/domain/wheel/wheel_accel.dart';

void main() {
  group('tests/test_ui_logic.c', () {
    test('accel_slow_is_one_step_per_four_counts', () {
      final a = WheelAccel();
      var t = 1000;
      var total = 0;
      for (var i = 0; i < 8; i++) {
        // one count every 400 ms: far below the acceleration window
        total += a.feed(1, t);
        t += 400;
      }
      expect(total, 2);
      expect(a.feed(4, t + 1000), 1); // a key press (+4) = one step
      expect(a.feed(-4, t + 2000), -1);
    });

    test('accel_fast_multiplies', () {
      final a = WheelAccel();
      var total = 0;
      var t = 5000;
      for (var i = 0; i < 20; i++) {
        // 4 counts every 10 ms
        total += a.feed(4, t);
        t += 10;
      }
      expect(total, greaterThan(20)); // plain speed: exactly 20 steps
      expect(total, lessThanOrEqualTo(20 * 8));
      // after a pause the speed is back to normal
      expect(a.feed(4, t + 1000), 1);
    });

    test('accel_reversal_drops_residue', () {
      final a = WheelAccel();
      expect(a.feed(3, 1000), 0); // residue +3
      expect(a.feed(-1, 3000), 0); // reversed: residue restarts at -1, not +2
      expect(a.feed(-3, 5000), -1);
    });
  });

  group('integer behaviour', () {
    test('the thresholds give 1x, 2x, 4x and 8x', () {
      for (final (counts, mult) in [
        (11, 1),
        (12, 2),
        (23, 2),
        (24, 4),
        (39, 4),
        (40, 8),
      ]) {
        final a = WheelAccel();
        a.feed(counts, 10000);
        expect(a.multiplier, mult, reason: '$counts counts');
        expect(a.residue + 0, (counts * mult) % uiCountsPerStep);
      }
    });

    test('energy decays linearly over 150 ms, in integers', () {
      final a = WheelAccel();
      a.feed(10, 1000);
      expect(a.energy, 10);
      a.feed(0, 1075); // 10 * 75 / 150 = 5
      expect(a.energy, 5);
      a.feed(0, 1076); // 5 * 149 / 150 = 4 (truncated)
      expect(a.energy, 4);
      a.feed(1, 1076 + uiAccelWindowMs); // a whole window: back to 0, +1
      expect(a.energy, 1);
      a.feed(1, 1076 + uiAccelWindowMs + 149);
      expect(a.energy, 1); // 1 * 1 / 150 = 0, +1
    });

    test('steps truncate toward zero and keep the residue signed', () {
      final a = WheelAccel();
      expect(a.feed(-7, 1000), -1);
      expect(a.residue, -3);
      expect(a.feed(-1, 2000), -1);
      expect(a.residue, 0);
      expect(a.feed(7, 3000), 1);
      expect(a.residue, 3);
    });

    test('zero counts decay the energy but keep the residue', () {
      final a = WheelAccel();
      a.feed(3, 1000);
      expect(a.feed(0, 1001), 0);
      expect(a.residue, 3);
      expect(a.feed(1, 5000), 1);
    });

    test('the clock wrapping past 2^32 reads as a short gap', () {
      final a = WheelAccel();
      a.feed(20, 0xFFFFFFF0);
      expect(a.multiplier, 2);
      a.feed(20, 0x00000004); // 20 ms later, across the wrap
      expect(a.energy, 20 * 130 ~/ 150 + 20);
      expect(a.multiplier, 4);
    });

    test('a clock that goes backwards reads as a long pause', () {
      final a = WheelAccel();
      a.feed(30, 5000);
      a.feed(4, 4990);
      expect(a.energy, 4);
      expect(a.multiplier, 1);
    });

    test('the first feed after init counts from time 0, as in C', () {
      final a = WheelAccel();
      a.feed(12, 100); // dt = 100 < 150: energy 0 * ... + 12
      expect(a.multiplier, 2);
      a.reset();
      expect([a.residue, a.energy, a.lastMs, a.multiplier], [0, 0, 0, 1]);
    });
  });
}
