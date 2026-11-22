// SPDX-License-Identifier: MPL-2.0

// The letter ring model against the firmware's own tests: each group below is
// a case of tests/test_wheel_input.c, assertion for assertion, under its C
// name. The firmware works in UTF-8 bytes, and so do lengths, limits and the
// cursor here. After them, cases for the port itself: the search callback
// the firmware's view drives, the page contents and the constants.

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/text_entry/wheel_text_entry.dart';

/// Turn the ring to the key with [label], in whichever chip has it (pick_key).
void pickKey(WheelTextEntry t, String label) {
  final count = TextChip.values.length;
  for (var c = 0; c < count; c++) {
    final chips = t.chips();
    final n = chips.length;
    for (var k = 0; k < n; k++) {
      t.chip = chips[(k + c) % n];
      for (var i = 0; i < t.stripLength(); i++) {
        if (t.stripKey(i).$2 == label) {
          t.sel = i;
          return;
        }
      }
    }
  }
  fail('no key "$label"');
}

/// type() in the C test.
void type(WheelTextEntry t, String s) {
  for (final ch in s.split('')) {
    pickKey(t, ch);
    t.event(WheelEvent.select);
  }
}

WheelTextEntry init(
  String initial,
  int maxChars, {
  TextCharset cs = TextCharset.any,
  bool masked = false,
  bool search = false,
}) => WheelTextEntry(
  initial: initial,
  maxChars: maxChars,
  charset: cs,
  masked: masked,
  search: search,
);

void main() {
  group('tests/test_wheel_input.c', () {
    test('test_text_typing_and_chips', () {
      final t = init('', 20);
      expect(t.chip, TextChip.lower);
      type(t, 'ab');
      expect(t.text, 'ab');
      expect(t.typed, isTrue);
      // Right / Left walk the chips: abc, ABC, 123, #!?, space, del, round
      expect(t.chips(), hasLength(6));
      t.event(WheelEvent.right);
      expect(t.chip, TextChip.upper);
      expect(t.sel, 0);
      expect(t.typed, isFalse);
      type(t, 'Z');
      type(t, '7');
      type(t, '!');
      expect(t.text, 'abZ7!');
      t.event(WheelEvent.left);
      t.event(WheelEvent.right);
      pickKey(t, 'space');
      expect(t.chip, TextChip.space);
      t.event(WheelEvent.select);
      expect(t.text, 'abZ7! ');
      pickKey(t, 'del'); // the delete chip: Select removes the last one
      t.event(WheelEvent.select);
      expect(t.text, 'abZ7!');
      expect(t.typed, isFalse);
      t.event(WheelEvent.right);
      expect(t.chip, TextChip.lower, reason: 'wraps round');
      t.event(WheelEvent.left);
      expect(t.chip, TextChip.delete);
      // the ring wraps both ways
      t.event(WheelEvent.right);
      t.sel = 0;
      t.event(WheelEvent.scroll, -1);
      expect(t.sel, t.stripLength() - 1);
      t.event(WheelEvent.scroll, 1);
      expect(t.sel, 0);
      // a long press of Menu clears
      t.clear();
      expect(t.lengthBytes, 0);
      expect(t.changed, isTrue);
    });

    test('test_text_backspace_cancel_confirm', () {
      var t = init('abc', 10);
      expect(t.cursor, 3);
      t.event(WheelEvent.menu); // deletes 'c'
      expect(t.text, 'ab');
      expect(t.cursor, 2);
      t = init('', 10);
      expect(
        t.event(WheelEvent.menu),
        WheelResult.cancelled,
        reason: 'empty: Menu cancels',
      );
      t.insert(0x71); // q
      expect(
        t.event(WheelEvent.menu),
        WheelResult.running,
        reason: 'non-empty: backspace',
      );
      expect(t.event(WheelEvent.play), WheelResult.ok, reason: 'Play is done');
      expect(
        t.event(WheelEvent.select),
        WheelResult.running,
        reason: 'Select only types',
      );
    });

    test('test_text_limits_charset_masking_utf8', () {
      var t = init('', 3, masked: true);
      type(t, 'abcd'); // 4th rejected
      expect(t.chars, 3);
      expect(t.masked, isTrue);

      t = init('', 8, cs: TextCharset.digits);
      expect(t.chip, TextChip.digits);
      expect(t.insert(0x61), isFalse); // a
      expect(t.insert(0x35), isTrue); // 5
      // digits only: the 123 chip and delete, no letters, no space
      expect(t.chips(), [TextChip.digits, TextChip.delete]);
      t = init('', 8, cs: TextCharset.hex);
      expect(t.insert(0x66), isTrue); // f
      expect(t.insert(0x67), isFalse); // g

      // UTF-8: a two-byte character counts as one, backspace removes it
      // whole, the cursor never lands inside it.
      t = init('', 4);
      expect(t.insert(0xE9), isTrue); // e acute
      expect(t.insert(0x78), isTrue); // x
      expect(t.lengthBytes, 3);
      expect(t.chars, 2);
      t.event(WheelEvent.menu);
      t.event(WheelEvent.menu); // the accented one goes whole
      expect(t.lengthBytes, 0);
      t.insert(0xE9);
      expect(t.lengthBytes, 2);
      // initial text is not cut mid-character
      t = WheelTextEntry.fromUtf8([0x63, 0x61, 0x66, 0xC3, 0xA9], maxChars: 10);
      expect(t.lengthBytes, 5);
    });

    test('test_text_byte_limit_and_validation', () {
      var t = init('', 40);
      t.setByteLimit(4); // an SSID-style limit in BYTES
      expect(t.insert(0xE9), isTrue); // 2 bytes
      expect(t.insert(0xE9), isTrue); // 4 bytes
      expect(t.insert(0x78), isFalse, reason: 'over the byte limit');
      expect(t.insert(0x110000), isFalse, reason: 'beyond Unicode');
      expect(t.insert(0xD800), isFalse, reason: 'a surrogate');
      // invalid initial text is cut at the first bad sequence; control bytes
      // are refused
      t = WheelTextEntry.fromUtf8([0x61, 0x62, 0xFF, 0x7A, 0x7A], maxChars: 10);
      expect(t.text, 'ab');
      t = WheelTextEntry.fromUtf8([0x6F, 0x6B, 0x01, 0x6E, 0x6F], maxChars: 10);
      expect(t.text, 'ok');
      t = WheelTextEntry.fromUtf8([0x63, 0x61, 0x66, 0xC3], maxChars: 10);
      expect(t.text, 'caf', reason: 'truncated sequence');
      t = init('abcdef', 3);
      expect(t.text, 'abc', reason: 'longer than max_chars');
      // The C case ends with a multi picker (wi_picker_t), which is not part
      // of the text entry and is not ported.
    });

    test('test_text_search_changed', () {
      final t = init('', 20, search: true);
      expect(t.changed, isFalse);
      type(t, 'a');
      expect(t.changed, isTrue);
      t.changed = false;
      t.event(WheelEvent.left);
      expect(t.changed, isFalse, reason: 'changing the chip is not an edit');
      t.event(WheelEvent.right);
      t.event(WheelEvent.menu);
      expect(t.changed, isTrue);
    });
  });

  group('search mode change callback (the view, app/ui/input/ui_input.c)', () {
    test('called once after every edit, with the text, and only then', () {
      final calls = <String>[];
      final t = WheelTextEntry(maxChars: 20, search: true, onChange: calls.add);
      type(t, 'ab');
      t.event(WheelEvent.left);
      t.event(WheelEvent.right);
      t.event(WheelEvent.scroll, 3);
      t.event(WheelEvent.play);
      t.event(WheelEvent.menu);
      pickKey(t, 'space');
      t.event(WheelEvent.select);
      pickKey(t, 'del');
      t.event(WheelEvent.select);
      t.insert(0x63);
      t.clear();
      t.clear(); // already empty: not an edit
      t.event(WheelEvent.menu); // empty: cancels, not an edit
      expect(calls, ['a', 'ab', 'a', 'a ', 'a', 'ac', '']);
    });

    test('a refused key is not an edit', () {
      final calls = <String>[];
      final t = WheelTextEntry(maxChars: 1, search: true, onChange: calls.add);
      type(t, 'xy');
      expect(t.text, 'x');
      expect(t.typed, isFalse);
      expect(calls, ['x']);
    });

    test('not called outside search mode', () {
      final calls = <String>[];
      final t = WheelTextEntry(maxChars: 20, onChange: calls.add);
      type(t, 'ab');
      t.clear();
      expect(calls, isEmpty);
      expect(t.changed, isTrue, reason: 'changed is still raised');
    });

    test('the callback leaves changed for the caller to clear', () {
      final t = WheelTextEntry(maxChars: 20, search: true, onChange: (_) {});
      t.insert(0x61);
      expect(t.changed, isTrue);
    });
  });

  group('the port', () {
    test('pages and their rings, as k_pages', () {
      // Copied from k_pages in app/ui/input/wheel_input.c.
      const pages = {
        TextChip.lower: 'abcdefghijklmnopqrstuvwxyz',
        TextChip.upper: 'ABCDEFGHIJKLMNOPQRSTUVWXYZ',
        TextChip.digits: '0123456789',
        TextChip.symbols: r'''.,-_@!?#$%&*+=/:;'"()[]{}<>~^\|`''',
      };
      final t = init('', 20);
      final rings = <TextChip, String>{};
      for (final c in t.chips()) {
        t.chip = c;
        rings[c] = [for (var i = 0; i < t.stripLength(); i++) t.stripKey(i).$2]
            .join();
      }
      for (final MapEntry(key: chip, value: page) in pages.entries) {
        expect(rings[chip], page, reason: '$chip');
        expect(kTextPages[chip.index], page, reason: '$chip');
      }
      expect(pages[TextChip.symbols], hasLength(32));
      expect(t.stripLength(), 1);
      t.chip = TextChip.space;
      expect(t.stripKey(0), (TextKeyKind.space, 'space'));
      t.chip = TextChip.delete;
      expect(t.stripKey(0), (TextKeyKind.delete, 'del'));
    });

    test('hex offers the a-f and A-F pages too, starting on 123', () {
      final t = init('', 8, cs: TextCharset.hex);
      expect(t.chip, TextChip.digits);
      expect(t.chips(), [
        TextChip.lower,
        TextChip.upper,
        TextChip.digits,
        TextChip.delete,
      ]);
      t.chip = TextChip.lower;
      expect(t.stripLength(), 6);
      expect(t.stripKey(5), (TextKeyKind.char, 'f'));
    });

    test('accelerated steps turn the ring that far, wrapping', () {
      final t = init('', 20);
      t.event(WheelEvent.scroll, 27);
      expect(t.sel, 1);
      t.event(WheelEvent.scroll, -55);
      expect(t.sel, 24);
    });

    test('the maximum is 64 bytes whatever max chars says', () {
      final t = init('', 100);
      expect(t.maxChars, kTextMaxBytes);
      for (var i = 0; i < 70; i++) {
        t.insert(0x61);
      }
      expect(t.lengthBytes, 64);
      expect(init('', 0).maxChars, kTextMaxBytes, reason: '0 is the maximum');
    });

    test('a byte limit truncates whole characters and is not an edit', () {
      final t = init('aé', 10);
      t.setByteLimit(2);
      expect(t.text, 'a');
      expect(t.cursor, 1);
      expect(t.changed, isFalse);
      t.setByteLimit(0);
      expect(t.maxBytes, kTextMaxBytes);
    });

    test('Select on the delete chip with nothing to delete does nothing', () {
      final t = init('', 10);
      t.chip = TextChip.delete;
      expect(t.event(WheelEvent.select), WheelResult.running);
      expect(t.changed, isFalse);
    });

    test(
      'non-ASCII is refused outside free text; control characters always',
      () {
        expect(init('', 8, cs: TextCharset.digits).insert(0xE9), isFalse);
        expect(init('', 8).insert(0x7F), isFalse);
        expect(init('', 8).insert(0x0A), isFalse);
        expect(init('', 8).insert(0x1F600), isTrue, reason: 'four bytes');
      },
    );

    test('view constants, from app/ui/input/ui_input.c', () {
      expect(kRingGlyphs, 9);
      expect(kMaskedRevealTime, const Duration(milliseconds: 800));
      expect(kClearHoldTime, const Duration(milliseconds: 600));
    });
  });
}
