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
}
