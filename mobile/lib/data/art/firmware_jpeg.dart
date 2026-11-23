// SPDX-License-Identifier: MPL-2.0

/// Whether the firmware's JPEG decoder takes a picture: `jd_prepare` of
/// TJpgDec R0.03 (`app/third_party/tjpgd/tjpgd.c`, configured by
/// `tjpgdcnf.h`: 512-byte stream buffer, `JD_FASTDECODE` 1) with the 4096-byte
/// pool `art_decode` gives it, and `art_decode`'s own size limit
/// (`app/audio/art.c`). A picture the device cannot show gives way to the next
/// source there (an embedded picture to the cover file), so the app checks the
/// same things before it decodes, or it would show a different picture from
/// the device's.
///
/// Checked here: the header, as `jd_prepare` reads it; the scaled size
/// `art_decode` works out from it (a side that scales to nothing is
/// `ART_E_FORMAT`); and that the compressed data runs to a marker before the
/// picture ends. TJpgDec (`JD_FASTDECODE` 1) always reads up to 16 bits
/// ahead and stops reading only once it meets a marker, so a picture cut
/// anywhere inside its compressed data, even with only the end marker
/// missing, fails there with `JDR_INP` (`ART_E_FORMAT`), while the app's
/// decoder would show what it has. Other damage inside the compressed data
/// is left to the app's decoder, which refuses it.
///
/// The scale is the one the hosted and PSRAM builds use (160 px thumbnails,
/// `NEUPOD_ART_HIRES`); the Pico 2 W's 64 px build is frozen.
library;

import 'dart:typed_data';

/// `art_err_t` for what [firmwareJpegCheck] can tell from the header.
enum FirmwareJpeg {
  /// The decoder would start on it.
  ok,

  /// `ART_E_FORMAT`: not a JPEG, or damaged / cut short.
  format,

  /// `ART_E_UNSUPPORTED`: progressive or another non-baseline JPEG, a layout
  /// TJpgDec does not decode, or tables bigger than its pool.
  unsupported,

  /// `ART_E_TOO_BIG`: wider or taller than `ART_MAX_SRC_DIM`.
  tooBig,
}

/// `ART_POOL_BYTES` (art.h).
const _poolBytes = 4096;

/// `JD_SZBUF` (tjpgdcnf.h).
const _szBuf = 512;

/// `ART_MAX_SRC_DIM` (art.h).
const firmwareMaxSourceDim = 5120;

/// `ART_MAX_DIM` of the builds with `NEUPOD_ART_HIRES` (art.h).
const _thumbBox = 160;

/// `jd_prepare` + `art_decode`'s size check over [jpeg], the picture's bytes
/// (the whole of them: the decoder reads no further).
FirmwareJpeg firmwareJpegCheck(Uint8List jpeg) => firmwareJpegInspect(jpeg).$1;

/// [firmwareJpegCheck], with the picture's width and height as its frame
/// header gives them (0 when it got no further).
(FirmwareJpeg, int, int) firmwareJpegInspect(Uint8List jpeg) {
  final p = _Prepare(jpeg);
  final r = p.run();
  return (r, p.width, p.height);
}

/// A copy of [jpeg] in which the APP1 (EXIF) and APP14 (Adobe) segments
/// before the scan are turned into comments. TJpgDec reads neither: it never
/// turns a picture by its EXIF orientation, and always takes three
/// components as YCbCr, whatever an Adobe segment says. The app's decoder
/// would honour both, so it is given the picture without them.
Uint8List withoutAppMarkersTJpgDecIgnores(Uint8List jpeg) {
  final out = Uint8List.fromList(jpeg);
  var i = 0;
  var marker = 0;
  for (; i < out.length; i++) {
    marker = ((marker << 8) | out[i]) & 0xFFFF;
    if (marker == 0xFFD8) break;
  }
  i++;
  while (i + 4 <= out.length && out[i] == 0xFF) {
    final m = out[i + 1];
    if (m == 0xDA || m == 0xD9) break;
    if (m == 0xE1 || m == 0xEE) out[i + 1] = 0xFE; // COM
    i += 2 + ((out[i + 2] << 8) | out[i + 3]);
  }
  return out;
}

final class _Prepare {
  _Prepare(this.d);

  final Uint8List d;
  int pos = 0;
  int pool = _poolBytes;

  int width = 0, height = 0, ncomp = 0, msx = 0, msy = 0;
  final qtid = List<int>.filled(3, 0);
  final qttbl = List<bool>.filled(4, false);

  /// `huffbits[num][cls]` loaded.
  final huff = [
    [false, false],
    [false, false],
  ];

  /// `alloc_pool`: false when the pool is spent (`JDR_MEM1`).
  bool alloc(int n) {
    n = (n + 3) & ~3;
    if (pool < n) return false;
    pool -= n;
    return true;
  }

  /// `infunc` reading [n] bytes, or null when the picture ends first
  /// (`JDR_INP`).
  Uint8List? take(int n) {
    if (pos + n > d.length) return null;
    final b = Uint8List.sublistView(d, pos, pos + n);
    pos += n;
    return b;
  }

  // art_decode maps JDR_FMT3, JDR_MEM1 and JDR_MEM2 to unsupported, anything
  // else (JDR_INP, JDR_FMT1) to format.
  static const _inp = FirmwareJpeg.format;
  static const _fmt1 = FirmwareJpeg.format;
  static const _fmt3 = FirmwareJpeg.unsupported;
  static const _mem = FirmwareJpeg.unsupported;

  FirmwareJpeg run() {
    if (!alloc(_szBuf)) return _mem; // the stream input buffer
    var marker = 0;
    do {
      final b = take(1);
      if (b == null) return _inp; // no SOI
      marker = ((marker << 8) | b[0]) & 0xFFFF;
    } while (marker != 0xFFD8);

    for (;;) {
      final h = take(4);
      if (h == null) return _inp;
      final m = (h[0] << 8) | h[1];
      var len = (h[2] << 8) | h[3];
      if (len <= 2 || (m >> 8) != 0xFF) return _fmt1;
      len -= 2;
      switch (m & 0xFF) {
        case 0xC0: // SOF0, baseline
          if (len > _szBuf) return _mem;
          final s = take(len);
          if (s == null) return _inp;
          if (s.length < 6) return _inp;
          height = (s[1] << 8) | s[2];
          width = (s[3] << 8) | s[4];
          ncomp = s[5];
          if (ncomp != 3 && ncomp != 1) return _fmt3;
          for (var i = 0; i < ncomp; i++) {
            if (8 + 3 * i >= s.length) return _inp;
            final b = s[7 + 3 * i];
            if (i == 0) {
              if (b != 0x11 && b != 0x22 && b != 0x21) return _fmt3;
              msx = b >> 4;
              msy = b & 15;
            } else if (b != 0x11) {
              return _fmt3;
            }
            qtid[i] = s[8 + 3 * i];
            if (qtid[i] > 3) return _fmt3;
          }
        case 0xDD: // DRI
          if (len > _szBuf) return _mem;
          if (take(len) == null) return _inp;
        case 0xC4: // DHT
          if (len > _szBuf) return _mem;
          final s = take(len);
          if (s == null) return _inp;
          final r = _huffman(s);
          if (r != null) return r;
        case 0xDB: // DQT
          if (len > _szBuf) return _mem;
          final s = take(len);
          if (s == null) return _inp;
          final r = _quant(s);
          if (r != null) return r;
        case 0xDA: // SOS
          if (len > _szBuf) return _mem;
          final s = take(len);
          if (s == null) return _inp;
          if (width == 0 || height == 0) return _fmt1;
          if (s.isEmpty || s[0] != ncomp) return _fmt3;
          for (var i = 0; i < ncomp; i++) {
            if (2 + 2 * i >= s.length) return _inp;
            final b = s[2 + 2 * i];
            if (b != 0x00 && b != 0x11) return _fmt3;
            final n = i == 0 ? 0 : 1;
            if (!huff[n][0] || !huff[n][1]) return _fmt1;
            if (!qttbl[qtid[i]]) return _fmt1;
          }
          final n = msy * msx;
          if (n == 0) return _fmt1;
          var work = n * 64 * 2 + 64;
          if (work < 256) work = 256;
          if (!alloc(work)) return _mem;
          if (!alloc((n + 2) * 64 * 2)) return _mem; // jd_yuv_t is int16_t
          // art_decode, once prepared.
          if (width > firmwareMaxSourceDim || height > firmwareMaxSourceDim) {
            return FirmwareJpeg.tooBig;
          }
          // The largest decoder shrink that keeps the long side at the
          // thumbnail's size or more; a side that scales to nothing is
          // ART_E_FORMAT (art.c).
          final m = width > height ? width : height;
          var scale = 0;
          while (scale < 3 && (m >> (scale + 1)) >= _thumbBox) {
            scale++;
          }
          final mx = msx * 8, my = msy * 8;
          final sw = ((width ~/ mx * mx) >> scale) + ((width % mx) >> scale);
          final sh = ((height ~/ my * my) >> scale) + ((height % my) >> scale);
          if (sw == 0 || sh == 0) return FirmwareJpeg.format;
          // The compressed data must reach a marker (FF then a byte that is
          // neither a stuffed 00 nor a restart marker) inside the picture.
          if (!_endsWithMarker(pos)) return _inp;
          return FirmwareJpeg.ok;
        case 0xC1 || 0xC2 || 0xC3 || 0xC5 || 0xC6 || 0xC7 || 0xC9 || 0xCA:
        case 0xCB || 0xCD || 0xCE || 0xCF || 0xD9:
          return _fmt3; // progressive and the rest
        default: // skipped without reading
          if (pos + len > d.length) return _inp;
          pos += len;
      }
    }
  }

  /// Whether a marker other than a restart marker follows the compressed data
  /// that starts at [from].
  bool _endsWithMarker(int from) {
    for (var i = from; i + 1 < d.length; i++) {
      if (d[i] != 0xFF) continue;
      final b = d[i + 1];
      // TJpgDec takes any other byte after FF as a marker and reads no
      // further (a restart marker is followed by more data it then reads).
      if (b == 0x00 || (b >= 0xD0 && b <= 0xD7)) continue;
      return true;
    }
    return false;
  }

  /// `create_huffman_tbl`.
  FirmwareJpeg? _huffman(Uint8List s) {
    var p = 0;
    var left = s.length;
    while (left > 0) {
      if (left < 17) return _fmt1;
      left -= 17;
      final d = s[p++];
      if ((d & 0xEE) != 0) return _fmt1;
      final cls = d >> 4, num = d & 0x0F;
      if (!alloc(16)) return _mem;
      huff[num][cls] = true;
      var np = 0;
      for (var i = 0; i < 16; i++) {
        np += s[p++];
      }
      if (!alloc(np * 2)) return _mem;
      if (left < np) return _fmt1;
      left -= np;
      if (!alloc(np)) return _mem;
      for (var i = 0; i < np; i++) {
        final v = s[p++];
        if (cls == 0 && v > 11) return _fmt1;
      }
    }
    return null;
  }

  /// `create_qt_tbl`.
  FirmwareJpeg? _quant(Uint8List s) {
    var p = 0;
    var left = s.length;
    while (left > 0) {
      if (left < 65) return _fmt1;
      left -= 65;
      final d = s[p++];
      if ((d & 0xF0) != 0) return _fmt1;
      if (!alloc(64 * 4)) return _mem;
      qttbl[d & 3] = true;
      p += 64;
    }
    return null;
  }
}
