// SPDX-License-Identifier: MPL-2.0

// Spec suite: Search typed with the wheel (owner decision 12, 22 November
// 2026), using a port of the firmware's text-entry model in search mode
// (app/include/wheel_input.h, app/ui/input/wheel_input.c, docs/ui-input.md),
// and the Search screen of the device view design ("Search, by the wheel",
// design PR #107).
//
// Part 1 mirrors the text-entry cases of tests/test_wheel_input.c case for
// case (test_text_typing_and_chips, test_text_backspace_cancel_confirm,
// test_text_limits_charset_masking_utf8, test_text_byte_limit_and_validation,
// test_text_search_changed). The firmware works in UTF-8 bytes: lengths,
// limits and the cursor here are UTF-8 byte counts too.
//
// Part 2 is the Search screen: Select types, Previous / Next walk the mode
// chips, Menu deletes (held: clears; on an empty field: leaves Search),
// Play/Pause goes to the results; in the results Menu returns to typing with
// the text kept and Play/Pause is play or pause again. The list is searched
// after every edit, and the best three matches show live while typing.
//
// WIRING: set the wired* values below to adapters over the app's code; each
// group's skip lifts by itself.
@Tags(['spec'])
library;

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/text_entry/wheel_text_entry.dart' as port;
import 'package:neupod/ui/screens/search/search_session.dart' as screen;

enum TextCharset { any, digits, hex }

/// The mode chips under the letter ring (wi_chip_t), in firmware order.
enum TextChip { lower, upper, digits, symbols, space, delete }

enum TextKeyKind { char, space, delete }

enum TextResult { running, ok, cancelled }

/// The port of wi_text_t and its functions.
abstract interface class TextEntry {
  String get text;

  /// UTF-8 bytes of [text] (t->len).
  int get lengthBytes;

  /// UTF-8 byte offset of the cursor, always on a character boundary.
  int get cursor;

  /// Code points (wi_text_chars).
  int get chars;
  bool get masked;

  /// The last event inserted a character.
  bool get typed;

  /// Set by every edit; the caller clears it after reading.
  bool get changed;
  set changed(bool value);

  TextChip get chip;
  set chip(TextChip value);

  /// Index into the current chip's ring.
  int get sel;
  set sel(int value);

  /// The chips this entry offers, in order (wi_text_chips).
  List<TextChip> chips();

  /// Keys on the current chip's ring (wi_text_strip_len).
  int stripLength();

  /// The key at [i]: its kind and label (the character, 'space' or 'del').
  (TextKeyKind, String) stripKey(int i);

  TextResult scroll(int steps);
  TextResult select();
  TextResult menu();
  TextResult play();
  TextResult left();
  TextResult right();

  /// Type a code point directly; false if refused (charset, length, not a
  /// Unicode scalar value, a control character).
  bool insert(int codePoint);

  /// Delete the whole text (a long press of Menu); an edit.
  void clear();

  /// Limit the text to [maxBytes] UTF-8 bytes (0: the 64-byte maximum).
  void setByteLimit(int maxBytes);
}

/// wi_text_init. [onSearchChanged] is called after every edit when [search]
/// is true, with the new text.
typedef TextEntryFactory = TextEntry Function({
  String initial,
  required int maxChars,
  TextCharset charset,
  bool masked,
  bool search,
  void Function(String text)? onSearchChanged,
});

/// wi_text_init from raw UTF-8 bytes, for the firmware's invalid-initial-text
/// cases (a Dart String cannot hold them). Leave null if the app's port only
/// ever receives Strings; those three cases then stay skipped.
typedef TextEntryFromBytes = TextEntry Function(
  List<int> initialUtf8, {
  required int maxChars,
});

/// The Search screen. [search] is the library search (called after every
/// edit with the text so far); [onPlayPause] is the player's play or pause.
abstract interface class SearchScreen {
  SearchPhase get phase;
  String get text;

  /// The best three matches shown while typing.
  List<String> get liveResults;

  /// The full results list (in the results phase).
  List<String> get results;

  void wheel(int steps);
  void centre();
  void previous();
  void next();
  void menu();
  void menuHeld();
  void playPause();
}

enum SearchPhase { typing, results, left }

typedef SearchScreenFactory = SearchScreen Function({
  required List<String> Function(String query) search,
  required void Function() onPlayPause,
});

// WIRE: replace null with adapters over the app's code.
// The nullable types stay so each group's skip can test for null.
// ignore: unnecessary_nullable_for_final_variable_declarations
final TextEntryFactory? wiredTextEntry = _portedTextEntry;
// ignore: unnecessary_nullable_for_final_variable_declarations
final TextEntryFromBytes? wiredTextEntryFromBytes = _portedTextEntryFromBytes;
// The Search screen is the session behind ui/screens/search (the widget draws
// and feeds it; test/ui/screens/search tests the widget).
// ignore: unnecessary_nullable_for_final_variable_declarations
final SearchScreenFactory? wiredSearchScreen = _wiredSearchScreen;

SearchScreen _wiredSearchScreen({
  required List<String> Function(String query) search,
  required void Function() onPlayPause,
}) => _SessionSearchScreen(search, onPlayPause);

/// The spec's Search screen over the app's [screen.SearchSession]. The
/// spec's library search returns names; they are songs here, in the order
/// given (the best first).
final class _SessionSearchScreen implements SearchScreen {
  _SessionSearchScreen(
    List<String> Function(String query) search,
    this._onPlayPause,
  ) : _session = screen.SearchSession(
        search: (q) => screen.SearchResults(
          songs: [
            for (final (i, name) in search(q).indexed)
              screen.SearchHit(
                kind: screen.SearchKind.song,
                id: '$i',
                title: name,
              ),
          ],
        ),
      );

  final screen.SearchSession _session;
  final void Function() _onPlayPause;

  @override
  SearchPhase get phase => switch (_session.phase) {
    screen.SearchPhase.typing => SearchPhase.typing,
    screen.SearchPhase.results => SearchPhase.results,
    screen.SearchPhase.left => SearchPhase.left,
  };

  @override
  String get text => _session.text;

  @override
  List<String> get liveResults => [
    for (final h in _session.liveResults) h.title,
  ];

  @override
  List<String> get results => [for (final h in _session.flatResults) h.title];

  @override
  void wheel(int steps) => _session.wheel(steps);

  @override
  void centre() => _session.centre();

  @override
  void previous() => _session.previous();

  @override
  void next() => _session.next();

  @override
  void menu() => _session.menu();

  @override
  void menuHeld() => _session.menuHeld();

  @override
  void playPause() {
    if (!_session.playPause()) _onPlayPause();
  }
}

TextEntry _portedTextEntry({
  String initial = '',
  required int maxChars,
  TextCharset charset = TextCharset.any,
  bool masked = false,
  bool search = false,
  void Function(String text)? onSearchChanged,
}) => _PortedTextEntry(
  port.WheelTextEntry(
    initial: initial,
    maxChars: maxChars,
    charset: port.TextCharset.values[charset.index],
    masked: masked,
    search: search,
    onChange: onSearchChanged,
  ),
);

TextEntry _portedTextEntryFromBytes(
  List<int> initialUtf8, {
  required int maxChars,
}) => _PortedTextEntry(
  port.WheelTextEntry.fromUtf8(initialUtf8, maxChars: maxChars),
);

/// The spec's TextEntry over the port (mobile/lib/domain/text_entry/); the
/// enums are the firmware's, in the same order on both sides.
final class _PortedTextEntry implements TextEntry {
  _PortedTextEntry(this._t);
  final port.WheelTextEntry _t;

  TextResult _r(port.WheelResult r) => TextResult.values[r.index];

  @override
  String get text => _t.text;
  @override
  int get lengthBytes => _t.lengthBytes;
  @override
  int get cursor => _t.cursor;
  @override
  int get chars => _t.chars;
  @override
  bool get masked => _t.masked;
  @override
  bool get typed => _t.typed;
  @override
  bool get changed => _t.changed;
  @override
  set changed(bool value) => _t.changed = value;
  @override
  TextChip get chip => TextChip.values[_t.chip.index];
  @override
  set chip(TextChip value) => _t.chip = port.TextChip.values[value.index];
  @override
  int get sel => _t.sel;
  @override
  set sel(int value) => _t.sel = value;
  @override
  List<TextChip> chips() => [
    for (final c in _t.chips()) TextChip.values[c.index],
  ];
  @override
  int stripLength() => _t.stripLength();
  @override
  (TextKeyKind, String) stripKey(int i) {
    final (kind, label) = _t.stripKey(i);
    return (TextKeyKind.values[kind.index], label);
  }

  @override
  TextResult scroll(int steps) => _r(_t.event(port.WheelEvent.scroll, steps));
  @override
  TextResult select() => _r(_t.event(port.WheelEvent.select));
  @override
  TextResult menu() => _r(_t.event(port.WheelEvent.menu));
  @override
  TextResult play() => _r(_t.event(port.WheelEvent.play));
  @override
  TextResult left() => _r(_t.event(port.WheelEvent.left));
  @override
  TextResult right() => _r(_t.event(port.WheelEvent.right));
  @override
  bool insert(int codePoint) => _t.insert(codePoint);
  @override
  void clear() => _t.clear();
  @override
  void setByteLimit(int maxBytes) => _t.setByteLimit(maxBytes);
}

TextEntry _entry(
  String initial,
  int maxChars, {
  TextCharset cs = TextCharset.any,
  bool masked = false,
  bool search = false,
  void Function(String)? onSearchChanged,
}) => wiredTextEntry!(
  initial: initial,
  maxChars: maxChars,
  charset: cs,
  masked: masked,
  search: search,
  onSearchChanged: onSearchChanged,
);

/// Turns the ring to the key labelled [label], in whichever chip has it
/// (pick_key in the firmware test).
void _pickKey(TextEntry t, String label) {
  final chips = t.chips();
  for (final c in chips) {
    t.chip = c;
    for (var i = 0; i < t.stripLength(); i++) {
      if (t.stripKey(i).$2 == label) {
        t.sel = i;
        return;
      }
    }
  }
  fail('no key "$label"');
}

void _type(TextEntry t, String s) {
  for (final ch in s.split('')) {
    _pickKey(t, ch);
    t.select();
  }
}

void main() {
  group(
    'text entry (tests/test_wheel_input.c)',
    skip: wiredTextEntry == null
        ? 'awaiting implementation of the wheel text entry (wi_text port)'
        : false,
    () {
      test('test_text_typing_and_chips', () {
        final t = _entry('', 20);
        expect(t.chip, TextChip.lower);
        _type(t, 'ab');
        expect(t.text, 'ab');
        expect(t.typed, isTrue);
        // Right / Left walk the chips: abc, ABC, 123, #!?, space, del, round.
        // Six chips including #!? also in Search: owner decision 12
        // (docs/app/README.md).
        expect(t.chips(), hasLength(6));
        t.right();
        expect(t.chip, TextChip.upper);
        expect(t.sel, 0);
        expect(t.typed, isFalse);
        _type(t, 'Z');
        _type(t, '7');
        _type(t, '!');
        expect(t.text, 'abZ7!');
        t.left();
        t.right();
        _pickKey(t, 'space');
        expect(t.chip, TextChip.space);
        t.select();
        expect(t.text, 'abZ7! ');
        _pickKey(t, 'del'); // the delete chip: Select removes the last one
        t.select();
        expect(t.text, 'abZ7!');
        expect(t.typed, isFalse);
        t.right();
        expect(t.chip, TextChip.lower, reason: 'wraps round');
        t.left();
        expect(t.chip, TextChip.delete);
        // the ring wraps both ways
        t.right();
        t.sel = 0;
        t.scroll(-1);
        expect(t.sel, t.stripLength() - 1);
        t.scroll(1);
        expect(t.sel, 0);
        // a long press of Menu clears
        t.clear();
        expect(t.lengthBytes, 0);
        expect(t.changed, isTrue);
      });

      test('test_text_backspace_cancel_confirm', () {
        var t = _entry('abc', 10);
        expect(t.cursor, 3);
        t.menu(); // deletes 'c'
        expect(t.text, 'ab');
        expect(t.cursor, 2);
        t = _entry('', 10);
        expect(t.menu(), TextResult.cancelled, reason: 'empty: Menu cancels');
        t.insert(0x71); // q
        expect(t.menu(), TextResult.running, reason: 'non-empty: backspace');
        expect(t.play(), TextResult.ok, reason: 'Play is done');
        expect(t.select(), TextResult.running, reason: 'Select only types');
      });

      test('test_text_limits_charset_masking_utf8', () {
        var t = _entry('', 3, masked: true);
        for (final ch in 'abcd'.split('')) {
          _pickKey(t, ch);
          t.select();
        }
        expect(t.chars, 3, reason: 'the 4th is refused');
        expect(t.masked, isTrue);

        t = _entry('', 8, cs: TextCharset.digits);
        expect(t.chip, TextChip.digits);
        expect(t.insert(0x61), isFalse); // a
        expect(t.insert(0x35), isTrue); // 5
        expect(t.chips(), [
          TextChip.digits,
          TextChip.delete,
        ], reason: 'digits only: 123 and delete, no letters, no space');
        t = _entry('', 8, cs: TextCharset.hex);
        expect(t.insert(0x66), isTrue); // f
        expect(t.insert(0x67), isFalse); // g

        // UTF-8: a two-byte character counts as one, goes whole on
        // backspace, and the cursor never lands inside it.
        t = _entry('', 4);
        expect(t.insert(0xE9), isTrue); // e acute
        expect(t.insert(0x78), isTrue); // x
        expect(t.lengthBytes, 3);
        expect(t.chars, 2);
        t.menu();
        t.menu(); // the accented one goes whole
        expect(t.lengthBytes, 0);
        t.insert(0xE9);
        expect(t.lengthBytes, 2);
        // initial text is not cut mid-character
        t = _entry('café', 10);
        expect(t.lengthBytes, 5);
      });

      test('test_text_byte_limit_and_validation', () {
        var t = _entry('', 40);
        t.setByteLimit(4); // an SSID-style limit in bytes
        expect(t.insert(0xE9), isTrue); // 2 bytes
        expect(t.insert(0xE9), isTrue); // 4 bytes
        expect(t.insert(0x78), isFalse, reason: 'over the byte limit');
        expect(t.insert(0x110000), isFalse, reason: 'beyond Unicode');
        expect(t.insert(0xD800), isFalse, reason: 'a surrogate');
        // control characters end the initial text
        t = _entry('ok\u0001no', 10);
        expect(t.text, 'ok');
        // longer than max chars
        t = _entry('abcdef', 3);
        expect(t.text, 'abc');
        // (The multi-picker case of this firmware test is not text entry.)
      });

      test('test_text_search_changed', () {
        final t = _entry('', 20, search: true);
        expect(t.changed, isFalse);
        _type(t, 'a');
        expect(t.changed, isTrue);
        t.changed = false;
        t.left();
        expect(t.changed, isFalse, reason: 'changing the chip is not an edit');
        t.right();
        t.menu();
        expect(t.changed, isTrue);
      });

      test('search mode calls back after every edit, with the text', () {
        final calls = <String>[];
        final t = _entry('', 20, search: true, onSearchChanged: calls.add);
        _type(t, 'ab');
        t.left();
        t.right();
        t.scroll(3);
        t.menu();
        _type(t, 'c');
        t.clear();
        expect(calls, ['a', 'ab', 'a', 'ac', '']);
      });

      test('the maximum is 64 bytes whatever max chars says', () {
        final t = _entry('', 100);
        for (var i = 0; i < 70; i++) {
          t.insert(0x61);
        }
        expect(t.lengthBytes, 64);
      });

      test('ring contents: 26 letters, 10 digits, one key per action chip', () {
        final t = _entry('', 20);
        t.chip = TextChip.lower;
        expect(t.stripLength(), 26);
        expect(t.stripKey(0), (TextKeyKind.char, 'a'));
        t.chip = TextChip.upper;
        expect(t.stripKey(25), (TextKeyKind.char, 'Z'));
        t.chip = TextChip.digits;
        expect(t.stripLength(), 10);
        t.chip = TextChip.space;
        expect(t.stripLength(), 1);
        expect(t.stripKey(0), (TextKeyKind.space, 'space'));
        t.chip = TextChip.delete;
        expect(t.stripKey(0), (TextKeyKind.delete, 'del'));
      });
    },
  );

  group(
    'text entry from invalid UTF-8 (tests/test_wheel_input.c)',
    skip: wiredTextEntryFromBytes == null
        ? 'awaiting implementation of the wheel text entry from bytes '
              '(not needed if the port only takes Strings)'
        : false,
    () {
      test('invalid initial text is cut at the first bad sequence', () {
        final t = wiredTextEntryFromBytes!([
          0x61,
          0x62,
          0xFF,
          0x7A,
          0x7A,
        ], maxChars: 10);
        expect(t.text, 'ab');
      });
      test('a control byte ends the initial text', () {
        final t = wiredTextEntryFromBytes!([
          0x6F,
          0x6B,
          0x01,
          0x6E,
          0x6F,
        ], maxChars: 10);
        expect(t.text, 'ok');
      });
      test('a truncated sequence is dropped', () {
        final t = wiredTextEntryFromBytes!([
          0x63,
          0x61,
          0x66,
          0xC3,
        ], maxChars: 10);
        expect(t.text, 'caf');
      });
    },
  );

  group(
    'Search screen (device view, Search by the wheel)',
    skip: wiredSearchScreen == null
        ? 'awaiting implementation of the Search screen typed with the wheel'
        : false,
    () {
      late List<String> queries;
      late int playPauses;
      const catalogue = [
        'Abba',
        'Abbey Road',
        'Ablaze',
        'Absolution',
        'Bark',
        'aardvark',
      ];
      List<String> search(String q) {
        queries.add(q);
        if (q.isEmpty) return const [];
        return [
          for (final c in catalogue)
            if (c.toLowerCase().contains(q.toLowerCase())) c,
        ];
      }

      SearchScreen screen() {
        queries = [];
        playPauses = 0;
        return wiredSearchScreen!(
          search: search,
          onPlayPause: () => playPauses++,
        );
      }

      test('opens typing, with an empty field on the lower-case ring', () {
        final s = screen();
        expect(s.phase, SearchPhase.typing);
        expect(s.text, '');
        s.centre();
        expect(s.text, 'a', reason: 'the ring starts at a');
      });

      test('the wheel turns the ring and the centre types', () {
        final s = screen();
        s.wheel(1);
        s.centre();
        expect(s.text, 'b');
        s.wheel(-1);
        s.centre();
        expect(s.text, 'ba');
      });

      test('the list is searched after every edit', () {
        final s = screen();
        s.centre(); // a
        s.wheel(1);
        s.centre(); // ab
        s.menu(); // a
        // Whether the empty field is searched on opening is not specified.
        expect(queries.where((q) => q.isNotEmpty).toList(), ['a', 'ab', 'a']);
      });

      test('turning the ring or changing chips is not an edit', () {
        final s = screen();
        s.centre();
        final n = queries.length;
        s.wheel(5);
        s.next();
        s.previous();
        expect(queries.length, n);
      });

      test('at most three live results, the best first', () {
        final s = screen();
        s.centre(); // 'a': six matches
        // At most three: how many fit at the larger text tiers is the
        // design's Open question 13.
        expect(s.liveResults, isNotEmpty);
        expect(s.liveResults.length, lessThanOrEqualTo(3));
        final best = search('a');
        expect(s.liveResults, best.take(s.liveResults.length).toList());
      });

      test('Previous and Next walk the mode chips', () {
        final s = screen();
        s.next(); // ABC
        s.centre();
        expect(s.text, 'A');
        s.previous(); // abc
        s.centre();
        expect(s.text, 'Aa');
      });

      test('Menu deletes one character', () {
        final s = screen();
        s.centre();
        s.centre();
        s.menu();
        expect(s.text, 'a');
        expect(s.phase, SearchPhase.typing);
      });

      test('Menu held clears the field', () {
        final s = screen();
        s.centre();
        s.centre();
        s.menuHeld();
        expect(s.text, '');
        expect(s.phase, SearchPhase.typing);
      });

      test('Menu on an empty field leaves Search', () {
        final s = screen();
        s.menu();
        expect(s.phase, SearchPhase.left);
      });

      test('Play/Pause goes to the results, text kept, no play or pause', () {
        final s = screen();
        s.centre();
        s.wheel(1);
        s.centre(); // ab
        s.playPause();
        expect(s.phase, SearchPhase.results);
        expect(s.text, 'ab');
        expect(s.results, containsAll(['Abba', 'Abbey Road']));
        expect(playPauses, 0);
      });

      test('in the results the wheel scrolls the list, not the text', () {
        final s = screen();
        s.centre();
        s.playPause();
        final n = queries.length;
        s.wheel(3);
        expect(s.text, 'a');
        expect(queries.length, n);
      });

      test('Menu in the results returns to typing with the text kept', () {
        final s = screen();
        s.centre();
        s.wheel(1);
        s.centre();
        s.playPause();
        s.menu();
        expect(s.phase, SearchPhase.typing);
        expect(s.text, 'ab');
      });

      test('Play/Pause in the results is play or pause again', () {
        final s = screen();
        s.centre();
        s.playPause();
        s.playPause();
        expect(s.phase, SearchPhase.results);
        expect(playPauses, 1);
      });
    },
  );
}
