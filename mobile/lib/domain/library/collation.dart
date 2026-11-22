// SPDX-License-Identifier: MPL-2.0

/// The firmware's sort rule (owner decision 11): a byte-for-byte port of
/// `lib_collate_key()`, `lib_collate_cmp()` and `lib_collate_bucket()` in
/// `app/library/lib_collate.c`.
///
/// A key is one byte per character, ordered by class, so that comparing keys
/// byte by byte (SQLite compares BLOBs with `memcmp`, a prefix first) gives
/// the device's display order:
///
/// - `0x20..0x7E`: ASCII symbols and space, as themselves (space sorts first)
/// - `0x7F` + UTF-8: any other non-letter character, by code point
/// - `0xB0..0xB9`: digits
/// - `0xC0..0xD9`: letters a..z, case and Latin-1 / Latin Extended-A accents
///   folded (ß, Æ, Œ, Þ, Ĳ expand to two letters)
///
/// One leading article (`The `, `A `, `An `, any case) is skipped when
/// something follows it. A sort tag, when present, is used instead of the
/// name. The key is cut at [collateKeyMax] bytes.
library;

import 'dart:convert';
import 'dart:typed_data';

/// `LIB_KEY_MAX` in `app/include/library.h`.
const collateKeyMax = 96;

const _kMark = 0x7F;
const _kDigit = 0xB0;
const _kLetter = 0xC0;

// k_fold in lib_collate.c: (lo, hi, folded letters). U+00D7 and U+00F7 are
// symbols and stay as they are.
const _fold = <(int, int, String)>[
  (0xC0, 0xC5, 'a'), (0xC6, 0xC6, 'ae'), (0xC7, 0xC7, 'c'), //
  (0xC8, 0xCB, 'e'), (0xCC, 0xCF, 'i'), (0xD0, 0xD0, 'd'),
  (0xD1, 0xD1, 'n'), (0xD2, 0xD6, 'o'), (0xD8, 0xD8, 'o'),
  (0xD9, 0xDC, 'u'), (0xDD, 0xDD, 'y'), (0xDE, 0xDE, 'th'),
  (0xDF, 0xDF, 'ss'), (0xE0, 0xE5, 'a'), (0xE6, 0xE6, 'ae'),
  (0xE7, 0xE7, 'c'), (0xE8, 0xEB, 'e'), (0xEC, 0xEF, 'i'),
  (0xF0, 0xF0, 'd'), (0xF1, 0xF1, 'n'), (0xF2, 0xF6, 'o'),
  (0xF8, 0xF8, 'o'), (0xF9, 0xFC, 'u'), (0xFD, 0xFD, 'y'),
  (0xFE, 0xFE, 'th'), (0xFF, 0xFF, 'y'),
  (0x100, 0x105, 'a'), (0x106, 0x10D, 'c'), (0x10E, 0x111, 'd'),
  (0x112, 0x11B, 'e'), (0x11C, 0x123, 'g'), (0x124, 0x127, 'h'),
  (0x128, 0x131, 'i'), (0x132, 0x133, 'ij'), (0x134, 0x135, 'j'),
  (0x136, 0x138, 'k'), (0x139, 0x142, 'l'), (0x143, 0x14B, 'n'),
  (0x14C, 0x151, 'o'), (0x152, 0x153, 'oe'), (0x154, 0x159, 'r'),
  (0x15A, 0x161, 's'), (0x162, 0x167, 't'), (0x168, 0x173, 'u'),
  (0x174, 0x175, 'w'), (0x176, 0x178, 'y'), (0x179, 0x17E, 'z'),
  (0x17F, 0x17F, 's'),
];

String? _foldLookup(int cp) {
  if (cp < 0xC0 || cp > 0x17F) return null;
  for (final (lo, hi, s) in _fold) {
    if (cp >= lo && cp <= hi) return s;
  }
  return null;
}

/// The C string as bytes: UTF-8, stopping at the first NUL as C would.
Uint8List _cString(String s) {
  final bytes = utf8.encode(s);
  final nul = bytes.indexOf(0);
  return nul < 0 ? bytes : Uint8List.sublistView(bytes, 0, nul);
}

/// `article_len()`: length of a leading "The ", "A " or "An " (any case)
/// that the key skips; 0 when the name is only the article or has none.
int _articleLen(Uint8List s) {
  int at(int i) => i < s.length ? s[i] : 0;
  final a = at(0) | 0x20;
  final b = at(0) != 0 ? at(1) | 0x20 : 0;
  if (a == 0x74 &&
      b == 0x68 &&
      (at(2) | 0x20) == 0x65 &&
      at(3) == 0x20 &&
      at(4) != 0) {
    return 4;
  }
  if (a == 0x61 && at(1) == 0x20 && at(2) != 0) return 2;
  if (a == 0x61 && b == 0x6E && at(2) == 0x20 && at(3) != 0) return 3;
  return 0;
}

/// The collation key of [name], or of [sort] when it is non-empty, cut at
/// [cap] bytes (default [collateKeyMax]). Port of `lib_collate_key()`; the
/// returned list is the key without the C version's trailing zero.
Uint8List collateKey(String name, {String? sort, int cap = collateKeyMax}) {
  if (cap <= 0) return Uint8List(0);
  final src = _cString(sort != null && sort.isNotEmpty ? sort : name);
  final key = Uint8List(cap);
  var n = 0;
  void put(int b) {
    if (n < cap) key[n] = b;
    n++;
  }

  var p = _articleLen(src);
  int at(int i) => i < src.length ? src[i] : 0;
  while (p < src.length && n < cap) {
    // next_cp(): malformed bytes come back as themselves (as Latin-1).
    var c = src[p++];
    final b0 = at(p), b1 = at(p + 1), b2 = at(p + 2);
    if (c >= 0xC2 && c < 0xE0 && (b0 & 0xC0) == 0x80) {
      c = ((c & 0x1F) << 6) | (b0 & 0x3F);
      p += 1;
    } else if (c >= 0xE0 &&
        c < 0xF0 &&
        (b0 & 0xC0) == 0x80 &&
        (b1 & 0xC0) == 0x80) {
      c = ((c & 0x0F) << 12) | ((b0 & 0x3F) << 6) | (b1 & 0x3F);
      p += 2;
    } else if (c >= 0xF0 &&
        c < 0xF5 &&
        (b0 & 0xC0) == 0x80 &&
        (b1 & 0xC0) == 0x80 &&
        (b2 & 0xC0) == 0x80) {
      c =
          ((c & 0x07) << 18) |
          ((b0 & 0x3F) << 12) |
          ((b1 & 0x3F) << 6) |
          (b2 & 0x3F);
      p += 3;
    }
    if (c < 0x80) {
      if (c >= 0x61 && c <= 0x7A) {
        put(_kLetter + (c - 0x61));
      } else if (c >= 0x41 && c <= 0x5A) {
        put(_kLetter + (c - 0x41));
      } else if (c >= 0x30 && c <= 0x39) {
        put(_kDigit + (c - 0x30));
      } else if (c >= 0x20 && c < 0x7F) {
        put(c);
      }
      // Control characters and DEL do not sort.
      continue;
    }
    final f = _foldLookup(c);
    if (f != null) {
      for (final unit in f.codeUnits) {
        put(_kLetter + (unit - 0x61));
      }
      continue;
    }
    put(_kMark); // Everything else: by code point, as UTF-8.
    if (c < 0x800) {
      put(0xC0 | (c >> 6));
      put(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      put(0xE0 | (c >> 12));
      put(0x80 | ((c >> 6) & 0x3F));
      put(0x80 | (c & 0x3F));
    } else {
      put(0xF0 | (c >> 18));
      put(0x80 | ((c >> 12) & 0x3F));
      put(0x80 | ((c >> 6) & 0x3F));
      put(0x80 | (c & 0x3F));
    }
  }
  return Uint8List.sublistView(key, 0, n < cap ? n : cap);
}

/// `memcmp` order of two keys, a prefix first: -1, 0 or 1. This is how
/// SQLite orders BLOBs, so `ORDER BY sort_key` agrees with it.
int compareKeys(List<int> a, List<int> b) {
  final n = a.length < b.length ? a.length : b.length;
  for (var i = 0; i < n; i++) {
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  }
  return a.length == b.length ? 0 : (a.length < b.length ? -1 : 1);
}

/// Port of `lib_collate_cmp()`.
int collateCompare(String a, String? sortA, String b, String? sortB) =>
    compareKeys(collateKey(a, sort: sortA), collateKey(b, sort: sortB));

/// Port of `lib_collate_bucket()`: 1..26 for a key starting with a letter,
/// 0 (the `#` bucket) for a digit, a symbol or an empty key.
int collateBucket(List<int> key) {
  if (key.isEmpty) return 0;
  final k = key[0];
  return k >= _kLetter && k < _kLetter + 26 ? 1 + (k - _kLetter) : 0;
}

/// The fast-scroll letter for a key: `A`..`Z`, or `#`.
String collateLetter(List<int> key) {
  final b = collateBucket(key);
  return b == 0 ? '#' : String.fromCharCode(0x40 + b);
}
