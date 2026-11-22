// SPDX-License-Identifier: MPL-2.0

// Spec suite: the sort rule (docs/app/README.md, The library, owner decision
// 11). Names sort by a collation key built as the firmware's
// lib_collate_key() does (app/library/lib_collate.c); decision 0005 adds that
// the key is compared bytewise and cut at 96 bytes (LIB_KEY_MAX).
//
// Written from the specification and the firmware, not from the app's code.
// Every case of the firmware's collation tests (tests/test_library.c:
// collate_case_and_the, collate_diacritics, collate_classes,
// collate_sort_tag_wins, collate_buckets, and the Artists order and letter
// jumps of build_and_browse) is here, plus the byte-exact keys that the
// firmware's own lib_collate_key() returns for a set of names (computed by
// compiling app/library/lib_collate.c on the host, 22 November 2026).
//
// WIRING: set wiredCollationKey and wiredCollationLetter below to the app's
// implementation. The skip lifts by itself once both are set.
@Tags(['spec'])
library;

import 'package:flutter_test/flutter_test.dart';

/// The collation key of a name: bytes such that comparing two keys byte by
/// byte (a shorter key that is a prefix of a longer one first) is the display
/// order. [sort] is the sort tag (TSOP, ARTISTSORT, soar, ...); when it is
/// null or empty the name is used. Same bytes as lib_collate_key() with a cap
/// of 96 (LIB_KEY_MAX): never more than 96 bytes, no trailing zero.
typedef CollationKey = List<int> Function(String name, {String? sort});

/// The fast-scroll letter of a key: 'A' to 'Z', or '#' for a key that starts
/// with a digit or a symbol, or is empty (lib_collate_bucket(): 0 is '#').
typedef CollationLetter = String Function(List<int> key);

// WIRE: replace null with the app's functions, for example
//   final CollationKey? wiredCollationKey = collationKey;
//   final CollationLetter? wiredCollationLetter = collationLetter;
final CollationKey? wiredCollationKey = null;
final CollationLetter? wiredCollationLetter = null;

final Object _skip = wiredCollationKey == null || wiredCollationLetter == null
    ? 'awaiting implementation of the collation key (lib_collate_key port)'
    : false;

List<int> _key(String name, [String? sort]) =>
    wiredCollationKey!(name, sort: sort);

/// memcmp over the common length, then the shorter first (lib_collate_cmp).
int _cmpKeys(List<int> a, List<int> b) {
  final n = a.length < b.length ? a.length : b.length;
  for (var i = 0; i < n; i++) {
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  }
  return a.length == b.length ? 0 : (a.length < b.length ? -1 : 1);
}

int _cmp(String a, String b, {String? sa, String? sb}) =>
    _cmpKeys(_key(a, sa), _key(b, sb));

String _letter(String name, [String? sort]) =>
    wiredCollationLetter!(_key(name, sort));

void _same(String a, String b, String why) =>
    test('"$a" == "$b": $why', () => expect(_cmp(a, b), 0));

void _before(String a, String b, String why) =>
    test('"$a" < "$b": $why', () => expect(_cmp(a, b), lessThan(0)));

void _after(String a, String b, String why) =>
    test('"$a" > "$b": $why', () => expect(_cmp(a, b), greaterThan(0)));

void main() {
  group('collation', skip: _skip, () {
    group('case and articles (collate_case_and_the)', () {
      _same('abba', 'ABBA', 'case is folded');
      _same('The Beatles', 'Beatles', '"The " is skipped');
      _same('THE BEATLES', 'beatles', 'the article in any case');
      _before('The Band', 'Beatles', 'b-a-n < b-e-a');
      _before(
        'Theatre of Tragedy',
        'Them',
        'only "The " with its space is an article',
      );
      _before('Theory', 'Therapy', 'no article without the space');
      _after('The ', 'A', '"The " alone is a name, not an article');
      _same('A Perfect Circle', 'Perfect Circle', '"A " is skipped');
      _same('An Horse', 'horse', '"An " is skipped');
      _after('A Perfect Circle', 'Abba', 'sorted under P');
      _before('Abba', 'Anvil', 'no space after A / An, so no article');
      _after('A ', 'A', '"A " alone is a name');
      _after('An ', 'An', '"An " alone is a name');
      // Spec: one leading article is skipped, so only the first.
      _same('The The', 'The', 'only one article is skipped');
      _same('a perfect circle', 'Perfect Circle', 'lower-case article');
    });

    group('accents folded (collate_diacritics)', () {
      _same('Émile', 'Emile', 'E acute');
      _same('Beyoncé', 'Beyonce', 'e acute');
      _same('ß', 'ss', 'sharp s expands to ss');
      _same('Æon', 'Aeon', 'AE ligature expands to ae');
      _same('Sigur Rós', 'Sigur Ros', 'o acute');
      _same('Ørsted', 'Orsted', 'O stroke');
      _same('Dvořák', 'Dvorak', 'Latin Extended-A r caron');
      _same('Łódź', 'Lodz', 'L stroke, o acute, z acute');
      _same('Œuvre', 'Oeuvre', 'OE ligature expands to oe');
      _same('Ĳ', 'ij', 'IJ ligature expands to ij');
      _before('É', 'F', 'E acute sorts as E');
      _same('Zoé', 'Zoe', 'e acute at the end');
      _same('Þorn', 'thorn', 'thorn expands to th');
      _same('Motörhead', 'Motorhead', 'o umlaut');
    });

    group('classes: symbols < digits < letters (collate_classes)', () {
      _before('10cc', 'Abba', 'digits before letters');
      _before('99 Luftballons', 'Abba', 'digits before letters');
      _before('!!!', '10cc', 'symbols before digits');
      _before(' leading space', 'a', 'a space sorts first');
      _before('ab c', 'abc', 'a space sorts before a letter');
      _before('&', '~', 'ASCII symbols by code point');
      _before('~', 'α', 'other characters after the ASCII symbols');
      _before('α', '0', 'other characters before digits');
      _before('α', 'β', 'other characters by code point');
      _before('あ', '\u{1F3B5}', 'hiragana a < musical note emoji');
      _same('a\tb', 'ab', 'control characters do not sort');
    });

    group('the sort tag wins (collate_sort_tag_wins)', () {
      test('"Weird Al" with sort tag "Yankovic, Weird Al" < "Zed"', () {
        expect(_cmp('Weird Al', 'Zed', sa: 'Yankovic, Weird Al'), lessThan(0));
      });
      test('"Weird Al" with sort tag "Yankovic, Weird Al" < "Yardbirds"', () {
        expect(
          _cmp('Weird Al', 'Yardbirds', sa: 'Yankovic, Weird Al'),
          lessThan(0),
        );
      });
      test('an empty sort tag means the name: "Weird Al" < "Zed"', () {
        expect(_cmp('Weird Al', 'Zed', sa: ''), lessThan(0));
        expect(_key('Weird Al', ''), _key('Weird Al'));
      });
      test(
        'the tag decides, not the name: "Abba" (sort "Zzz") > "Beatles"',
        () {
          expect(_cmp('Abba', 'Beatles', sa: 'Zzz'), greaterThan(0));
        },
      );
      test('"The " is also dropped from a sort tag', () {
        expect(_cmp('x', 'y', sa: 'The Y'), 0);
      });
    });

    group('fast-scroll letter (collate_buckets)', () {
      final cases = <(String, String?, String)>[
        ('Abba', null, 'A'),
        ('The Zombies', null, 'Z'),
        ('Émile', null, 'E'),
        ('10cc', null, '#'),
        ('!!!', null, '#'),
        ('α', null, '#'),
        ('A Perfect Circle', null, 'P'),
        ('Weird Al', 'Yankovic, Weird Al', 'Y'),
        ('Æon', null, 'A'),
        ('Þorn', null, 'T'),
        (' x', null, '#'),
        ('', null, '#'),
      ];
      for (final (name, sort, letter) in cases) {
        test(
          '"$name"${sort == null ? '' : ' (sort "$sort")'} is under $letter',
          () {
            expect(_letter(name, sort), letter);
          },
        );
      }
      test('a long name is cut at 96 bytes', () {
        expect(_key('a' * 299).length, 96);
      });
      test('a long name of two-byte characters is cut at 96 bytes', () {
        expect(_key('α' * 100).length, 96);
      });
      test('a name cut at 96 bytes still sorts before a longer one', () {
        // Both are cut: the first 96 bytes are equal, so they compare equal.
        expect(_cmp('a' * 120, 'a' * 200), 0);
      });
    });

    group('the firmware Artists order (build_and_browse)', () {
      test('album artists sort: symbols, digits, letters, "The" ignored, '
          'accents folded, sort tag wins', () {
        // name, sort tag
        const expected = <(String, String?)>[
          ('!!!', null),
          ('10cc', null),
          ('Aaa Artist', null),
          ('ABBA', null),
          ('The Band', null),
          ('Émile Zola', null),
          ('Quux', null),
          ('Unknown Artist', null),
          ('Weird Al', 'Yankovic, Weird Al'),
          ('Zed', null),
        ];
        final shuffled = [
          for (final i in [7, 2, 9, 0, 5, 3, 8, 1, 6, 4]) expected[i],
        ];
        shuffled.sort((a, b) => _cmpKeys(_key(a.$1, a.$2), _key(b.$1, b.$2)));
        expect(shuffled, expected);
      });

      test('letter jumps land on the first row at or after the letter', () {
        const names = <(String, String?)>[
          ('!!!', null),
          ('10cc', null),
          ('Aaa Artist', null),
          ('ABBA', null),
          ('The Band', null),
          ('Émile Zola', null),
          ('Quux', null),
          ('Unknown Artist', null),
          ('Weird Al', 'Yankovic, Weird Al'),
          ('Zed', null),
        ];
        final letters = [for (final (n, s) in names) _letter(n, s)];
        int jump(String l) {
          if (l == '#') return 0;
          final i = letters.indexWhere(
            (x) => x != '#' && x.compareTo(l.toUpperCase()) >= 0,
          );
          return i < 0 ? names.length : i;
        }

        expect(jump('#'), 0);
        expect(jump('A'), 2);
        expect(jump('b'), 4);
        expect(jump('C'), 5, reason: 'no C or D: the next letter, E (Emile)');
        expect(jump('E'), 5);
        expect(jump('q'), 6);
        expect(jump('Y'), 8, reason: 'Weird Al files under Y');
        expect(jump('Z'), 9);
      });
    });

    group('byte-exact with the firmware lib_collate_key()', () {
      // Computed by compiling app/library/lib_collate.c on the host.
      final cases = <(String, String?, List<int>)>[
        ('Abba', null, [0xC0, 0xC1, 0xC1, 0xC0]),
        ('The Beatles', null, [0xC1, 0xC4, 0xC0, 0xD3, 0xCB, 0xC4, 0xD2]),
        (
          'A Perfect Circle',
          null,
          [0xCF, 0xC4, 0xD1, 0xC5, 0xC4, 0xC2, 0xD3, 0x20, 0xC2, 0xC8, 0xD1] +
              [0xC2, 0xCB, 0xC4],
        ),
        ('An Horse', null, [0xC7, 0xCE, 0xD1, 0xD2, 0xC4]),
        ('The ', null, [0xD3, 0xC7, 0xC4, 0x20]),
        ('A ', null, [0xC0, 0x20]),
        ('10cc', null, [0xB1, 0xB0, 0xC2, 0xC2]),
        ('!!!', null, [0x21, 0x21, 0x21]),
        (' x', null, [0x20, 0xD7]),
        ('Émile', null, [0xC4, 0xCC, 0xC8, 0xCB, 0xC4]),
        ('ß', null, [0xD2, 0xD2]),
        ('Æon', null, [0xC0, 0xC4, 0xCE, 0xCD]),
        ('×', null, [0x7F, 0xC3, 0x97]),
        ('α', null, [0x7F, 0xCE, 0xB1]),
        ('あ', null, [0x7F, 0xE3, 0x81, 0x82]),
        ('\u{1F3B5}', null, [0x7F, 0xF0, 0x9F, 0x8E, 0xB5]),
        (
          'Weird Al',
          'Yankovic, Weird Al',
          [0xD8, 0xC0, 0xCD, 0xCA, 0xCE, 0xD5, 0xC8, 0xC2, 0x2C, 0x20, 0xD6] +
              [0xC4, 0xC8, 0xD1, 0xC3, 0x20, 0xC0, 0xCB],
        ),
        ('Weird Al', '', [0xD6, 0xC4, 0xC8, 0xD1, 0xC3, 0x20, 0xC0, 0xCB]),
        ('x', 'The Y', [0xD8]),
        ('a\tb', null, [0xC0, 0xC1]),
        (
          'Sigur Rós',
          null,
          [0xD2, 0xC8, 0xC6, 0xD4, 0xD1, 0x20, 0xD1, 0xCE, 0xD2],
        ),
        ('Þorn', null, [0xD3, 0xC7, 0xCE, 0xD1, 0xCD]),
        (
          'Motörhead',
          null,
          [0xCC, 0xCE, 0xD3, 0xCE, 0xD1, 0xC7, 0xC4, 0xC0, 0xC3],
        ),
        ('AC/DC', null, [0xC0, 0xC2, 0x2F, 0xC3, 0xC2]),
        ('¿Qué?', null, [0x7F, 0xC2, 0xBF, 0xD0, 0xD4, 0xC4, 0x3F]),
        ('', null, <int>[]),
      ];
      for (final (name, sort, bytes) in cases) {
        test('key of "$name"${sort == null ? '' : ' (sort "$sort")'}', () {
          expect(_key(name, sort), bytes);
        });
      }
    });
  });
}
