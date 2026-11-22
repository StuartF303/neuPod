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

  test('cutUtf8 cuts at a character boundary, as the firmware stores text', () {
    expect(cutUtf8('abc', 2), 'ab');
    expect(cutUtf8('abc', 3), 'abc');
    expect(cutUtf8('a\u00e9', 2), 'a'); // é is two bytes
    expect(cutUtf8('a\u00e9', 3), 'a\u00e9');
    expect(cutUtf8('\u{1F3B5}x', 3), ''); // a 4-byte character never splits
    expect(cutUtf8('', 0), '');
  });

  test(
    'songSortKey: title cut to 63 bytes, file name to 64, sort tag to 31',
    () {
      final t63 = 'a' * 63;
      expect(
        songSortKey(title: '${t63}zzz', titleSort: '', fileName: 'x.flac'),
        collateKey(t63),
      );
      expect(
        songSortKey(title: '', titleSort: '', fileName: '${'b' * 70}.flac'),
        collateKey('b' * 64),
      );
      final s31 = 'c' * 31;
      expect(
        songSortKey(title: 'Title', titleSort: '${s31}ddd', fileName: 'x'),
        collateKey('Title', sort: s31),
      );
      // The sort tag wins over the title; an untagged song sorts by its file
      // name with the extension, as on the device.
      expect(
        songSortKey(title: '', titleSort: '', fileName: 'The Song.mp3'),
        collateKey('Song.mp3'),
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
      final name = utf8.decode(unhex(nameHex));
      final sort = sortHex == '-' ? null : utf8.decode(unhex(sortHex));
      final key = collateKey(name, sort: sort);
      if (!_same(key, unhex(keyHex))) {
        mismatches.add(
          '${jsonEncode(name)} / ${jsonEncode(sort)}: '
          'dart ${_hex(key)} c $keyHex',
        );
      }
    }
    expect(mismatches, isEmpty, reason: mismatches.take(20).join('\n'));
  });
}

bool _same(List<int> a, List<int> b) =>
    a.length == b.length && compareKeys(a, b) == 0;

String _hex(List<int> b) =>
    b.map((x) => x.toRadixString(16).padLeft(2, '0')).join();
