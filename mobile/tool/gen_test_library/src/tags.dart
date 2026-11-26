// SPDX-License-Identifier: MPL-2.0

// Minimal tag writers for the generated test library: FLAC Vorbis comments
// (and an optional PICTURE block) and ID3v2.3 / ID3v2.4 for MP3. They only
// replace the metadata of the repository's small fixtures; the audio frames
// are copied unchanged. Not a general-purpose tagger.

import 'dart:convert';
import 'dart:typed_data';

/// Tag values for one file. Null fields are not written.
class TagSet {
  const TagSet({
    this.title,
    this.artist,
    this.albumArtist,
    this.album,
    this.track,
    this.disc,
    this.year,
    this.genre,
    this.compilation = false,
    this.artistSort,
  });

  final String? title;
  final String? artist;
  final String? albumArtist;
  final String? album;

  /// Written as given, e.g. `3` or `3/12`.
  final String? track;

  /// Written as given, e.g. `1` or `1/2`.
  final String? disc;
  final String? year;
  final String? genre;
  final bool compilation;
  final String? artistSort;

  Map<String, Object?> toJson() => {
    'title': title,
    'artist': artist,
    'albumArtist': albumArtist,
    'album': album,
    'track': track,
    'disc': disc,
    'year': year,
    'genre': genre,
    'compilation': compilation,
    'artistSort': artistSort,
  };
}

/// A FLAC file with its tags replaced. [tags] null writes no VORBIS_COMMENT
/// at all (the "missing tags" case). [pictureJpeg] adds a front-cover
/// PICTURE block. STREAMINFO, SEEKTABLE, APPLICATION and CUESHEET blocks are
/// kept; old VORBIS_COMMENT, PICTURE and PADDING blocks are dropped.
Uint8List flacWithTags(Uint8List flac, {TagSet? tags, Uint8List? pictureJpeg}) {
  if (flac.length < 8 || !_startsWith(flac, 'fLaC')) {
    throw const FormatException('not a FLAC file');
  }
  final kept = <(int, Uint8List)>[];
  var p = 4;
  while (true) {
    if (p + 4 > flac.length) throw const FormatException('truncated FLAC');
    final header = flac[p];
    final type = header & 0x7F;
    final len = (flac[p + 1] << 16) | (flac[p + 2] << 8) | flac[p + 3];
    final body = Uint8List.sublistView(flac, p + 4, p + 4 + len);
    if (type != 1 && type != 4 && type != 6) kept.add((type, body));
    p += 4 + len;
    if (header & 0x80 != 0) break;
  }
  final audio = Uint8List.sublistView(flac, p);
  if (tags != null) kept.add((4, vorbisComment(tags)));
  if (pictureJpeg != null) kept.add((6, flacPicture(pictureJpeg)));
  // STREAMINFO first, every other block in its original order. Dart's sort
  // is not stable, so the original index is part of the key.
  final indexed = [for (var i = 0; i < kept.length; i++) (i, kept[i])]
    ..sort((a, b) {
      final sa = a.$2.$1 == 0 ? 0 : 1, sb = b.$2.$1 == 0 ? 0 : 1;
      return sa != sb ? sa.compareTo(sb) : a.$1.compareTo(b.$1);
    });
  kept
    ..clear()
    ..addAll([for (final (_, block) in indexed) block]);

  final out = BytesBuilder(copy: false)..add(ascii.encode('fLaC'));
  for (var i = 0; i < kept.length; i++) {
    final (type, body) = kept[i];
    if (body.length >= 1 << 24) throw const FormatException('block too big');
    final last = i == kept.length - 1 ? 0x80 : 0;
    out
      ..addByte(last | type)
      ..addByte((body.length >> 16) & 0xFF)
      ..addByte((body.length >> 8) & 0xFF)
      ..addByte(body.length & 0xFF)
      ..add(body);
  }
  out.add(audio);
  return out.takeBytes();
}

/// The body of a VORBIS_COMMENT block (little-endian lengths, UTF-8).
Uint8List vorbisComment(TagSet t) {
  final comments = <String>[
    if (t.title != null) 'TITLE=${t.title}',
    if (t.artist != null) 'ARTIST=${t.artist}',
    if (t.albumArtist != null) 'ALBUMARTIST=${t.albumArtist}',
    if (t.album != null) 'ALBUM=${t.album}',
    if (t.track != null) 'TRACKNUMBER=${t.track}',
    if (t.disc != null) 'DISCNUMBER=${t.disc}',
    if (t.year != null) 'DATE=${t.year}',
    if (t.genre != null) 'GENRE=${t.genre}',
    if (t.compilation) 'COMPILATION=1',
    if (t.artistSort != null) 'ARTISTSORT=${t.artistSort}',
  ];
  final b = BytesBuilder(copy: false);
  void le32(int v) =>
      b.add(Uint8List(4)..buffer.asByteData().setUint32(0, v, Endian.little));
  void str(String s) {
    final u = utf8.encode(s);
    le32(u.length);
    b.add(u);
  }

  str('neuPod gen_test_library');
  le32(comments.length);
  comments.forEach(str);
  return b.takeBytes();
}

/// The body of a PICTURE block: front cover, image/jpeg, no description;
/// width, height, depth and colours left 0 (allowed: "unknown").
Uint8List flacPicture(Uint8List jpeg) {
  final b = BytesBuilder(copy: false);
  void be32(int v) =>
      b.add(Uint8List(4)..buffer.asByteData().setUint32(0, v, Endian.big));
  final mime = ascii.encode('image/jpeg');
  be32(3); // front cover
  be32(mime.length);
  b.add(mime);
  be32(0); // description length
  be32(0);
  be32(0);
  be32(0);
  be32(0);
  be32(jpeg.length);
  b.add(jpeg);
  return b.takeBytes();
}

/// An MP3 with any ID3v2 tag at the start and ID3v1 tag at the end removed,
/// and, unless [tags] is null, a new ID3v2 tag of [version] 3 or 4 in front.
/// v2.4 text is UTF-8 with TDRC; v2.3 text is UTF-16 with a BOM and TYER.
Uint8List mp3WithTags(Uint8List mp3, {TagSet? tags, int version = 4}) {
  var start = 0;
  if (mp3.length >= 10 && _startsWith(mp3, 'ID3')) {
    final size = _syncsafe(mp3, 6);
    final footer = mp3[5] & 0x10 != 0 ? 10 : 0;
    start = 10 + size + footer;
  }
  var end = mp3.length;
  if (end - start >= 128 &&
      mp3[end - 128] == 0x54 &&
      mp3[end - 127] == 0x41 &&
      mp3[end - 126] == 0x47) {
    end -= 128;
  }
  final audio = Uint8List.sublistView(mp3, start, end);
  if (tags == null) return Uint8List.fromList(audio);
  final out = BytesBuilder(copy: false)
    ..add(id3v2(tags, version: version))
    ..add(audio);
  return out.takeBytes();
}

/// An ID3v2.3 or v2.4 tag holding [t]'s text frames (no padding).
Uint8List id3v2(TagSet t, {int version = 4}) {
  if (version != 3 && version != 4) throw ArgumentError.value(version);
  final frames = <(String, String)>[
    if (t.title != null) ('TIT2', t.title!),
    if (t.artist != null) ('TPE1', t.artist!),
    if (t.albumArtist != null) ('TPE2', t.albumArtist!),
    if (t.album != null) ('TALB', t.album!),
    if (t.track != null) ('TRCK', t.track!),
    if (t.disc != null) ('TPOS', t.disc!),
    if (t.year != null) (version == 4 ? 'TDRC' : 'TYER', t.year!),
    if (t.genre != null) ('TCON', t.genre!),
    if (t.compilation) ('TCMP', '1'),
    if (t.artistSort != null) ('TSOP', t.artistSort!),
  ];
  final body = BytesBuilder(copy: false);
  for (final (id, text) in frames) {
    final data = version == 4
        ? [3, ...utf8.encode(text)]
        : [1, 0xFF, 0xFE, ..._utf16le(text)];
    body
      ..add(ascii.encode(id))
      ..add(version == 4 ? _toSyncsafe(data.length) : _be32(data.length))
      ..add([0, 0])
      ..add(data);
  }
  final bytes = body.takeBytes();
  return (BytesBuilder(copy: false)
        ..add(ascii.encode('ID3'))
        ..add([version, 0, 0])
        ..add(_toSyncsafe(bytes.length))
        ..add(bytes))
      .takeBytes();
}

/// An MP4 (M4A) file with its iTunes tags replaced: the `udta` box of `moov`
/// is dropped and, unless [tags] is null, a new `udta` / `meta` / `ilst`
/// holding [tags] is added at the end of `moov`. When `moov` comes before
/// `mdat`, the chunk offsets (`stco`, `co64`) are moved by the change in
/// size, so the audio is found where it now is. Every other box is copied
/// unchanged.
Uint8List m4aWithTags(Uint8List m4a, {TagSet? tags}) {
  final top = _boxes(m4a, 0, m4a.length);
  final moov = top.where((b) => b.type == 'moov').firstOrNull;
  if (moov == null) throw const FormatException('no moov box');
  final kept = BytesBuilder(copy: false);
  for (final c in _boxes(m4a, moov.start + 8, moov.end)) {
    if (c.type != 'udta') kept.add(Uint8List.sublistView(m4a, c.start, c.end));
  }
  if (tags != null) kept.add(_udta(tags));
  final body = kept.takeBytes();
  final newMoov = Uint8List(8 + body.length)
    ..setAll(0, _be32(8 + body.length))
    ..setAll(4, ascii.encode('moov'))
    ..setAll(8, body);
  final delta = newMoov.length - (moov.end - moov.start);
  final mdatAfter = top.any((b) => b.type == 'mdat' && b.start > moov.start);
  if (mdatAfter && delta != 0) {
    _shiftChunkOffsets(newMoov, 8, newMoov.length, delta);
  }
  final out = BytesBuilder(copy: false);
  for (final b in top) {
    out.add(
      identical(b, moov) ? newMoov : Uint8List.sublistView(m4a, b.start, b.end),
    );
  }
  return out.takeBytes();
}

typedef _Box = ({String type, int start, int end});

/// The boxes from [start] to [end] (32-bit sizes only, as the fixtures have).
List<_Box> _boxes(Uint8List b, int start, int end) {
  final out = <_Box>[];
  final d = ByteData.sublistView(b);
  var p = start;
  while (p + 8 <= end) {
    final size = d.getUint32(p);
    if (size < 8 || p + size > end) {
      throw FormatException('bad MP4 box at $p');
    }
    out.add((
      type: String.fromCharCodes(b.sublist(p + 4, p + 8)),
      start: p,
      end: p + size,
    ));
    p += size;
  }
  return out;
}

/// Adds [delta] to every chunk offset in the boxes from [start] to [end] of
/// [moov] and the containers within them (in place).
void _shiftChunkOffsets(Uint8List moov, int start, int end, int delta) {
  final d = ByteData.sublistView(moov);
  for (final c in _boxes(moov, start, end)) {
    switch (c.type) {
      case 'trak' || 'mdia' || 'minf' || 'stbl':
        _shiftChunkOffsets(moov, c.start + 8, c.end, delta);
      case 'stco':
        final n = d.getUint32(c.start + 12);
        for (var i = 0; i < n; i++) {
          final at = c.start + 16 + i * 4;
          d.setUint32(at, d.getUint32(at) + delta);
        }
      case 'co64':
        final n = d.getUint32(c.start + 12);
        for (var i = 0; i < n; i++) {
          final at = c.start + 16 + i * 8;
          d.setUint64(at, d.getUint64(at) + delta);
        }
    }
  }
}

Uint8List _box(String type, List<int> body) => Uint8List.fromList([
  ..._be32(8 + body.length),
  ...ascii.encode(type),
  ...body,
]);

/// `©nam` and the like: the copyright sign is byte 0xA9 in a box type.
Uint8List _boxA9(String three, List<int> body) => Uint8List.fromList([
  ..._be32(8 + body.length),
  0xA9,
  ...ascii.encode(three),
  ...body,
]);

/// A `data` box: [type] 1 is UTF-8 text, 0 binary, 21 an integer.
Uint8List _data(int type, List<int> value) =>
    _box('data', [0, 0, 0, type, 0, 0, 0, 0, ...value]);

/// The leading number of `3` or `3/12`, and the total after a slash, as
/// 16-bit fields.
List<int> _pair(String text) {
  final parts = text.split('/');
  final n = int.tryParse(parts.first.trim()) ?? 0;
  final total = parts.length > 1 ? int.tryParse(parts[1].trim()) ?? 0 : 0;
  return [(n >> 8) & 0xFF, n & 0xFF, (total >> 8) & 0xFF, total & 0xFF];
}

/// `udta` / `meta` (with its `mdir` handler) / `ilst` holding [t].
Uint8List _udta(TagSet t) {
  List<int> text(String s) => _data(1, utf8.encode(s));
  final items = <Uint8List>[
    if (t.title != null) _boxA9('nam', text(t.title!)),
    if (t.artist != null) _boxA9('ART', text(t.artist!)),
    if (t.albumArtist != null) _box('aART', text(t.albumArtist!)),
    if (t.album != null) _boxA9('alb', text(t.album!)),
    if (t.track != null)
      _box('trkn', _data(0, [0, 0, ..._pair(t.track!), 0, 0])),
    if (t.disc != null) _box('disk', _data(0, [0, 0, ..._pair(t.disc!)])),
    if (t.year != null) _boxA9('day', text(t.year!)),
    if (t.genre != null) _boxA9('gen', text(t.genre!)),
    if (t.compilation) _box('cpil', _data(21, [1])),
    if (t.artistSort != null) _box('soar', text(t.artistSort!)),
  ];
  final hdlr = _box('hdlr', [
    ...[0, 0, 0, 0], // version and flags
    ...[0, 0, 0, 0], // pre-defined
    ...ascii.encode('mdir'),
    ...ascii.encode('appl'),
    ...List.filled(9, 0), // reserved, and an empty name
  ]);
  final ilst = _box('ilst', [for (final i in items) ...i]);
  final meta = _box('meta', [0, 0, 0, 0, ...hdlr, ...ilst]);
  return _box('udta', meta);
}

List<int> _utf16le(String s) {
  final out = <int>[];
  for (final unit in s.codeUnits) {
    out
      ..add(unit & 0xFF)
      ..add(unit >> 8);
  }
  return out;
}

int _syncsafe(Uint8List b, int at) =>
    (b[at] << 21) | (b[at + 1] << 14) | (b[at + 2] << 7) | b[at + 3];

List<int> _toSyncsafe(int v) {
  if (v >= 1 << 28) throw ArgumentError.value(v, 'v', 'too big for syncsafe');
  return [(v >> 21) & 0x7F, (v >> 14) & 0x7F, (v >> 7) & 0x7F, v & 0x7F];
}

List<int> _be32(int v) => [
  (v >> 24) & 0xFF,
  (v >> 16) & 0xFF,
  (v >> 8) & 0xFF,
  v & 0xFF,
];

bool _startsWith(Uint8List b, String magic) {
  if (b.length < magic.length) return false;
  for (var i = 0; i < magic.length; i++) {
    if (b[i] != magic.codeUnitAt(i)) return false;
  }
  return true;
}
