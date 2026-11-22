// SPDX-License-Identifier: MPL-2.0

import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/library/collation.dart';

int cmp(String a, String b) => collateCompare(a, null, b, null);

List<int> unhex(String h) => [
  for (var i = 0; i + 1 < h.length; i += 2)
    int.parse(h.substring(i, i + 2), radix: 16),
];

void main() {
  // The cases of tests/test_library.c, one test per firmware TEST().
  test('collate_case_and_the', () {
    expect(cmp('abba', 'ABBA'), 0);
    expect(cmp('The Beatles', 'Beatles'), 0);
    expect(cmp('THE BEATLES', 'beatles'), 0);
    expect(cmp('The Band', 'Beatles'), lessThan(0));
    expect(cmp('Theatre of Tragedy', 'Them'), lessThan(0));
    expect(cmp('Theory', 'Therapy'), lessThan(0));
    expect(cmp('The ', 'A'), greaterThan(0));
    expect(cmp('A Perfect Circle', 'Perfect Circle'), 0);
    expect(cmp('An Horse', 'horse'), 0);
    expect(cmp('A Perfect Circle', 'Abba'), greaterThan(0));
    expect(cmp('Abba', 'Anvil'), lessThan(0));
    expect(cmp('A ', 'A'), greaterThan(0));
    expect(cmp('An ', 'An'), greaterThan(0));
  });

  test('collate_diacritics', () {
    expect(cmp('Émile', 'Emile'), 0);
    expect(cmp('Beyoncé', 'Beyonce'), 0);
    expect(cmp('ß', 'ss'), 0);
    expect(cmp('Æon', 'Aeon'), 0);
    expect(cmp('Sigur Rós', 'Sigur Ros'), 0);
    expect(cmp('Ørsted', 'Orsted'), 0);
    expect(cmp('Dvořák', 'Dvorak'), 0);
    expect(cmp('Łódź', 'Lodz'), 0);
    expect(cmp('Œuvre', 'Oeuvre'), 0);
    expect(cmp('Ĳ', 'ij'), 0);
    expect(cmp('É', 'F'), lessThan(0));
    expect(cmp('Zoé', 'Zoe'), 0);
  });

  test('collate_classes', () {
    expect(cmp('10cc', 'Abba'), lessThan(0));
    expect(cmp('99 Luftballons', 'Abba'), lessThan(0));
    expect(cmp('!!!', '10cc'), lessThan(0));
    expect(cmp(' leading space', 'a'), lessThan(0));
    expect(cmp('ab c', 'abc'), lessThan(0));
    expect(cmp('&', '~'), lessThan(0));
    expect(cmp('~', 'α'), lessThan(0));
    expect(cmp('α', '0'), lessThan(0));
    expect(cmp('α', 'β'), lessThan(0));
    expect(cmp('あ', '\u{1F3B5}'), lessThan(0));
  });

  test('collate_sort_tag_wins', () {
    expect(
      collateCompare('Weird Al', 'Yankovic, Weird Al', 'Zed', null),
      lessThan(0),
    );
    expect(
      collateCompare('Weird Al', 'Yankovic, Weird Al', 'Yardbirds', null),
      lessThan(0),
    );
    expect(collateCompare('Weird Al', '', 'Zed', null), lessThan(0));
    expect(collateCompare('Abba', 'Zzz', 'Beatles', null), greaterThan(0));
    expect(collateCompare('x', 'The Y', 'y', null), 0);
  });

  test('collate_buckets', () {
    expect(collateBucket(collateKey('Abba')), 1);
    expect(collateBucket(collateKey('The Zombies')), 26);
    expect(collateBucket(collateKey('Émile')), 5);
    expect(collateBucket(collateKey('10cc')), 0);
    expect(collateBucket(collateKey('!!!')), 0);
    expect(collateBucket(collateKey('α')), 0);
    expect(collateKey('a' * 299, cap: 40), hasLength(40));
    // The fast-scroll letters.
    expect(collateLetter(collateKey('The Zombies')), 'Z');
    expect(collateLetter(collateKey('10cc')), '#');
    expect(collateLetter(collateKey('')), '#');
  });

  test('firmwareCut is lib_build.c cut(): cap - 1 bytes, whole characters', () {
    List<int> b(String s) => utf8.encode(s);
    expect(firmwareCut(b('abc'), 3), b('ab'));
    expect(firmwareCut(b('abc'), 4), b('abc'));
    expect(firmwareCut(b('a\u00e9'), 3), b('a')); // é is two bytes
    expect(firmwareCut(b('a\u00e9'), 4), b('a\u00e9'));
    expect(firmwareCut(b('\u{1F3B5}x'), 4), isEmpty); // never splits
    expect(firmwareCut([0x61, 0, 0x62], 10), [0x61]); // a C string
  });

  test(
    'songSortKey: title cut to 63 bytes, file name to 64, sort tag to 31',
    () {
      List<int> b(String s) => utf8.encode(s);
      // The title text comes already cut by firmwareText (the data layer).
      expect(
        songSortKey(titleText: b('Title'), titleSortText: [], fileName: 'x'),
        collateKey('Title'),
      );
      expect(
        songSortKey(
          titleText: [],
          titleSortText: [],
          fileName: '${'b' * 70}.flac',
        ),
        collateKey('b' * 64),
      );
      expect(
        songSortKey(
          titleText: b('Title'),
          titleSortText: b('Sort'),
          fileName: 'x',
        ),
        collateKey('Title', sort: 'Sort'),
      );
      // An untagged song sorts by its file name with the extension.
      expect(
        songSortKey(titleText: [], titleSortText: [], fileName: 'The Song.mp3'),
        collateKey('Song.mp3'),
      );
      // Raw (malformed) bytes collate as the firmware collates them.
      expect(
        songSortKey(titleText: [0x43, 0xE9], titleSortText: [], fileName: 'x'),
        collateKeyBytes([0x43, 0xE9]),
      );
    },
  );

  test('keys are cut at 96 bytes and never contain zero', () {
    expect(collateKey('x' * 500), hasLength(collateKeyMax));
    expect(collateKey('日' * 100), hasLength(collateKeyMax));
    expect(collateKey('a\u0000b'), [0xC0]); // C stops at a NUL
    expect(collateKey('\u{1F600}' * 30).contains(0), isFalse);
  });

  // Byte-for-byte against the firmware: mobile/test/domain/library/
  // collate_golden.tsv is lib_collate_key() itself, compiled for the host by
  // scripts/app/gen-collate-golden.sh.
  test('every key matches lib_collate_key() byte for byte', () {
    final lines = File('test/domain/library/collate_golden.tsv')
        .readAsLinesSync()
        .where((l) => l.isNotEmpty && !l.startsWith('#'))
        .toList();
    expect(lines.length, greaterThan(1000));
    final mismatches = <String>[];
    for (final line in lines) {
      final [nameHex, sortHex, keyHex] = line.split('\t');
      // Raw bytes, as the C saw them (some are not valid UTF-8).
      final name = unhex(nameHex);
      final sort = sortHex == '-' ? null : unhex(sortHex);
      final key = collateKeyBytes(name, sort: sort);
      if (!_same(key, unhex(keyHex))) {
        mismatches.add('$nameHex / $sortHex: dart ${_hex(key)} c $keyHex');
      }
      // And through the String API wherever the bytes are valid UTF-8.
      final text = _tryUtf8(name);
      final sortText = sort == null ? null : _tryUtf8(sort);
      if (text != null && (sort == null || sortText != null)) {
        if (!_same(collateKey(text, sort: sortText), key)) {
          mismatches.add('$nameHex via String');
        }
      }
    }
    expect(mismatches, isEmpty, reason: mismatches.take(20).join('\n'));
  });
}

String? _tryUtf8(List<int> b) {
  try {
    return utf8.decode(b);
  } on FormatException {
    return null;
  }
}

bool _same(List<int> a, List<int> b) =>
    a.length == b.length && compareKeys(a, b) == 0;

String _hex(List<int> b) =>
    b.map((x) => x.toRadixString(16).padLeft(2, '0')).join();
