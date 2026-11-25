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
/// Done only for new or changed files, in the scan's isolate, on the handle
/// already open for their tags. It reads the payload's first 64 KiB plus the
/// container's headers (FLAC block headers, MP4 box headers, WAV chunk
/// headers, at most a few hundred bytes); an MP3 also reads up to 64 KiB to
/// find its first frame header, so up to about 128 KiB in all. The payload's
/// length makes two different recordings with the same first 64 KiB (a
/// shared silent intro) still differ unless they are also exactly as long.
///
/// Damaged input never hangs or reads without bound: every walk stops at the
/// end of the file, steps strictly forward, and FLAC stops after
/// [maxFlacBlocks] metadata blocks; anything that does not add up gives no
/// fingerprint.
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

/// The most FLAC metadata blocks walked before the file is taken as not a
/// FLAC (a real file has a handful; a file of zeros after `fLaC` would
/// otherwise be walked 4 bytes at a time).
const maxFlacBlocks = 1024;

(int, int)? _flac(ByteSource src) {
  var at = _id3v2At(src, 0);
  if (!_is(src.readAt(at, 4), 0, 'fLaC')) return null;
  at += 4;
  for (var n = 0; n < maxFlacBlocks; n++) {
    final h = src.readAt(at, 4);
    if (h.length < 4) return null;
    final size = (h[1] << 16) | (h[2] << 8) | h[3];
    if (size > src.length - at - 4) return null; // the block runs past the end
    at += 4 + size;
    if (h[0] & 0x80 != 0) return (at, src.length);
  }
  return null;
}

bool _mp3Header(Uint8List b, int i) {
  if (i + 4 > b.length || b[i] != 0xFF || (b[i + 1] & 0xE0) != 0xE0) {
    return false;
  }
  final version = (b[i + 1] >> 3) & 3, layer = (b[i + 1] >> 1) & 3;
  final bitrate = b[i + 2] >> 4, rate = (b[i + 2] >> 2) & 3;
  return version != 1 && layer != 0 && bitrate != 15 && rate != 3;
}

/// The six ASCII digits of a Lyrics3v2 size, or null (no sign, no spaces).
int? _sixDigits(Uint8List b) {
  if (b.length < 6) return null;
  var v = 0;
  for (var i = 0; i < 6; i++) {
    final c = b[i];
    if (c < 0x30 || c > 0x39) return null;
    v = v * 10 + (c - 0x30);
  }
  return v;
}

(int, int)? _mp3(ByteSource src) {
  var at = 0;
  for (var n = _id3v2At(src, at); n > 0; n = _id3v2At(src, at)) {
    at += n;
  }
  if (at >= src.length) return null;
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
  // an ID3v1. Each one taken off must move the end strictly back and leave
  // it after the first frame; anything else stops the walk where it is.
  if (end - 128 >= first && _is(src.readAt(end - 128, 3), 0, 'TAG')) {
    end -= 128;
  }
  while (true) {
    var next = end;
    if (end - 32 >= first) {
      final ape = src.readAt(end - 32, 32);
      if (_is(ape, 0, 'APETAGEX')) {
        final d = ByteData.sublistView(ape);
        final size = d.getUint32(12, Endian.little);
        final header = (d.getUint32(20, Endian.little) & 0x80000000) != 0;
        // The size counts the items and the footer, not the header.
        if (size >= 32) next = end - size - (header ? 32 : 0);
      }
    }
    if (next == end && end - 15 >= first) {
      final ly = src.readAt(end - 15, 15);
      if (_is(ly, 6, 'LYRICS200')) {
        final size = _sixDigits(ly);
        if (size != null) next = end - 15 - size;
      }
    }
    if (next >= end || next <= first) break;
    end = next;
  }
  return (first, end);
}

(int, int)? _mp4(ByteSource src) {
  var at = 0;
  while (at <= src.length - 8) {
    final h = src.readAt(at, 16);
    if (h.length < 8) return null;
    final d = ByteData.sublistView(h);
    var size = d.getUint32(0);
    var header = 8;
    if (size == 1) {
      if (h.length < 16) return null;
      // A 64-bit size; the VM reads 2^63 and up as negative, which the
      // checks below refuse.
      size = d.getUint64(8);
      header = 16;
    } else if (size == 0) {
      size = src.length - at; // to the end of the file
    }
    if (size < header) return null;
    final last = size > src.length - at; // runs past the end
    if (_is(h, 4, 'mdat')) {
      return (at + header, last ? src.length : at + size);
    }
    if (last) return null;
    at += size;
  }
  return null;
}

(int, int)? _wav(ByteSource src) {
  final h = src.readAt(0, 12);
  if (!_is(h, 0, 'RIFF') || !_is(h, 8, 'WAVE')) return null;
  var at = 12;
  while (at <= src.length - 8) {
    final c = src.readAt(at, 8);
    if (c.length < 8) return null;
    final size = ByteData.sublistView(c).getUint32(4, Endian.little);
    final room = src.length - at - 8;
    if (_is(c, 0, 'data')) {
      // A streaming writer leaves the size at 0xFFFFFFFF: to the end.
      return (
        at + 8,
        size == 0xFFFFFFFF || size > room ? src.length : at + 8 + size,
      );
    }
    if (size > room) return null; // the chunk runs past the end
    at += 8 + size + (size & 1);
  }
  return null;
}
