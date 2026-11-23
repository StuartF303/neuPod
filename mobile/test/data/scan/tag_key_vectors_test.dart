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
//
// The library tags (phase 3), against the same parser: the album artist
// (ALBUMARTIST, "ALBUM ARTIST", TPE2, TP2; the later one standing), the sort
// tags (ARTISTSORT, ALBUMSORT, ALBUMARTISTSORT, TSOP, TSOA, TSO2, TS2), the
// genre with its ID3v1 references ("(17)", a bare "18" in ID3 but not in
// Vorbis, "(RX)", "((", "(17)(18)", "(17)Custom", a number past the table,
// a genre past 31 bytes), the track and disc ("3/18", "2/3", a leading
// space, 300), the year (DATE "2001-05-04", TDRC with a time, TYER after
// TDRC, a DATE that is not a year after a YEAR), the compilation flag ("1",
// "yes", "0"), a comment block over the scratch (none of them), ID3v2.2,
// 2.3 and 2.4; MP4 items (texts, trkn, disk, ©day, cpil, ©gen, gnre, the
// sort items, an item over the 96-byte buffer, an AAC sample entry) and the
// ALAC duration (edit list, iTunSMPB, an edit list that does not fit); WAV
// durations (an odd chunk, WAVE_FORMAT_EXTENSIBLE, a streaming size, rates
// and depths the device does not play); then what `index_file` makes of them (the album artist, the
// compilation test, the artist and album keys), through the library view.

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:drift/drift.dart' show Value;
import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/data/scan/local_library_scanner.dart' show scanVersion;
import 'package:neupod/data/scan/tag_reader.dart';
import 'package:neupod/domain/library/collation.dart';
import 'package:neupod/domain/library/track.dart';

/// The tags as the scanner stores them: a parse that fails part-way gives
/// what it read (`TagReadException.partial`), as the firmware keeps it.
AudioTags _read(ByteSource src, String ext) {
  try {
    return readTags(src, ext);
  } on TagReadException catch (e) {
    return e.partial ?? (throw e);
  }
}

String _hex(List<int> b) =>
    b.map((x) => x.toRadixString(16).padLeft(2, '0')).join();

void main() {
  const dir = 'test/fixtures/tag_keys';
  final lines = File('$dir/expected.tsv')
      .readAsLinesSync()
      .where((l) => l.isNotEmpty && !l.startsWith('#'))
      .toList();

  test('the vectors are there', () => expect(lines.length, 47));

  for (final line in lines) {
    final f = line.split('\t');
    final name = f[0];
    test(name, () {
      final bytes = File('$dir/$name').readAsBytesSync();
      final ext = name.substring(name.lastIndexOf('.') + 1);
      final tags = _read(BytesSource(Uint8List.fromList(bytes)), ext);
      expect(_hex(tags.titleKeyText), f[1], reason: 'title as stored');
      expect(_hex(tags.titleSortKeyText), f[2], reason: 'sort as stored');
      final key = songSortKey(
        titleText: tags.titleKeyText,
        titleSortText: tags.titleSortKeyText,
        fileName: name,
      );
      expect(_hex(key), f[3], reason: 'song key');
      expect(_hex(tags.artistKeyText), f[4], reason: 'artist as stored');
      expect(_hex(tags.albumKeyText), f[5], reason: 'album as stored');
      expect(_hex(tags.albumArtistKeyText), f[6], reason: 'album artist');
      expect(_hex(tags.artistSortKeyText), f[7], reason: 'sort artist');
      expect(_hex(tags.albumSortKeyText), f[8], reason: 'sort album');
      expect(
        _hex(tags.albumArtistSortKeyText),
        f[9],
        reason: 'sort album artist',
      );
      expect(_hex(tags.genreKeyText), f[10], reason: 'genre as stored');
      expect('${tags.trackNo}', f[11], reason: 'track');
      expect('${tags.discNo}', f[12], reason: 'disc');
      expect('${tags.year}', f[13], reason: 'year');
      expect(tags.compilation ? '1' : '0', f[14], reason: 'flag');
      // The duration from the same frames and rate (TR_DUR is their
      // quotient in seconds). AAC: the device cannot play it and has none;
      // the phone plays it and takes the media header's.
      final [frames, rate] = f[20].split('/').map(int.parse).toList();
      if (tags.codec != 'aac') {
        expect(
          tags.durationMs,
          rate == 0 ? 0 : frames * 1000 ~/ rate,
          reason: 'duration',
        );
      } else {
        expect(rate, 0, reason: 'no duration for AAC on the device');
        // aac_tags.m4a is alac16_48k_mono.m4a with its sample entry made AAC:
        // the media header is the same, and m4a_tags.m4a (the same source as
        // ALAC, untrimmed) gives its length: 9600 frames at 48 kHz.
        expect(tags.durationMs, 200, reason: "the media header's length");
      }
    });
  }

  test('failed parses keep their tags, with the reason', () {
    for (final (name, ext, reason) in [
      ('truncated.flac', 'flac', UnreadableReason.damaged),
      ('no_frames.mp3', 'mp3', UnreadableReason.damaged),
      ('no_audio.m4a', 'm4a', UnreadableReason.damaged),
    ]) {
      final src = BytesSource(
        Uint8List.fromList(File('$dir/$name').readAsBytesSync()),
      );
      expect(
        () => readTags(src, ext),
        throwsA(
          isA<TagReadException>()
              .having((e) => e.reason, 'reason', reason)
              .having((e) => e.partial?.title, 'title', isNotEmpty),
        ),
        reason: name,
      );
    }
  });

  // `index_file`'s album artist, compilation test and entity keys, through
  // the library view: each file is a track of its own in a folder named
  // after it, stored as the scanner stores its tags; the view's ids hold the
  // keys' hex.
  test('index_file: compilation and the artist and album keys, through the '
      'library view', () async {
    final db = LibraryDatabase(NativeDatabase.memory());
    addTearDown(db.close);
    final view = DriftLibraryView(db);
    addTearDown(view.dispose);
    final src = await db
        .into(db.sources)
        .insert(
          SourcesCompanion.insert(
            kind: 'local_folder',
            uriOrBookmark: 'x',
            displayName: 'Music',
          ),
        );
    Value<Uint8List> blob(List<int> b) => Value(Uint8List.fromList(b));
    final byPath = <String, List<String>>{};
    for (final line in lines) {
      final f = line.split('\t');
      final name = f[0];
      final tags = _read(
        BytesSource(Uint8List.fromList(File('$dir/$name').readAsBytesSync())),
        name.substring(name.lastIndexOf('.') + 1),
      );
      final path = '$name/$name';
      byPath[path] = f;
      await db
          .into(db.tracks)
          .insert(
            TracksCompanion.insert(
              sourceId: src,
              relPath: path,
              uri: 'file:///$path',
              size: 1,
              mtime: 1,
              codec: tags.codec,
              title: tags.title,
              titleSort: tags.titleSort,
              titleSortKey: Uint8List(0),
              artist: tags.artist,
              album: tags.album,
              artistKeyText: blob(tags.artistKeyText),
              albumKeyText: blob(tags.albumKeyText),
              albumArtist: Value(tags.albumArtist),
              albumArtistKeyText: blob(tags.albumArtistKeyText),
              artistSortKeyText: blob(tags.artistSortKeyText),
              albumSortKeyText: blob(tags.albumSortKeyText),
              albumArtistSortKeyText: blob(tags.albumArtistSortKeyText),
              genre: Value(tags.genre),
              genreKeyText: blob(tags.genreKeyText),
              discNo: Value(tags.discNo),
              year: Value(tags.year),
              compilation: Value(tags.compilation),
              trackNo: tags.trackNo,
              durationMs: tags.durationMs,
              sampleRate: tags.sampleRate,
              bitDepth: tags.bitDepth,
              scanVersion: scanVersion,
            ),
          );
    }
    final artistIds = {for (final a in await view.artists()) a.id};
    final albums = await view.albumsByArtist();
    final songs = await view.songs();
    for (final MapEntry(key: path, value: f) in byPath.entries) {
      final comp = f[17] == '1';
      final song = songs.firstWhere((s) => s.uri == 'file:///$path');
      final album = albums.firstWhere((a) => a.id == song.albumId);
      expect(album.compilation, comp, reason: '$path: comp');
      if (!comp) {
        expect(artistIds, contains('artist:${f[18]}'), reason: '$path: aa');
        expect(album.artistId, 'artist:${f[18]}', reason: '$path: group');
      }
      if (f[19].isNotEmpty) {
        expect(album.id, endsWith('|${f[19]}'), reason: '$path: album key');
      }
    }
  });

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
