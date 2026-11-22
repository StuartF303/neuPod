// SPDX-License-Identifier: MPL-2.0

// Prints the highest multiplier the ui_accel_feed() port reaches for a finger
// turning at a steady speed, by touch sample rate. The firmware decays the
// energy in integers, so the answer depends on how often positions are fed.
//
//   dart run tool/wheel_vectors/accel_speeds.dart

import 'dart:math' as math;

import 'package:neupod/domain/wheel/wheel_input.dart';

void main() {
  const speeds = [150, 300, 450, 600, 800, 1000, 1500, 2000, 3000];
  // ignore: avoid_print
  print('deg/s:   ${speeds.map((s) => s.toString().padLeft(5)).join()}');
  for (final hz in [60, 120, 240]) {
    final row = StringBuffer('${hz.toString().padLeft(3)} Hz: ');
    for (final dps in speeds) {
      final w = WheelInput();
      w.down(1, 0, -0.75, 0);
      var best = 1;
      const n = 600;
      for (var i = 1; i <= n; i++) {
        final tMs = (i * 1000 / hz).round();
        final r = (dps * i / hz) * math.pi / 180;
        w.move(1, 0.75 * math.sin(r), -0.75 * math.cos(r), tMs);
        if (i > n ~/ 2) best = math.max(best, w.accel.multiplier);
      }
      row.write('${best}x'.padLeft(5));
    }
    // ignore: avoid_print
    print(row);
  }
}
