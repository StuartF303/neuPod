// SPDX-License-Identifier: MPL-2.0

/// Phase 1 tag reader (decision 0003): FLAC and MP3 title, artist, album,
/// track number and duration, ported from the firmware's `app/audio/meta.c`
/// (`flac_parse`, `vorbis_comments`, `id3v2_tags`, `id3_text_frame`,
/// `mp3_parse`, `meta_text_to_utf8`), read through [ByteSource]. Phase 3
/// adds the other fields and containers.
///
/// Every length read from a file is bounded by the file size and a cap, and
/// a damaged file gives partial tags or a [TagReadException], never a hang or
/// unbounded memory.
library;

import 'dart:convert';
import 'dart:typed_data';

/// Random-access bytes of one file (implemented over neupod_storage's
/// `StorageReader`, or over test bytes).
abstract interface class ByteSource {
  int get length;

  /// Up to [count] bytes at [offset]; fewer at the end of the file.
  Uint8List readAt(int offset, int count);
}

/// A [ByteSource] over bytes in memory (tests).
final class BytesSource implements ByteSource {
  BytesSource(this.bytes);

  final Uint8List bytes;

  @override
  int get length => bytes.length;

  @override
  Uint8List readAt(int offset, int count) {
    if (offset >= bytes.length || count <= 0) return Uint8List(0);
    final end = offset + count > bytes.length ? bytes.length : offset + count;
    return Uint8List.sublistView(bytes, offset, end);
  }
}

/// The file could not be read as the format its name says.
final class TagReadException implements Exception {
  const TagReadException(this.reason);

  /// Short and user-facing, for the greyed row: "not a FLAC file".
  final String reason;

  @override
  String toString() => 'TagReadException($reason)';
}

/// What phase 1 reads from a file. Empty strings and zeros mean "not
/// tagged".
final class AudioTags {
  const AudioTags({
    required this.codec,
    this.title = '',
    this.artist = '',
    this.album = '',
    this.trackNo = 0,
    this.durationMs = 0,
    this.durationEstimated = false,
    this.sampleRate = 0,
    this.bitDepth = 0,
  });

  /// `flac` or `mp3`.
  final String codec;
  final String title;
  final String artist;
  final String album;
  final int trackNo;
  final int durationMs;

  /// MP3 without a Xing/Info or VBRI frame count: estimated from the bitrate,
  /// as the firmware does.
  final bool durationEstimated;
  final int sampleRate;

  /// 0 for lossy formats.
  final int bitDepth;
}

/// Longest text frame or comment the reader keeps; longer ones are cut at a
/// character boundary (the firmware keeps less).
const _maxText = 1024;

/// Largest Vorbis comment block read; larger ones (art stuffed into
/// comments) are skipped, as the firmware does when it exceeds its scratch.
const _maxCommentBlock = 256 * 1024;

/// Reads the tags of [src], choosing the parser by [extension] (`flac`,
/// `mp3`; any case).
AudioTags readTags(ByteSource src, String extension) =>
    switch (extension.toLowerCase()) {
      'flac' => _readFlac(src),
      'mp3' => _readMp3(src),
      _ => throw TagReadException('$extension tags are not read yet'),
    };

int _be32(Uint8List b, int o) =>
    (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3];

int _le32(Uint8List b, int o) =>
    b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24);

bool _startsWith(Uint8List b, int o, String ascii) {
  if (o + ascii.length > b.length) return false;
  for (var i = 0; i < ascii.length; i++) {
    if (b[o + i] != ascii.codeUnitAt(i)) return false;
  }
  return true;
}

Uint8List _need(ByteSource src, int offset, int count) {
  final b = src.readAt(offset, count);
  if (b.length != count) throw const TagReadException('file is cut short');
  return b;
}

/// `meta_text_to_utf8`: [enc] 0 Latin-1, 1 UTF-16 with BOM, 2 UTF-16BE,
/// 3 UTF-8. Stops at the first NUL. Malformed UTF-8 is replaced, not thrown.
String _text(Uint8List s, int enc) {
  if (enc == 0 || enc == 3) {
    var end = s.indexOf(0);
    if (end < 0) end = s.length;
    final bytes = Uint8List.sublistView(s, 0, end);
    final text = enc == 0
        ? latin1.decode(bytes)
        : utf8.decode(bytes, allowMalformed: true);
    return _cut(text);
  }
  var le = false;
  var i = 0;
  if (enc == 1 && s.length >= 2) {
    if (s[0] == 0xFF && s[1] == 0xFE) {
      le = true;
      i = 2;
    } else if (s[0] == 0xFE && s[1] == 0xFF) {
      i = 2;
    }
  }
  final units = <int>[];
  for (; i + 1 < s.length; i += 2) {
    final u = le ? s[i] | (s[i + 1] << 8) : (s[i] << 8) | s[i + 1];
    if (u == 0) break;
    units.add(u);
  }
  // Unlike the firmware (whose font has no astral glyphs), surrogate pairs
  // are kept; a lone surrogate becomes U+FFFD.
  return _cut(String.fromCharCodes(units).replaceAll(_loneSurrogate, '�'));
}

final _loneSurrogate = RegExp(
  r'[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]',
);

String _cut(String s) {
  final t = s.trim();
  if (t.length <= _maxText) return t;
  var end = _maxText;
  if (end > 0 && (t.codeUnitAt(end - 1) & 0xFC00) == 0xD800) end--;
  return t.substring(0, end);
}

/// `track_from`: the leading digits ("3/12" is 3), capped as the firmware
/// caps it.
int _number(String s) {
  var v = 0;
  for (final c in s.codeUnits) {
    if (c < 0x30 || c > 0x39 || v >= 10000) break;
    v = v * 10 + (c - 0x30);
  }
  return v > 0xFFFF ? 0xFFFF : v;
}

/// `id3v2_size`: the whole ID3v2 tag's size including header and footer, or
/// 0 when there is none.
int _id3v2Size(Uint8List h) {
  if (h.length < 10 || !_startsWith(h, 0, 'ID3')) return 0;
  if ((h[6] | h[7] | h[8] | h[9]) & 0x80 != 0) return 0;
  final sz = (h[6] << 21) | (h[7] << 14) | (h[8] << 7) | h[9];
  return 10 + sz + ((h[5] & 0x10) != 0 ? 10 : 0);
}

// ---- FLAC -------------------------------------------------------------------

AudioTags _readFlac(ByteSource src) {
  final size = src.length;
  var pos = _id3v2Size(src.readAt(0, 10)); // some files carry ID3v2 in front
  if (!_startsWith(src.readAt(pos, 4), 0, 'fLaC')) {
    throw const TagReadException('not a FLAC file');
  }
  pos += 4;
  var rate = 0, bits = 0, totalFrames = 0;
  var haveInfo = false;
  final fields = _VorbisFields();
  for (var guard = 0; guard < 256; guard++) {
    final bh = _need(src, pos, 4);
    final last = (bh[0] & 0x80) != 0;
    final type = bh[0] & 0x7F;
    final len = (bh[1] << 16) | (bh[2] << 8) | bh[3];
    final body = pos + 4;
    if (type == 0) {
      if (len < 34) throw const TagReadException('damaged FLAC header');
      final si = _need(src, body, 34);
      rate = (si[10] << 12) | (si[11] << 4) | (si[12] >> 4);
      bits = (((si[12] & 1) << 4) | (si[13] >> 4)) + 1;
      totalFrames = ((si[13] & 0xF) << 32) | _be32(si, 14);
      haveInfo = true;
    } else if (type == 4 && len <= _maxCommentBlock && body + len <= size) {
      _vorbisComments(src.readAt(body, len), fields);
    }
    pos = body + len;
    if (last) break;
    if (pos >= size) throw const TagReadException('damaged FLAC header');
  }
  if (!haveInfo) throw const TagReadException('damaged FLAC header');
  return AudioTags(
    codec: 'flac',
    title: fields.title,
    artist: fields.artist,
    album: fields.album,
    trackNo: fields.track,
    durationMs: rate == 0 ? 0 : totalFrames * 1000 ~/ rate,
    sampleRate: rate,
    bitDepth: bits,
  );
}

class _VorbisFields {
  String title = '', artist = '', album = '';
  int track = 0;
}

/// `vorbis_comments` and the phase 1 keys of `vorbis_field`.
void _vorbisComments(Uint8List b, _VorbisFields t) {
  final n = b.length;
  if (n < 8) return;
  final vendor = _le32(b, 0);
  if (vendor > n - 8) return;
  var p = 4 + vendor;
  final count = _le32(b, p);
  p += 4;
  for (var i = 0; i < count && p + 4 <= n; i++) {
    final len = _le32(b, p);
    p += 4;
    if (len > n - p) return;
    final eq = b.indexOf(0x3D, p);
    if (eq >= 0 && eq < p + len) {
      final key = ascii
          .decode(Uint8List.sublistView(b, p, eq), allowInvalid: true)
          .toUpperCase();
      final value = Uint8List.sublistView(b, eq + 1, p + len);
      switch (key) {
        case 'TITLE':
          t.title = _text(value, 3);
        case 'ARTIST':
          t.artist = _text(value, 3);
        case 'ALBUM':
          t.album = _text(value, 3);
        case 'TRACKNUMBER':
          t.track = _number(_text(value, 3));
      }
    }
    p += len;
  }
}

// ---- MP3 --------------------------------------------------------------------

final class _Mp3Header {
  _Mp3Header(
    this.rate,
    this.kbps,
    this.frameBytes,
    this.spf,
    this.channels,
    this.version,
  );

  final int rate, kbps, frameBytes, spf, channels, version;
}

const _br1 = [
  0,
  32,
  40,
  48,
  56,
  64,
  80,
  96,
  112,
  128,
  160,
  192,
  224,
  256,
  320,
  0,
];
const _br2 = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0];
const _sr1 = [44100, 48000, 32000];

/// `mp3_header`: a Layer III frame header at [o], or null.
_Mp3Header? _mp3Header(Uint8List h, int o) {
  if (o + 4 > h.length) return null;
  if (h[o] != 0xFF || (h[o + 1] & 0xE0) != 0xE0) return null;
  final ver = (h[o + 1] >> 3) & 3, layer = (h[o + 1] >> 1) & 3;
  final bi = h[o + 2] >> 4, si = (h[o + 2] >> 2) & 3, pad = (h[o + 2] >> 1) & 1;
  if (ver == 1 || layer != 1 || bi == 0 || bi == 15 || si == 3) return null;
  final kbps = ver == 3 ? _br1[bi] : _br2[bi];
  final rate = ver == 3 ? _sr1[si] : _sr1[si] >> (ver == 2 ? 1 : 2);
  final channels = (h[o + 3] >> 6) == 3 ? 1 : 2;
  final frameBytes = (ver == 3 ? 144000 : 72000) * kbps ~/ rate + pad;
  return _Mp3Header(
    rate,
    kbps,
    frameBytes,
    ver == 3 ? 1152 : 576,
    channels,
    ver,
  );
}

class _Id3Fields {
  String title = '', artist = '', album = '';
  int track = 0;
}

/// `id3v2_tags` restricted to the phase 1 text frames.
void _id3v2Tags(ByteSource src, Uint8List h, int total, _Id3Fields t) {
  final ver = h[3];
  if (ver < 2 || ver > 4) return;
  var pos = 10;
  final end = total - ((h[5] & 0x10) != 0 ? 10 : 0);
  if (ver >= 3 && (h[5] & 0x40) != 0) {
    final e = src.readAt(pos, 4);
    if (e.length != 4) return;
    pos += ver == 4
        ? ((e[0] & 0x7F) << 21) |
              ((e[1] & 0x7F) << 14) |
              ((e[2] & 0x7F) << 7) |
              (e[3] & 0x7F)
        : _be32(e, 0) + 4;
  }
  final idLen = ver == 2 ? 3 : 4, hLen = ver == 2 ? 6 : 10;
  for (var guard = 0; guard < 200 && pos + hLen <= end; guard++) {
    final fh = src.readAt(pos, hLen);
    if (fh.length != hLen || fh[0] == 0) return;
    final id = ascii.decode(
      Uint8List.sublistView(fh, 0, idLen),
      allowInvalid: true,
    );
    final sz = ver == 2
        ? (fh[3] << 16) | (fh[4] << 8) | fh[5]
        : ver == 4
        ? ((fh[4] & 0x7F) << 21) |
              ((fh[5] & 0x7F) << 14) |
              ((fh[6] & 0x7F) << 7) |
              (fh[7] & 0x7F)
        : _be32(fh, 4);
    if (pos + hLen + sz > end) return;
    if (id.startsWith('T') && sz > 1 && sz <= 400) {
      final skip = ver == 4 && (fh[9] & 0x01) != 0
          ? 4
          : 0; // data length indicator
      if (sz > skip + 1) {
        var d = src.readAt(pos + hLen + skip, sz - skip);
        if (d.length == sz - skip) {
          if ((h[5] & 0x80) != 0 || (ver == 4 && (fh[9] & 0x02) != 0)) {
            d = _unsync(d); // FF 00 stands for FF
          }
          _id3TextFrame(id, d, t);
        }
      }
    }
    pos += hLen + sz;
  }
}

Uint8List _unsync(Uint8List d) {
  final out = BytesBuilder(copy: false);
  for (var i = 0; i < d.length; i++) {
    out.addByte(d[i]);
    if (d[i] == 0xFF && i + 1 < d.length && d[i + 1] == 0) i++;
  }
  return out.takeBytes();
}

void _id3TextFrame(String id, Uint8List d, _Id3Fields t) {
  if (d.length < 2) return;
  final enc = d[0] > 3 ? 0 : d[0];
  final s = Uint8List.sublistView(d, 1);
  switch (id) {
    case 'TIT2' || 'TT2':
      t.title = _text(s, enc);
    case 'TPE1' || 'TP1':
      t.artist = _text(s, enc);
    case 'TALB' || 'TAL':
      t.album = _text(s, enc);
    case 'TRCK' || 'TRK':
      t.track = _number(_text(s, enc));
  }
}

/// `mp3_parse`: tags, then the first frame header confirmed by the next one,
/// then the duration from Xing/Info (minus the LAME delay and padding, as
/// dr_mp3 delivers it), VBRI, or the bitrate (estimated).
AudioTags _readMp3(ByteSource src) {
  final size = src.length;
  final h10 = src.readAt(0, 10);
  final start = _id3v2Size(h10);
  final tags = _Id3Fields();
  if (start != 0) _id3v2Tags(src, h10, start > size ? size : start, tags);

  _Mp3Header? h;
  var found = 0;
  var pos = start;
  for (var scanned = 0; scanned < 65536 && h == null; scanned += 256) {
    final buf = src.readAt(pos, 512);
    if (buf.length < 4) break;
    for (var i = 0; i + 4 <= buf.length && i < 256; i++) {
      final c = _mp3Header(buf, i);
      if (c == null) continue;
      final nx = src.readAt(pos + i + c.frameBytes, 4);
      if (nx.length == 4) {
        final c2 = _mp3Header(nx, 0);
        if (c2 == null || c2.rate != c.rate) continue;
      }
      h = c;
      found = pos + i;
      break;
    }
    pos += 256;
  }
  if (h == null) throw const TagReadException('not an MP3 file');

  var frames = 0, delay = 0, padding = 0;
  final fr = src.readAt(found, 192);
  var side = h.version == 3
      ? (h.channels == 1 ? 17 : 32)
      : (h.channels == 1 ? 9 : 17);
  final got = fr.length < 16 ? 0 : fr.length;
  if (got != 0 && (fr[1] & 1) == 0) side += 2; // CRC follows the header
  if (got != 0 && 4 + side + 12 <= got) {
    final x = 4 + side;
    if ((_startsWith(fr, x, 'Xing') || _startsWith(fr, x, 'Info')) &&
        (fr[x + 7] & 1) != 0) {
      final flags = fr[x + 7];
      frames = _be32(fr, x + 8);
      final off =
          4 +
          side +
          8 +
          4 +
          ((flags & 2) != 0 ? 4 : 0) +
          ((flags & 4) != 0 ? 100 : 0) +
          ((flags & 8) != 0 ? 4 : 0);
      final fb = h.frameBytes < got ? h.frameBytes : got;
      if (off + 24 <= fb && fr[off] != 0) {
        final d = (fr[off + 21] << 4) | (fr[off + 22] >> 4);
        final pd = ((fr[off + 22] & 0xF) << 8) | fr[off + 23];
        delay = d + 528 + 1;
        padding = pd > 529 ? pd - 529 : 0;
      }
    }
  }
  // VBRI (Fraunhofer): 32 bytes after the frame header; the frame count is
  // exact and there is no LAME tag.
  if (frames == 0 && got >= 36 + 14 && _startsWith(fr, 36, 'VBRI')) {
    frames = _be32(fr, 36 + 14);
  }
  final audioBytes = size > found ? size - found : 0;
  int totalSamples;
  var estimated = false;
  if (frames != 0) {
    final raw = frames * h.spf;
    totalSamples = raw > delay + padding ? raw - delay - padding : raw;
  } else {
    totalSamples = h.kbps != 0 ? audioBytes * 8 * h.rate ~/ (h.kbps * 1000) : 0;
    estimated = true;
  }
  return AudioTags(
    codec: 'mp3',
    title: tags.title,
    artist: tags.artist,
    album: tags.album,
    trackNo: tags.track,
    durationMs: totalSamples * 1000 ~/ h.rate,
    durationEstimated: estimated,
    sampleRate: h.rate,
  );
}
