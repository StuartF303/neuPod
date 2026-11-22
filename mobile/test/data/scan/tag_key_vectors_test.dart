// SPDX-License-Identifier: MPL-2.0

// The song sort key from tag to bytes, against the firmware itself (owner
// decision 11). test/fixtures/tag_keys/ holds small tagged files and
// expected.tsv, made by scripts/app/gen_tag_key_vectors.py from the
// firmware's tag parser (app/audio/meta.c) and library key (lib_build.c,
// lib_collate.c) compiled for the host. Cases: UTF-8 with a sort tag, a title
// past the 63-byte buffer, malformed UTF-8 (copied raw), a FLAC comment
// block over the 4096-byte scratch (the key falls back to the file name),
// an untagged file, UTF-16 surrogate pairs and a lone surrogate ('?'),
// UTF-16BE with a Latin-1 sort tag, Latin-1, and a sort tag past 31 bytes.
//
// The artist and album too, as the firmware's parser stores them and as its
// library keys them (the artist, or "Unknown Artist"; the album tag): UTF-16
// artists and albums that differ only in an emoji ('?' in the firmware, so
// one artist and one album there), a lone surrogate in an album, malformed
// UTF-8 and an album past 63 bytes, UTF-16BE and Latin-1.

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/scan/tag_reader.dart';
import 'package:neupod/domain/library/collation.dart';

String _hex(List<int> b) =>
    b.map((x) => x.toRadixString(16).padLeft(2, '0')).join();

void main() {
  const dir = 'test/fixtures/tag_keys';
  final lines = File('$dir/expected.tsv')
      .readAsLinesSync()
      .where((l) => l.isNotEmpty && !l.startsWith('#'))
      .toList();

  test('the vectors are there', () => expect(lines.length, 13));

  for (final line in lines) {
    final [
      name,
      titleHex,
      sortHex,
      keyHex,
      artistHex,
      albumHex,
      artistKeyHex,
      albumKeyHex,
    ] = line.split(
      '\t',
    );
    test(name, () {
      final bytes = File('$dir/$name').readAsBytesSync();
      final ext = name.substring(name.lastIndexOf('.') + 1);
      final tags = readTags(BytesSource(Uint8List.fromList(bytes)), ext);
      expect(_hex(tags.titleKeyText), titleHex, reason: 'title as stored');
      expect(_hex(tags.titleSortKeyText), sortHex, reason: 'sort as stored');
      final key = songSortKey(
        titleText: tags.titleKeyText,
        titleSortText: tags.titleSortKeyText,
        fileName: name,
      );
      expect(_hex(key), keyHex, reason: 'song key');
      expect(_hex(tags.artistKeyText), artistHex, reason: 'artist as stored');
      expect(_hex(tags.albumKeyText), albumHex, reason: 'album as stored');
      // The keys `index_file` / `stage_entity` fold on, from that text, as
      // the library view builds them.
      final artist = tags.artistKeyText.isNotEmpty
          ? tags.artistKeyText
          : utf8.encode('Unknown Artist');
      expect(
        _hex(collateKeyBytes(firmwareCut(artist, firmwarePoolCap))),
        artistKeyHex,
        reason: 'artist key',
      );
      expect(
        tags.albumKeyText.isEmpty
            ? ''
            : _hex(
                collateKeyBytes(
                  firmwareCut(tags.albumKeyText, firmwarePoolCap),
                ),
              ),
        albumKeyHex,
        reason: 'album key',
      );
    });
  }

  test('the displayed title stays whole while the key is the device\'s', () {
    final bytes = File('$dir/Big Comments.flac').readAsBytesSync();
    final tags = readTags(BytesSource(Uint8List.fromList(bytes)), 'flac');
    expect(tags.title, 'Ignored By Device'); // shown
    expect(tags.titleKeyText, isEmpty); // the device dropped the block
    final pairs = File('$dir/utf16_pairs.mp3').readAsBytesSync();
    final t = readTags(BytesSource(Uint8List.fromList(pairs)), 'mp3');
    expect(t.title, startsWith('Mood \u{1F3B5} x')); // the emoji is shown
  });

  test('two artists that differ in an emoji are one to the device, two on '
      'screen', () {
    AudioTags read(String name) => readTags(
      BytesSource(Uint8List.fromList(File('$dir/$name').readAsBytesSync())),
      'mp3',
    );
    final a = read('utf16_band_a.mp3'), b = read('utf16_band_b.mp3');
    expect(a.artist, '\u{1F600} Band');
    expect(b.artist, '\u{1F603} Band');
    expect(a.artistKeyText, b.artistKeyText);
    expect(utf8.decode(a.artistKeyText), '? Band');
    expect(a.albumKeyText, b.albumKeyText);
  });
}
