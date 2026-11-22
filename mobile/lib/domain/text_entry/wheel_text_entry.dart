// SPDX-License-Identifier: MPL-2.0

// The letter ring: a port of the firmware's wheel text entry and its search
// mode (app/include/wheel_input.h, app/ui/input/wheel_input.c,
// docs/ui-input.md). Not a reimplementation: the state, the glyph pages, the
// limits and every branch follow wi_text_t and its functions line for line,
// so the phone types exactly like the device. Tests:
// test/domain/text_entry/wheel_text_entry_test.dart mirrors
// tests/test_wheel_input.c case for case.
//
// Like the firmware, the model works in UTF-8 bytes: the length, the byte
// limit and the cursor are byte counts, and `maxChars` counts code points.
//
// Pure Dart: no Flutter. The view (the ring, the chips, the masked reveal,
// the Menu hold timer) is the UI layer's; the view constants the firmware
// keeps beside its view are exported below for it.

import 'dart:convert';

/// The wheel's inputs (wi_ev_t).
enum WheelEvent { scroll, select, menu, play, left, right }

/// Whether the entry is still running, was confirmed or cancelled
/// (wi_result_t).
enum WheelResult { running, ok, cancelled }

/// Which characters the entry accepts (wi_charset_t).
enum TextCharset { any, digits, hex }

/// The four pages of characters (wi_page_t).
enum TextPage { lower, upper, digits, symbols }

/// The mode chips under the letter ring, in firmware order (wi_chip_t): four
/// pages of characters (the wheel turns through the page), then two actions
/// (Select types a space / deletes one).
enum TextChip { lower, upper, digits, symbols, space, delete }

/// What a key on the ring does (wi_key_kind_t).
enum TextKeyKind { char, space, delete }

/// Bytes of text at most, whatever the limits say (WI_TEXT_MAX).
const int kTextMaxBytes = 64;

/// Glyphs the view shows in the ring: the current one and four either side
/// (RING_N in app/ui/input/ui_input.c).
const int kRingGlyphs = 9;

/// A typed character of a masked entry stays visible this long, then it
/// becomes a dot (REVEAL_MS in app/ui/input/ui_input.c).
const Duration kMaskedRevealTime = Duration(milliseconds: 800);

/// Menu held this long clears the text (CLEAR_HOLD_MS in
/// app/ui/input/ui_input.c); the view then calls [WheelTextEntry.clear].
const Duration kClearHoldTime = Duration(milliseconds: 600);

/// The characters of each page, in ring order (k_pages).
const List<String> kTextPages = [
  'abcdefghijklmnopqrstuvwxyz',
  'ABCDEFGHIJKLMNOPQRSTUVWXYZ',
  '0123456789',
  r'''.,-_@!?#$%&*+=/:;'"()[]{}<>~^\|`''',
];

int _wrap(int v, int n) {
  final r = v.remainder(n);
  return r < 0 ? r + n : r;
}

bool _isCont(int c) => (c & 0xC0) == 0x80;

List<int> _encode(int cp) {
  if (cp < 0x80) return [cp];
  if (cp < 0x800) return [0xC0 | (cp >> 6), 0x80 | (cp & 0x3F)];
  if (cp < 0x10000) {
    return [0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F)];
  }
  return [
    0xF0 | (cp >> 18),
    0x80 | ((cp >> 12) & 0x3F),
    0x80 | ((cp >> 6) & 0x3F),
    0x80 | (cp & 0x3F),
  ];
}

bool _allowed(TextCharset cs, int c) => switch (cs) {
  TextCharset.digits => c >= 0x30 && c <= 0x39,
  TextCharset.hex =>
    (c >= 0x30 && c <= 0x39) ||
        (c >= 0x61 && c <= 0x66) ||
        (c >= 0x41 && c <= 0x46),
  TextCharset.any => true,
};

int _pageCountChars(TextCharset cs, TextPage p) {
  var n = 0;
  for (final c in kTextPages[p.index].codeUnits) {
    if (_allowed(cs, c)) n++;
  }
  return n;
}

/// A limit of 0 (or one above the maximum) means the maximum; C takes these as
/// unsigned, so a negative one is a huge one and also means the maximum.
int _limit(int v) => v <= 0 || v > kTextMaxBytes ? kTextMaxBytes : v;

/// The text entry (wi_text_t) and its search mode.
///
/// Feed it [event]s; read [text], [chip], [sel] and the ring ([stripLength],
/// [stripKey]) to draw it. In search mode ([search] true) [onChange] is
/// called with the new text after every edit, as the firmware's view calls
/// the request's `change` callback, so the caller can filter a list live.
final class WheelTextEntry {
  /// wi_text_init with a String [initial] text (encoded to UTF-8 first; a
  /// Dart String always encodes to valid UTF-8).
  factory WheelTextEntry({
    String initial = '',
    required int maxChars,
    TextCharset charset = TextCharset.any,
    bool masked = false,
    bool search = false,
    void Function(String text)? onChange,
  }) => WheelTextEntry.fromUtf8(
    utf8.encode(initial),
    maxChars: maxChars,
    charset: charset,
    masked: masked,
    search: search,
    onChange: onChange,
  );

  /// wi_text_init from raw UTF-8 bytes: keeps the valid, printable prefix,
  /// never an invalid sequence, a control byte or half a character, then
  /// drops whole characters from the end until it fits [maxChars].
  WheelTextEntry.fromUtf8(
    List<int> initial, {
    required int maxChars,
    this.charset = TextCharset.any,
    this.masked = false,
    this.search = false,
    this.onChange,
  }) : maxChars = _limit(maxChars),
       chip = charset == TextCharset.digits || charset == TextCharset.hex
           ? TextChip.digits
           : TextChip.lower {
    var i = 0;
    var n = 0;
    while (i < initial.length && initial[i] != 0 && n < kTextMaxBytes) {
      final c = initial[i] & 0xFF;
      final l = c < 0x80
          ? 1
          : (c >= 0xC2 && c < 0xE0)
          ? 2
          : (c >= 0xE0 && c < 0xF0)
          ? 3
          : (c >= 0xF0 && c < 0xF5)
          ? 4
          : 0;
      if (l == 0 ||
          (l == 1 && (c < 0x20 || c == 0x7F)) ||
          n + l > kTextMaxBytes ||
          n + l > _maxBytes) {
        break;
      }
      var ok = true;
      for (var k = 1; k < l; k++) {
        ok = ok && i + k < initial.length && _isCont(initial[i + k] & 0xFF);
      }
      if (!ok) break;
      for (var k = 0; k < l; k++) {
        _text.add(initial[i + k] & 0xFF);
      }
      n += l;
      i += l;
    }
    _cursor = _text.length;
    while (chars > this.maxChars) {
      // too many characters: drop from the end, a whole character at a time
      var e = _text.length;
      do {
        e--;
      } while (e > 0 && _isCont(_text[e]));
      _text.length = e;
      _cursor = e;
    }
  }

  final List<int> _text = [];
  int _cursor = 0;
  int _maxBytes = kTextMaxBytes;
  bool _typed = false;
  bool _edited = false;

  /// Code points at most, never more than [kTextMaxBytes].
  final int maxChars;
  final TextCharset charset;

  /// A password: the view shows dots (and the last typed one for a moment).
  final bool masked;

  /// Incremental: [changed] is raised, and [onChange] called, after every
  /// edit.
  final bool search;

  /// Called with the new text after every edit when [search] is true (the
  /// firmware view's `change` callback).
  final void Function(String text)? onChange;

  /// Set by an edit; the view or caller clears it after reading.
  bool changed = false;

  /// The mode chip in use.
  TextChip chip;

  /// Index into the current chip's ring.
  int sel = 0;

  /// The text, as UTF-8 bytes (t->text).
  List<int> get utf8Bytes => List.unmodifiable(_text);

  /// The text. The firmware's initial-text check admits a few sequences
  /// strict UTF-8 refuses: overlong and surrogate three-byte forms (E0 80 80,
  /// ED A0 80) and four-byte forms above U+10FFFF (F4 90 80 80). The bytes are
  /// kept as the firmware keeps them and [chars] counts each as one, but Dart
  /// decodes them as one U+FFFD per byte (three or four here). Views must
  /// size and mask from [chars], not from `text.runes`.
  String get text => utf8.decode(_text, allowMalformed: true);

  /// UTF-8 bytes of the text (t->len).
  int get lengthBytes => _text.length;

  /// UTF-8 byte offset of the cursor, always on a character boundary. The text
  /// always grows at its end, so in use this is [lengthBytes].
  int get cursor => _cursor;

  /// The byte limit, at most [kTextMaxBytes] (t->max_bytes).
  int get maxBytes => _maxBytes;

  /// The last event inserted a character: the view reveals it while it is
  /// new.
  bool get typed => _typed;

  /// Code points in the text (wi_text_chars).
  int get chars {
    var n = 0;
    for (final c in _text) {
      if (!_isCont(c)) n++;
    }
    return n;
  }

  /// Limit the text to [maxBytes] bytes (0 = [kTextMaxBytes]); truncates the
  /// current text at a character boundary if it is longer
  /// (wi_text_set_byte_limit). Not an edit, as in the firmware.
  void setByteLimit(int maxBytes) {
    _maxBytes = _limit(maxBytes);
    while (_text.length > _maxBytes) {
      var e = _text.length;
      do {
        e--;
      } while (e > 0 && _isCont(_text[e]));
      _text.length = e;
    }
    if (_cursor > _text.length) _cursor = _text.length;
  }

  /// The chips this entry offers, in order: the pages with a usable
  /// character, space only for free text, delete always (wi_text_chips).
  List<TextChip> chips() => [
    for (final p in TextPage.values)
      if (_pageCountChars(charset, p) != 0) TextChip.values[p.index],
    if (charset == TextCharset.any) TextChip.space,
    TextChip.delete,
  ];

  /// Keys on the current chip's ring (wi_text_strip_len): the page's usable
  /// characters, or one key for an action chip.
  int stripLength() => chip.index < TextChip.space.index
      ? _pageCountChars(charset, TextPage.values[chip.index])
      : 1;

  /// The key at [i] on the current chip's ring (wi_text_strip_key): the
  /// character, or 'space' / 'del' for the action chips. A page with no key
  /// at [i] gives an empty label.
  (TextKeyKind, String) stripKey(int i) {
    if (chip == TextChip.space) return (TextKeyKind.space, 'space');
    if (chip == TextChip.delete) return (TextKeyKind.delete, 'del');
    var left = i;
    for (final c in kTextPages[chip.index].codeUnits) {
      if (_allowed(charset, c) && left-- == 0) {
        return (TextKeyKind.char, String.fromCharCode(c));
      }
    }
    return (TextKeyKind.char, '');
  }

  /// Type a code point directly (a hardware keyboard shortcut). False if
  /// refused: not a Unicode scalar value, a control character, outside the
  /// charset, or over a limit (wi_text_insert).
  bool insert(int codePoint) {
    final ok = _insert(codePoint);
    _notify();
    return ok;
  }

  /// Delete the whole text (a long press of Menu: [kClearHoldTime]). Counts
  /// as an edit (wi_text_clear).
  void clear() {
    if (_text.isNotEmpty) {
      _text.clear();
      _cursor = 0;
      _edit();
    }
    _notify();
  }

  /// One wheel input (wi_text_event). [steps] is for [WheelEvent.scroll] and
  /// arrives already accelerated.
  ///
  /// scroll turns the ring (wrapping both ways); Select types the key under
  /// the ring (a space on the space chip, deletes one on the delete chip);
  /// Menu deletes one, or cancels when the text is empty; Play confirms;
  /// Left / Right move to the previous / next chip (wrapping).
  WheelResult event(WheelEvent ev, [int steps = 0]) {
    final r = _event(ev, steps);
    _notify();
    return r;
  }

  WheelResult _event(WheelEvent ev, int steps) {
    final n = stripLength();
    _typed = false;
    switch (ev) {
      case WheelEvent.scroll:
        // The firmware never has an empty ring here (the chips offered all
        // have a key); the guard only keeps a chip set by hand to an empty
        // page from dividing by zero. (C adds in int32; Dart ints are 64-bit,
        // so an overflowing sel + steps differs, but neither is reachable.)
        if (n != 0) sel = _wrap(sel + steps, n);
      case WheelEvent.select:
        final (kind, label) = stripKey(sel);
        switch (kind) {
          case TextKeyKind.char:
            _typed = _insert(label.isEmpty ? 0 : label.codeUnitAt(0));
          case TextKeyKind.space:
            _typed = _insert(0x20);
          case TextKeyKind.delete:
            if (_cursor > 0) _backspace();
        }
      case WheelEvent.menu:
        if (_cursor > 0) {
          _backspace();
        } else if (_text.isEmpty) {
          return WheelResult.cancelled;
        }
      case WheelEvent.play:
        return WheelResult.ok;
      case WheelEvent.left:
        _moveChip(-1);
      case WheelEvent.right:
        _moveChip(1);
    }
    if (sel >= stripLength()) sel = 0;
    return WheelResult.running;
  }

  bool _insert(int cp) {
    if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      return false; // not a Unicode scalar value
    }
    final b = _encode(cp);
    if (cp < 0x80 && (cp < 0x20 || cp == 0x7F || !_allowed(charset, cp))) {
      return false;
    }
    if (cp >= 0x80 && charset != TextCharset.any) return false;
    if (chars >= maxChars || _text.length + b.length > _maxBytes) return false;
    _text.insertAll(_cursor, b);
    _cursor += b.length;
    _edit();
    return true;
  }

  void _backspace() {
    var s = _cursor;
    do {
      s--;
    } while (s > 0 && _isCont(_text[s]));
    _text.removeRange(s, _cursor);
    _cursor = s;
    _edit();
  }

  // Left / Right: the previous / next chip this entry offers.
  void _moveChip(int dir) {
    final c = chips();
    var at = 0;
    for (var i = 0; i < c.length; i++) {
      if (c[i] == chip) at = i;
    }
    chip = c[_wrap(at + dir, c.length)];
    sel = 0;
  }

  void _edit() {
    changed = true;
    _edited = true;
  }

  // The firmware's view calls `change` once after an event that edited a
  // search entry (feed() and the Menu hold in in_tick, ui_input.c).
  void _notify() {
    if (!_edited) return;
    _edited = false;
    if (search) onChange?.call(text);
  }
}
