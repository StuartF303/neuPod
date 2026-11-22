// SPDX-License-Identifier: MPL-2.0

import 'dart:convert';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/scan/tag_reader.dart';

import '../../fixtures/tagged_audio.dart';

void main() {
  group('FLAC', () {
    test('untagged fixtures: duration and format from STREAMINFO', () {
      final t = readTags(BytesSource(fixture('flac16_44k_st.flac')), 'flac');
      expect(t.codec, 'flac');
      expect(t.title, isEmpty);
      expect(t.durationMs, 300);
      expect(t.sampleRate, 44100);
      expect(t.bitDepth, 16);
      final hi = readTags(BytesSource(fixture('flac24_96k_st.flac')), 'FLAC');
      expect((hi.sampleRate, hi.bitDepth), (96000, 24));
    });

    test('Vorbis comments: keys in any case, track "3/12" is 3', () {
      final bytes = flacWithComments(fixture('flac16_44k_st.flac'), [
        'title=Hey Jude',
        'ARTIST=The Beatles',
        'Album=Past Masters',
        'TRACKNUMBER=3/12',
        'COMMENT=ignored',
      ]);
      final t = readTags(BytesSource(bytes), 'flac');
      expect(t.title, 'Hey Jude');
      expect(t.artist, 'The Beatles');
      expect(t.album, 'Past Masters');
      expect(t.trackNo, 3);
      expect(t.durationMs, 300);
    });

    test('UTF-8 text survives', () {
      final bytes = flacWithComments(fixture('flac16_48k_mono.flac'), [
        'TITLE=Hoppípolla',
        'ARTIST=Sigur Rós',
        'ALBUM=Takk…',
      ]);
      final t = readTags(BytesSource(bytes), 'flac');
      expect(
        (t.title, t.artist, t.album),
        ('Hoppípolla', 'Sigur Rós', 'Takk…'),
      );
    });

    test('an ID3v2 tag in front of fLaC is skipped', () {
      final bytes = Uint8List.fromList([
        ...id3v2([textFrame('TIT2', 'ignored')]),
        ...flacWithComments(fixture('flac16_44k_st.flac'), ['TITLE=Real']),
      ]);
      expect(readTags(BytesSource(bytes), 'flac').title, 'Real');
    });

    test('not a FLAC file', () {
      expect(
        () => readTags(BytesSource(fixture('mp3_44k_st.mp3')), 'flac'),
        throwsA(isA<TagReadException>()),
      );
    });
  });

  group('MP3', () {
    test('untagged fixture: duration from the frames', () {
      final t = readTags(BytesSource(fixture('mp3_44k_st.mp3')), 'mp3');
      expect(t.codec, 'mp3');
      expect(t.sampleRate, 44100);
      expect(t.bitDepth, 0);
      // ffprobe: 0.291548 s of audio would need a Xing frame count; the
      // fixture's length is what the firmware computes for it too.
      expect(t.durationMs, greaterThan(0));
    });

    for (final version in [3, 4]) {
      test('ID3v2.$version text frames', () {
        final bytes = Uint8List.fromList([
          ...id3v2([
            textFrame('TIT2', 'Title', version: version),
            textFrame('TPE1', 'Artist', version: version),
            textFrame('TALB', 'Album', version: version),
            textFrame('TRCK', '07/10', version: version),
            textFrame('TPE2', 'Album Artist', version: version),
          ], version: version),
          ...fixture('mp3_44k_st.mp3'),
        ]);
        final t = readTags(BytesSource(bytes), 'mp3');
        expect(t.title, 'Title');
        expect(t.artist, 'Artist'); // TPE1, not TPE2
        expect(t.album, 'Album');
        expect(t.trackNo, 7);
        final plain = readTags(BytesSource(fixture('mp3_44k_st.mp3')), 'mp3');
        expect(t.durationMs, plain.durationMs);
      });
    }

    test('ID3 encodings: Latin-1, UTF-16 with BOM, UTF-16BE, UTF-8', () {
      final bytes = Uint8List.fromList([
        ...id3v2([
          textFrame('TIT2', 'Café', encoding: 0),
          textFrame('TPE1', 'Björk', encoding: 1),
          textFrame('TALB', 'Homogénic', encoding: 2, version: 4),
          textFrame('TRCK', '1', encoding: 3, version: 4),
        ], version: 4),
        ...fixture('mp3_44k_st.mp3'),
      ]);
      final t = readTags(BytesSource(bytes), 'mp3');
      expect(
        (t.title, t.artist, t.album, t.trackNo),
        ('Café', 'Björk', 'Homogénic', 1),
      );
    });

    test('not an MP3 file', () {
      expect(
        () => readTags(BytesSource(Uint8List(5000)), 'mp3'),
        throwsA(isA<TagReadException>()),
      );
    });
  });

  test('other extensions are refused for now', () {
    expect(
      () => readTags(BytesSource(Uint8List(10)), 'm4a'),
      throwsA(isA<TagReadException>()),
    );
  });

  test('damaged files never throw anything but TagReadException', () {
    final rnd = Random(1);
    final sources = [
      flacWithComments(fixture('flac16_44k_st.flac'), ['TITLE=x', 'ARTIST=y']),
      Uint8List.fromList([
        ...id3v2([textFrame('TIT2', 'x'), textFrame('TPE1', 'y')]),
        ...fixture('mp3_44k_st.mp3'),
      ]),
    ];
    for (var i = 0; i < 2000; i++) {
      final base = sources[i % 2];
      final b = Uint8List.fromList(base.sublist(0, rnd.nextInt(base.length)));
      for (var j = 0; j < 8 && b.isNotEmpty; j++) {
        b[rnd.nextInt(min(b.length, 600))] = rnd.nextInt(256);
      }
      try {
        readTags(BytesSource(b), i.isEven ? 'flac' : 'mp3');
      } on TagReadException {
        // expected for many
      }
    }
  });

  test('reads only headers: the bytes asked for stay small', () {
    final counting = _CountingSource(
      flacWithComments(fixture('flac24_96k_st.flac'), ['TITLE=x']),
    );
    readTags(counting, 'flac');
    expect(counting.bytesRead, lessThan(16 * 1024));
  });

  test('fixtures exist', () {
    expect(File(fixturePath('flac16_44k_st.flac')).existsSync(), isTrue);
    expect(utf8.decode(fixture('mp3_44k_st.txt')), contains('44100'));
  });
}

final class _CountingSource implements ByteSource {
  _CountingSource(this._bytes);

  final Uint8List _bytes;
  int bytesRead = 0;

  @override
  int get length => _bytes.length;

  @override
  Uint8List readAt(int offset, int count) {
    final r = BytesSource(_bytes).readAt(offset, count);
    bytesRead += r.length;
    return r;
  }
}
