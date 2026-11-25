// SPDX-License-Identifier: MPL-2.0

/// A fingerprint of a file's audio, with no tag bytes in it, so the same
/// audio is recognised after a move, a rename or a retag (owner decision 20,
/// decision 0011). The scanner stores it per track and carries a track's
/// ratings and play counts to its new path and size when a rescan sees one
/// file gone and one new file with the same fingerprint.
///
/// `a1:` and the hex SHA-256 of: the container family (`flac`, `mp3`, `mp4`,
/// `wav`), the audio payload's length in bytes (8 bytes, little endian), and
/// the first [fingerprintBytes] of the payload. The payload is where the
/// container keeps the audio, and only that:
///
/// - FLAC: everything after the last metadata block (`fLaC`, then blocks
///   until the one flagged last; an ID3v2 tag in front is skipped). Tags,
///   pictures and padding are metadata blocks.
/// - MP3: from the first frame header after any ID3v2 tags to the end,
///   less an ID3v1 tag, an APEv2 tag and a Lyrics3v2 block at the end.
/// - MP4 / M4A: the body of the first top-level `mdat` box. Taggers rewrite
///   `moov`/`udta` and may move `mdat`, never its body.
/// - WAV: the body of the `data` chunk (`LIST`/`id3 ` chunks are tags).
///
/// One read of 64 KiB per new or changed file, in the scan's isolate, while
/// the file is open for its tags anyway. The payload's length makes two
/// different recordings with the same first 64 KiB (a shared silent intro)
/// still differ unless they are also exactly as long.
library;

import 'dart:typed_data';

import 'package:crypto/crypto.dart';
import 'package:neupod/data/scan/tag_reader.dart' show ByteSource;

/// Bytes of the audio payload hashed.
const fingerprintBytes = 64 * 1024;

/// The fingerprint of [src], a file with extension [ext], or null when the
/// format has none here or its audio cannot be found.
String? audioFingerprint(ByteSource src, String ext) {
  final family = switch (ext) {
    'flac' => 'flac',
    'mp3' => 'mp3',
    'm4a' || 'mp4' => 'mp4',
    'wav' => 'wav',
    _ => null,
  };
  if (family == null) return null;
  final range = switch (family) {
    'flac' => _flac(src),
    'mp3' => _mp3(src),
    'mp4' => _mp4(src),
    _ => _wav(src),
  };
  if (range == null) return null;
  final (start, end) = range;
  final length = end - start;
  if (start < 0 || length <= 0 || end > src.length) return null;
  final head = src.readAt(
    start,
    length < fingerprintBytes ? length : fingerprintBytes,
  );
  final len = ByteData(8)..setUint64(0, length, Endian.little);
  final digest = sha256.convert([
    ...family.codeUnits,
    0,
    ...len.buffer.asUint8List(),
    ...head,
  ]);
  return 'a1:$digest';
}

bool _is(Uint8List b, int at, String s) {
  if (at < 0 || at + s.length > b.length) return false;
  for (var i = 0; i < s.length; i++) {
    if (b[at + i] != s.codeUnitAt(i)) return false;
  }
  return true;
}

/// The length of an ID3v2 tag at [at] (header, body and footer), or 0.
int _id3v2At(ByteSource src, int at) {
  final h = src.readAt(at, 10);
  if (h.length < 10 || !_is(h, 0, 'ID3') || h[3] == 0xFF || h[4] == 0xFF) {
    return 0;
  }
  if ((h[6] | h[7] | h[8] | h[9]) & 0x80 != 0) return 0;
  final size = (h[6] << 21) | (h[7] << 14) | (h[8] << 7) | h[9];
  return 10 + size + ((h[5] & 0x10) != 0 ? 10 : 0);
}

(int, int)? _flac(ByteSource src) {
  var at = _id3v2At(src, 0);
  if (!_is(src.readAt(at, 4), 0, 'fLaC')) return null;
  at += 4;
  while (true) {
    final h = src.readAt(at, 4);
    if (h.length < 4) return null;
    at += 4 + ((h[1] << 16) | (h[2] << 8) | h[3]);
    if (at > src.length) return null;
    if (h[0] & 0x80 != 0) return (at, src.length);
  }
}

bool _mp3Header(Uint8List b, int i) {
  if (i + 4 > b.length || b[i] != 0xFF || (b[i + 1] & 0xE0) != 0xE0) {
    return false;
  }
  final version = (b[i + 1] >> 3) & 3, layer = (b[i + 1] >> 1) & 3;
  final bitrate = b[i + 2] >> 4, rate = (b[i + 2] >> 2) & 3;
  return version != 1 && layer != 0 && bitrate != 15 && rate != 3;
}

(int, int)? _mp3(ByteSource src) {
  var at = 0;
  for (var n = _id3v2At(src, at); n > 0; n = _id3v2At(src, at)) {
    at += n;
  }
  // The first frame header within 64 KiB (padding after a tag is skipped).
  final scan = src.readAt(at, fingerprintBytes);
  var first = -1;
  for (var i = 0; i + 4 <= scan.length; i++) {
    if (_mp3Header(scan, i)) {
      first = at + i;
      break;
    }
  }
  if (first < 0) return null;
  var end = src.length;
  // Trailing tags, in the order they are written: APEv2 or Lyrics3v2 before
  // an ID3v1.
  if (end - 128 >= first && _is(src.readAt(end - 128, 3), 0, 'TAG')) {
    end -= 128;
  }
  for (var again = true; again;) {
    again = false;
    final ape = src.readAt(end - 32, 32);
    if (end - 32 >= first && _is(ape, 0, 'APETAGEX')) {
      final size = ByteData.sublistView(ape).getUint32(12, Endian.little);
      final header = ByteData.sublistView(ape).getUint32(20, Endian.little);
      if (size < 32) break; // the size counts the footer: a damaged tag
      end -= size + ((header & 0x80000000) != 0 ? 32 : 0);
      again = true;
      continue;
    }
    final ly = src.readAt(end - 15, 15);
    if (end - 15 >= first && _is(ly, 6, 'LYRICS200')) {
      final digits = String.fromCharCodes(ly.sublist(0, 6));
      final size = int.tryParse(digits);
      if (size != null) {
        end -= size + 15;
        again = true;
      }
    }
  }
  return end > first ? (first, end) : null;
}

(int, int)? _mp4(ByteSource src) {
  var at = 0;
  while (at + 8 <= src.length) {
    final h = src.readAt(at, 16);
    if (h.length < 8) return null;
    final d = ByteData.sublistView(h);
    var size = d.getUint32(0);
    var header = 8;
    if (size == 1) {
      if (h.length < 16) return null;
      size = d.getUint64(8);
      header = 16;
    } else if (size == 0) {
      size = src.length - at;
    }
    if (size < header) return null;
    if (_is(h, 4, 'mdat')) {
      final end = at + size > src.length ? src.length : at + size;
      return (at + header, end);
    }
    at += size;
  }
  return null;
}

(int, int)? _wav(ByteSource src) {
  final h = src.readAt(0, 12);
  if (!_is(h, 0, 'RIFF') || !_is(h, 8, 'WAVE')) return null;
  var at = 12;
  while (at + 8 <= src.length) {
    final c = src.readAt(at, 8);
    if (c.length < 8) return null;
    final size = ByteData.sublistView(c).getUint32(4, Endian.little);
    if (_is(c, 0, 'data')) {
      final end = size == 0xFFFFFFFF || at + 8 + size > src.length
          ? src.length
          : at + 8 + size;
      return (at + 8, end);
    }
    at += 8 + size + (size & 1);
  }
  return null;
}
