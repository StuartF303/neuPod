// SPDX-License-Identifier: MPL-2.0

// The firmware's own test for the letter-jump overlay
// (tests/test_wheel_input.c, test_letter_jump), case for case, plus the
// trigger's edges.

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/firmware_constants.dart';
import 'package:neupod/domain/wheel/letter_jump.dart';

void main() {
  test('test_letter_jump (tests/test_wheel_input.c)', () {
    final j = LetterJump();
    expect(j.scroll(1, 1000, 'a'), isFalse); // slow: no overlay
    expect(j.scroll(1, 1100, 'a'), isFalse);
    expect(j.scroll(5, 1200, 'm'), isTrue); // fast
    expect(j.letter, 'M');
    j.tick(1200 + wiLjHideMs);
    expect(j.active, isTrue); // still within the rest delay
    j.tick(1200 + wiLjHideMs + 1);
    expect(j.active, isFalse);
    expect(j.scroll(1, 5000, '7'), isFalse); // energy decayed
    j.scroll(8, 5050, '7');
    expect(j.letter, '#');
    const s = '#AABBBDEEFZ'; // sorted initials; '#' first
    String at(int i) => s[i];
    expect(LetterJump.firstIndex(11, at, '#'), 0);
    expect(LetterJump.firstIndex(11, at, 'A'), 1);
    expect(LetterJump.firstIndex(11, at, 'B'), 3);
    expect(LetterJump.firstIndex(11, at, 'C'), 6); // no C: the next present
    expect(LetterJump.firstIndex(11, at, 'Z'), 10);
    expect(LetterJump.firstIndex(11, at, 'z'), 10); // case-insensitive
    expect(LetterJump.firstIndex(0, at, 'A'), 0);
  });

  test('the trigger is 6 accelerated steps with no gap over 400 ms', () {
    final j = LetterJump();
    for (var i = 0; i < 5; i++) {
      expect(j.scroll(1, 1000 + i * wiLjWindowMs, 'b'), isFalse);
    }
    expect(j.energy, 5);
    expect(j.scroll(1, 1000 + 5 * wiLjWindowMs, 'b'), isTrue); // gap of 400
    final k = LetterJump();
    k.scroll(5, 1000, 'b');
    expect(k.scroll(1, 1000 + wiLjWindowMs + 1, 'b'), isFalse); // gap of 401
    expect(k.energy, 1);
  });

  test('one accelerated move of 8 steps shows it at once; direction does not '
      'matter', () {
    final j = LetterJump();
    expect(j.scroll(-8, 100, 'q'), isTrue);
    expect(j.letter, 'Q');
  });

  test('it stays while the wheel keeps moving, however slowly', () {
    final j = LetterJump();
    j.scroll(6, 0, 'a');
    for (var t = 200; t < 5000; t += 200) {
      j.tick(t);
      expect(j.active, isTrue);
      j.scroll(1, t, 'a');
    }
  });

  test('normaliseInitial', () {
    expect(LetterJump.normaliseInitial('beatles'), 'B');
    expect(LetterJump.normaliseInitial('Z'), 'Z');
    expect(LetterJump.normaliseInitial('9'), '#');
    expect(LetterJump.normaliseInitial('É'), '#'); // callers pass the sort key
    expect(LetterJump.normaliseInitial(''), '#');
  });

  test(
    'a list without the wanted letter or later ones gives the last item',
    () {
      String at(int i) => 'ABC'[i];
      expect(LetterJump.firstIndex(3, at, 'Z'), 2);
    },
  );
}
