// SPDX-License-Identifier: MPL-2.0

// Test audio: the firmware's freely licensed fixtures in tests/assets/audio
// (read in place, not copied), and helpers that add tags to them in memory.

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

/// Path of a firmware fixture, from mobile/ (where `flutter test` runs).
String fixturePath(String name) => '../tests/assets/audio/$name';

Uint8List fixture(String name) => File(fixturePath(name)).readAsBytesSync();

Uint8List _le32(int v) =>
    Uint8List(4)..buffer.asByteData().setUint32(0, v, Endian.little);

/// [flac] with a VORBIS_COMMENT block of [comments] inserted after
/// STREAMINFO (any existing blocks are kept after it).
Uint8List flacWithComments(Uint8List flac, List<String> comments) {
  assert(String.fromCharCodes(flac.sublist(0, 4)) == 'fLaC');
  final siLen = (flac[5] << 16) | (flac[6] << 8) | flac[7];
  final siEnd = 8 + siLen;
  final wasLast = (flac[4] & 0x80) != 0;
  final body = BytesBuilder()
    ..add(_le32(6))
    ..add(ascii.encode('neuPod'))
    ..add(_le32(comments.length));
  for (final c in comments) {
    final b = utf8.encode(c);
    body
      ..add(_le32(b.length))
      ..add(b);
  }
  final block = body.takeBytes();
  return Uint8List.fromList([
    ...flac.sublist(0, 4),
    flac[4] & 0x7F, // STREAMINFO is no longer last
    ...flac.sublist(5, siEnd),
    (wasLast ? 0x80 : 0) | 4,
    (block.length >> 16) & 0xFF,
    (block.length >> 8) & 0xFF,
    block.length & 0xFF,
    ...block,
    ...flac.sublist(siEnd),
  ]);
}

List<int> _syncsafe(int v) => [
  (v >> 21) & 0x7F,
  (v >> 14) & 0x7F,
  (v >> 7) & 0x7F,
  v & 0x7F,
];

/// An ID3v2 text frame. [encoding]: 0 Latin-1, 1 UTF-16 with BOM,
/// 2 UTF-16BE, 3 UTF-8.
Uint8List textFrame(
  String id,
  String text, {
  int version = 3,
  int encoding = 3,
}) {
  final List<int> payload = switch (encoding) {
    0 => latin1.encode(text),
    1 => [
      0xFF,
      0xFE,
      for (final u in text.codeUnits) ...[u & 0xFF, u >> 8],
    ],
    2 => [
      for (final u in text.codeUnits) ...[u >> 8, u & 0xFF],
    ],
    _ => utf8.encode(text),
  };
  final data = [encoding, ...payload];
  final size = version == 4
      ? _syncsafe(data.length)
      : [
          (data.length >> 24) & 0xFF,
          (data.length >> 16) & 0xFF,
          (data.length >> 8) & 0xFF,
          data.length & 0xFF,
        ];
  return Uint8List.fromList([...ascii.encode(id), ...size, 0, 0, ...data]);
}

/// An ID3v2.[version] tag holding [frames], with some padding.
Uint8List id3v2(List<Uint8List> frames, {int version = 3}) {
  final body = [for (final f in frames) ...f, ...List.filled(64, 0)];
  return Uint8List.fromList([
    ...ascii.encode('ID3'),
    version,
    0,
    0,
    ..._syncsafe(body.length),
    ...body,
  ]);
}
