// SPDX-License-Identifier: MPL-2.0

// The audio fingerprint (owner decision 20, decision 0011): the same audio
// under any tags gives the same fingerprint, so a retag that changes the
// file's size still carries its ratings and play counts; other audio gives
// another.

import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/scan/audio_fingerprint.dart';
import 'package:neupod/data/scan/tag_reader.dart';

import '../../fixtures/tagged_audio.dart';

String? fp(List<int> bytes, String ext) =>
    audioFingerprint(BytesSource(Uint8List.fromList(bytes)), ext);

Uint8List _be32(int v) => Uint8List(4)..buffer.asByteData().setUint32(0, v);
Uint8List _le32(int v) =>
    Uint8List(4)..buffer.asByteData().setUint32(0, v, Endian.little);

List<int> box(String type, List<int> body) => [
  ..._be32(8 + body.length),
  ...ascii.encode(type),
  ...body,
];

List<int> chunk(String id, List<int> body) => [
  ...ascii.encode(id),
  ..._le32(body.length),
  ...body,
  if (body.length.isOdd) 0,
];

List<int> wav(List<int> data, {List<int> extra = const []}) {
  final body = [
    ...ascii.encode('WAVE'),
    ...chunk('fmt ', List.filled(16, 1)),
    ...extra,
    ...chunk('data', data),
  ];
  return [...ascii.encode('RIFF'), ..._le32(body.length), ...body];
}

List<int> id3v1() => [...ascii.encode('TAG'), ...List.filled(125, 0x20)];

List<int> ape(List<int> items) {
  final footer = [
    ...ascii.encode('APETAGEX'),
    ..._le32(2000),
    ..._le32(items.length + 32),
    ..._le32(1),
    ..._le32(0), // no header
    ...List.filled(8, 0),
  ];
  return [...items, ...footer];
}

void main() {
  final audio = List<int>.generate(100000, (i) => (i * 7919) & 0xFF);

  test('FLAC: tags, pictures and padding do not count; the frames do', () {
    final base = fixture('flac16_44k_st.flac');
    final a = fp(flacWithComments(base, ['TITLE=A']), 'flac');
    final b = fp(
      flacWithComments(base, ['TITLE=Another', 'ARTIST=X', 'GENRE=Jazz']),
      'flac',
    );
    expect(a, startsWith('a1:'));
    expect(b, a);
    expect(
      fp(flacWithComments(fixture('flac24_48k_st.flac'), []), 'flac'),
      isNot(a),
    );
    // An ID3v2 tag in front of fLaC is skipped too.
    final tagged = [
      ...id3v2([textFrame('TIT2', 'In front')]),
      ...flacWithComments(base, ['TITLE=A']),
    ];
    expect(fp(tagged, 'flac'), a);
  });

  test('MP3: ID3v2 in front, and APEv2, Lyrics3v2 and ID3v1 at the end, do '
      'not count', () {
    final frames = fixture('mp3_44k_st.mp3');
    final plain = fp(frames, 'mp3');
    expect(plain, isNotNull);
    final lyrics = [
      ...ascii.encode('LYRICSBEGIN'),
      ...ascii.encode('IND00002'),
      ...ascii.encode('11'),
    ];
    final lyricsBlock = [
      ...lyrics,
      ...ascii.encode(lyrics.length.toString().padLeft(6, '0')),
      ...ascii.encode('LYRICS200'),
    ];
    final retagged = [
      ...id3v2([textFrame('TIT2', 'A'), textFrame('TPE1', 'B')], version: 4),
      ...frames,
      ...ape(List.filled(40, 1)),
      ...lyricsBlock,
      ...id3v1(),
    ];
    expect(fp(retagged, 'mp3'), plain);
    // Two ID3v2 tags in a row, and padding after them.
    expect(
      fp([
        ...id3v2([textFrame('TIT2', 'A')]),
        ...id3v2([textFrame('TALB', 'C')]),
        0,
        0,
        0,
        ...frames,
      ], 'mp3'),
      plain,
    );
    // Other audio: the last frame cut off.
    expect(fp(frames.sublist(0, frames.length - 100), 'mp3'), isNot(plain));
  });

  test('MP4: only the mdat body counts, wherever it is', () {
    final ftyp = box('ftyp', ascii.encode('M4A mp42'));
    final mdat = box('mdat', audio);
    final small = [
      ...ftyp,
      ...box('moov', box('udta', [1, 2, 3])),
      ...mdat,
    ];
    final big = [
      ...ftyp,
      ...mdat,
      ...box('moov', box('udta', List.filled(5000, 9))),
      ...box('free', List.filled(100, 0)),
    ];
    expect(fp(small, 'm4a'), isNotNull);
    expect(fp(big, 'mp4'), fp(small, 'm4a'));
    final other = [...ftyp, ...box('mdat', audio.reversed.toList())];
    expect(fp(other, 'm4a'), isNot(fp(small, 'm4a')));
  });

  test('WAV: only the data chunk counts', () {
    final a = fp(wav(audio), 'wav');
    final b = fp(
      wav(audio, extra: chunk('LIST', ascii.encode('INFOINAM title!'))),
      'wav',
    );
    expect(a, isNotNull);
    expect(b, a);
    expect(fp(wav(audio.sublist(1)), 'wav'), isNot(a));
  });

  test('the same first 64 KiB but another length is other audio', () {
    expect(
      fp(wav(audio), 'wav'),
      isNot(fp(wav(audio.sublist(0, 90000)), 'wav')),
    );
  });

  test('no fingerprint where the audio cannot be found, or the format is '
      'not opened by the scanner', () {
    expect(fp(ascii.encode('not a flac'), 'flac'), isNull);
    expect(fp(List.filled(100, 0), 'mp3'), isNull);
    expect(fp(box('ftyp', [0]), 'm4a'), isNull);
    expect(fp(ascii.encode('RIFF....WAVE'), 'wav'), isNull);
    expect(fp(audio, 'ogg'), isNull);
    // A damaged APE size never loops.
    expect(
      fp([
        ...fixture('mp3_44k_st.mp3'),
        ...ascii.encode('APETAGEX'),
        ...List.filled(24, 0),
      ], 'mp3'),
      isNotNull,
    );
  });

  // Damaged input: every case ends, quickly, with no fingerprint or the one
  // of the audio that is there. A hang here would time the test out.
  group('damaged input', () {
    final frames = fixture('mp3_44k_st.mp3');
    final plain = fp(frames, 'mp3');

    List<int> lyrics3(String size) => [
      ...frames,
      ...ascii.encode('LYRICSBEGIN'),
      ...ascii.encode(size),
      ...ascii.encode('LYRICS200'),
    ];

    test('Lyrics3v2: a negative size is not a size (it used to loop)', () {
      expect(fp(lyrics3('-00015'), 'mp3'), isNotNull);
      expect(fp(lyrics3('-00015'), 'mp3'), isNot(plain)); // nothing taken off
      expect(fp(lyrics3('+00011'), 'mp3'), isNot(plain)); // nor a plus sign
    });

    test('Lyrics3v2: a size that is not six digits is not a size', () {
      for (final s in ['00 011', '0x0011', '00001a', '      ']) {
        expect(fp(lyrics3(s), 'mp3'), isNotNull, reason: s);
        expect(fp(lyrics3(s), 'mp3'), isNot(plain), reason: s);
      }
    });

    test('Lyrics3v2: a size past the first frame stops the walk', () {
      expect(fp(lyrics3('999999'), 'mp3'), isNotNull);
      expect(fp(lyrics3('000000'), 'mp3'), isNotNull); // only the 15 bytes
    });

    test('APEv2: a size bigger than the file, or one that does not move the '
        'end, stops the walk', () {
      List<int> apeOf(int size, {bool header = false}) => [
        ...frames,
        ...ascii.encode('APETAGEX'),
        ..._le32(2000),
        ..._le32(size),
        ..._le32(0),
        ..._le32(header ? 0x80000000 : 0),
        ...List.filled(8, 0),
      ];
      expect(fp(apeOf(0xFFFFFFFF), 'mp3'), isNotNull);
      expect(fp(apeOf(0xFFFFFFFF, header: true), 'mp3'), isNotNull);
      expect(fp(apeOf(31), 'mp3'), isNotNull);
      // A right size is taken off.
      expect(fp(apeOf(32), 'mp3'), plain);
    });

    test('MP4: a 64-bit size at or above 2^63, or past the end of the '
        'file, gives no fingerprint', () {
      List<int> big(int hi, int lo, String type) => [
        ..._be32(1),
        ...ascii.encode(type),
        ..._be32(hi),
        ..._be32(lo),
        ...List.filled(64, 0),
      ];
      expect(fp(big(0x80000000, 0, 'free'), 'm4a'), isNull);
      expect(fp(big(0xFFFFFFFF, 0xFFFFFFFF, 'free'), 'm4a'), isNull);
      expect(fp(big(0x7FFFFFFF, 0xFFFFFFFF, 'free'), 'm4a'), isNull);
      // An mdat that claims more than the file: its body to the end.
      expect(fp(big(0x7FFFFFFF, 0xFFFFFFFF, 'mdat'), 'm4a'), isNotNull);
      expect(fp(big(0x80000000, 0, 'mdat'), 'm4a'), isNull);
      // A 32-bit box past the end, before any mdat.
      expect(
        fp([..._be32(0xFFFFFFF0), ...ascii.encode('moov'), 1, 2, 3], 'm4a'),
        isNull,
      );
    });

    test('FLAC: a block past the end, or no last block, gives no '
        'fingerprint, and a file of zeros is not walked for ever', () {
      final head = [...ascii.encode('fLaC')];
      expect(fp([...head, 0x80, 0xFF, 0xFF, 0xFF, 1, 2, 3], 'flac'), isNull);
      expect(fp([...head, 0x00, 0x00, 0x00, 0x04, 1, 2, 3, 4], 'flac'), isNull);
      final zeros = [...head, ...List.filled(8 * 1024 * 1024, 0)];
      final watch = Stopwatch()..start();
      expect(fp(zeros, 'flac'), isNull);
      expect(watch.elapsed, lessThan(const Duration(seconds: 2)));
      expect(maxFlacBlocks, 1024);
    });

    test('WAV: chunks are stepped over (odd sizes padded), and one past the '
        'end before the data gives no fingerprint', () {
      expect(
        fp(wav(audio, extra: chunk('odd!', [1, 2, 3])), 'wav'),
        fp(wav(audio), 'wav'),
      );
      final pastEnd = [
        ...ascii.encode('RIFF'),
        ..._le32(100),
        ...ascii.encode('WAVE'),
        ...ascii.encode('LIST'),
        ..._le32(0xFFFFFFF0),
        ...List.filled(20, 0),
      ];
      expect(fp(pastEnd, 'wav'), isNull);
      // A streaming size (0xFFFFFFFF) on the data chunk: to the end.
      final streaming = [
        ...ascii.encode('RIFF'),
        ..._le32(0xFFFFFFFF),
        ...ascii.encode('WAVE'),
        ...ascii.encode('data'),
        ..._le32(0xFFFFFFFF),
        ...audio,
      ];
      expect(fp(streaming, 'wav'), isNotNull);
    });
  });
}
