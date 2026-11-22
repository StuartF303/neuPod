// SPDX-License-Identifier: MPL-2.0

// Replays vectors made by the firmware's own ui_accel_feed() and
// wi_letterjump_*() (compiled for the host by
// tool/wheel_vectors/gen_wheel_vectors.sh) through the Dart ports, and checks
// every step, residue, energy, active flag and letter agrees.

import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/letter_jump.dart';
import 'package:neupod/domain/wheel/wheel_accel.dart';

void main() {
  final vectors = jsonDecode(
    File('test/domain/wheel/fixtures/firmware_vectors.json').readAsStringSync(),
  ) as Map<String, dynamic>;

  test('ui_accel_feed: the Dart port agrees with the C, call for call', () {
    final sequences = vectors['accel'] as List<dynamic>;
    expect(sequences, hasLength(60));
    var calls = 0;
    final multipliers = <int>{};
    for (final (s, seq) in sequences.indexed) {
      final a = WheelAccel();
      for (final (i, raw) in (seq as List<dynamic>).indexed) {
        final v = (raw as List<dynamic>).cast<int>();
        final steps = a.feed(v[0], v[1]);
        final where = 'sequence $s, call $i: feed(${v[0]}, ${v[1]})';
        expect(steps, v[2], reason: '$where steps');
        expect(a.residue, v[3], reason: '$where residue');
        expect(a.energy, v[4], reason: '$where energy');
        multipliers.add(a.multiplier);
        calls++;
      }
    }
    expect(calls, greaterThan(3000));
    expect(multipliers, {1, 2, 4, 8}, reason: 'vectors exercise every rate');
  });

  test('wi_letterjump: the Dart port agrees with the C, call for call', () {
    final sequences = vectors['letterjump'] as List<dynamic>;
    expect(sequences, hasLength(30));
    var activations = 0;
    for (final (s, seq) in sequences.indexed) {
      final j = LetterJump();
      for (final (i, raw) in (seq as List<dynamic>).indexed) {
        final v = raw as List<dynamic>;
        final where = 'sequence $s, call $i: $v';
        if (v[0] == 'tick') {
          j.tick(v[1] as int);
          expect(j.active, v[2] == 1, reason: '$where active');
          expect(j.energy, v[3], reason: '$where energy');
        } else {
          final r = j.scroll(v[2] as int, v[1] as int, v[3] as String);
          expect(r, v[4] == 1, reason: '$where result');
          expect(j.energy, v[5], reason: '$where energy');
          expect(j.letter, v[6], reason: '$where letter');
          if (r) activations++;
        }
      }
    }
    expect(activations, greaterThan(50));
  });
}
