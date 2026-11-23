// SPDX-License-Identifier: MPL-2.0

/// Tag reader (decision 0003): FLAC, MP3 and MP4 tags and duration, and WAV
/// duration, ported from the firmware's `app/audio/meta.c`, `mp4.c` and
/// `wav.c` (`flac_parse`, `vorbis_comments`, `mp4_parse`, `wav_parse`,
/// `vorbis_field`, `id3v2_tags`, `id3_text_frame`, `mp3_parse`,
/// `meta_text_to_utf8`, `meta_genre_text`, `meta_year_text`), read through
/// [ByteSource], with the library tags the firmware's scanner asks for
/// (`audio_tags_ext_t`, set by `meta_set_tags_ext` in lib_build.c): album
/// artist, genre, disc, year, compilation flag and the sort tags.
///
/// Texts come twice: as displayed (the whole tag, decoded) and as the
/// firmware's parser stores them (`...KeyText`: its buffer sizes, its
/// decoding), which is what the library's keys and grouping are built from
/// (owner decision 11). Numbers and the flag are the firmware's only: what
/// the device reads is what orders an album and groups it.
///
/// Every length read from a file is bounded by the file size and a cap, and
/// a damaged file gives partial tags or a [TagReadException], never a hang or
/// unbounded memory.
library;

import 'dart:convert';
import 'dart:typed_data';

import 'package:neupod/domain/library/collation.dart';
import 'package:neupod/domain/library/track.dart';

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
  const TagReadException(this.reason, this.detail, {this.partial});

  /// The tags read before the failure, or null. The firmware keeps what its
  /// parser had filled in when a parse fails part-way (`index_file` in
  /// lib_build.c takes the tags whatever the parse returned), so the
  /// library lists the file by them, with the reason.
  final AudioTags? partial;

  /// This failure, carrying [tags] as what was read.
  TagReadException withPartial(AudioTags tags) =>
      TagReadException(reason, detail, partial: tags);

  /// Stored on the track as a code; the UI words it.
  final UnreadableReason reason;

  /// For logs and tests only: "file is cut short".
  final String detail;

  @override
  String toString() => 'TagReadException(${reason.name}: $detail)';
}

/// What phase 1 reads from a file. Empty strings and zeros mean "not
/// tagged".
final class AudioTags {
  const AudioTags({
    required this.codec,
    this.title = '',
    this.titleSort = '',
    this.titleKeyText = const [],
    this.titleSortKeyText = const [],
    this.artist = '',
    this.album = '',
    this.artistKeyText = const [],
    this.albumKeyText = const [],
    this.albumArtist = '',
    this.albumArtistKeyText = const [],
    this.artistSortKeyText = const [],
    this.albumSortKeyText = const [],
    this.albumArtistSortKeyText = const [],
    this.genre = '',
    this.genreKeyText = const [],
    this.trackNo = 0,
    this.discNo = 0,
    this.year = 0,
    this.compilation = false,
    this.durationMs = 0,
    this.durationEstimated = false,
    this.sampleRate = 0,
    this.bitDepth = 0,
  });

  /// `flac`, `mp3`, `alac`, `aac`, `mp4` (another codec in an MP4) or
  /// `wav`.
  final String codec;

  /// The title as displayed: the whole tag, decoded.
  final String title;

  /// The title sort tag (TSOT/TST, TITLESORT), or ''.
  final String titleSort;

  /// The title exactly as the firmware's parser stores it, for the sort key
  /// (owner decision 11): `meta_text_to_utf8` into a 64-byte buffer, so at
  /// most 63 bytes, UTF-16 surrogates as `?`, malformed UTF-8 copied raw, and
  /// empty for a FLAC whose comment block is over the scanner's 4096-byte
  /// scratch.
  final List<int> titleKeyText;

  /// The title sort tag as the firmware stores it (32-byte buffer).
  final List<int> titleSortKeyText;

  /// The artist and album as displayed: the whole tag, decoded.
  final String artist;
  final String album;

  /// The artist and album tags as the firmware stores them (`char
  /// artist[64]`, `album[64]`), the way [titleKeyText] is: the text the
  /// artist and album keys and the compilation test are built from.
  final List<int> artistKeyText;
  final List<int> albumKeyText;

  /// The album artist (ALBUMARTIST / "ALBUM ARTIST", TPE2 / TP2) as
  /// displayed, and as the firmware stores it (`char album_artist[64]`).
  final String albumArtist;
  final List<int> albumArtistKeyText;

  /// The sort tags the library keys on, as the firmware stores them (`char
  /// sort_artist[32]` and so on): ARTISTSORT / TSOP / TSP, ALBUMSORT / TSOA /
  /// TSA, ALBUMARTISTSORT / TSO2 / TS2.
  final List<int> artistSortKeyText;
  final List<int> albumSortKeyText;
  final List<int> albumArtistSortKeyText;

  /// The genre (GENRE, TCON / TCO) with ID3v1 `(n)` references resolved as
  /// `meta_genre_text` does: as displayed, and as the firmware stores it
  /// (`char genre[32]`).
  final String genre;
  final List<int> genreKeyText;

  /// The firmware's track number (`track_from`: the leading digits, so
  /// "3/12" is 3; 0 when none).
  final int trackNo;

  /// The firmware's disc number (DISCNUMBER / DISC, TPOS / TPA), as
  /// [trackNo]; 0 when none.
  final int discNo;

  /// The firmware's year (`meta_year_text`: the first four characters when
  /// they are digits, so "2019-05-04" is 2019; 0 otherwise): DATE / YEAR,
  /// TDRC / TYER / TYE, the later one in the file standing.
  final int year;

  /// The firmware's compilation flag (`flag_from`: COMPILATION / TCMP / TCP
  /// starting with `1`, `y`, `Y`, `t` or `T`).
  final bool compilation;
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

/// The firmware library scanner's comment scratch (`TAG_SCRATCH` in
/// lib_build.c): a larger FLAC comment block is skipped there, so its title
/// and sort tag do not count for the sort key.
const _firmwareCommentScratch = 4096;

/// The firmware's tag buffers: `title[64]`, `sort_title[32]`, `artist[64]`,
/// `album[64]`, `album_artist[64]`, `genre[32]` (meta.h), the genre's
/// decoding buffer `g[48]` and the number buffers of `id3_text_frame` and
/// `vorbis_field` (meta.c).
const _fwTitleCap = 64;
const _fwSortCap = 32;
const _fwArtistCap = 64;
const _fwAlbumCap = 64;
const _fwAlbumArtistCap = 64;
const _fwGenreCap = 32;
const _fwGenreRaw = 48;
const _fwId3Number = 16;
const _fwId3Flag = 8;
const _fwVorbisNumber = 11;

/// Reads the tags of [src], choosing the parser by [extension] (`flac`,
/// `mp3`, `m4a`, `mp4`, `wav`; any case), as the firmware's scanner does
/// (`index_file` in lib_build.c).
AudioTags readTags(ByteSource src, String extension) =>
    switch (extension.toLowerCase()) {
      'flac' => _readFlac(src),
      'mp3' => _readMp3(src),
      'm4a' || 'mp4' => _readMp4(src),
      'wav' => _readWav(src),
      _ => throw TagReadException(
        UnreadableReason.wrongFormat,
        '$extension tags are not read yet',
      ),
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
  if (b.length != count) {
    throw const TagReadException(UnreadableReason.damaged, 'file is cut short');
  }
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

/// Caps a text at [_maxText] UTF-16 units without splitting a pair. Not
/// trimmed: the firmware keeps leading and trailing spaces, and they sort.
String _cut(String t) {
  if (t.length <= _maxText) return t;
  var end = _maxText;
  if (end > 0 && (t.codeUnitAt(end - 1) & 0xFC00) == 0xD800) end--;
  return t.substring(0, end);
}

/// `track_from` (meta.c): the leading digits ("3/12" is 3), capped as the
/// firmware caps it.
int firmwareNumber(List<int> s) {
  var v = 0;
  for (final c in s) {
    if (c < 0x30 || c > 0x39 || v >= 10000) break;
    v = v * 10 + (c - 0x30);
  }
  return v > 0xFFFF ? 0xFFFF : v;
}

/// `meta_year_text` (meta.c): the first four characters when they are all
/// digits, else 0.
int firmwareYear(List<int> s) {
  if (s.length < 4) return 0;
  var v = 0;
  for (var i = 0; i < 4; i++) {
    if (s[i] < 0x30 || s[i] > 0x39) return 0;
    v = v * 10 + (s[i] - 0x30);
  }
  return v;
}

/// `flag_from` (meta.c): true when the text starts with `1`, `y`, `Y`, `t`
/// or `T`.
bool firmwareFlag(List<int> s) =>
    s.isNotEmpty &&
    (s[0] == 0x31 ||
        s[0] == 0x79 ||
        s[0] == 0x59 ||
        s[0] == 0x74 ||
        s[0] == 0x54);

/// The ID3v1 genres, in number order (`k_genres` in meta.c: the 80 standard
/// ones, then the Winamp extensions).
const _genres = [
  'Blues', 'Classic Rock', 'Country', 'Dance', 'Disco', 'Funk', 'Grunge', //
  'Hip-Hop', 'Jazz', 'Metal', 'New Age', 'Oldies', 'Other', 'Pop', 'R&B',
  'Rap', 'Reggae', 'Rock', 'Techno', 'Industrial', 'Alternative', 'Ska',
  'Death Metal', 'Pranks', 'Soundtrack', 'Euro-Techno', 'Ambient',
  'Trip-Hop', 'Vocal', 'Jazz+Funk', 'Fusion', 'Trance', 'Classical',
  'Instrumental', 'Acid', 'House', 'Game', 'Sound Clip', 'Gospel', 'Noise',
  'AlternRock', 'Bass', 'Soul', 'Punk', 'Space', 'Meditative',
  'Instrumental Pop', 'Instrumental Rock', 'Ethnic', 'Gothic', 'Darkwave',
  'Techno-Industrial', 'Electronic', 'Pop-Folk', 'Eurodance', 'Dream',
  'Southern Rock', 'Comedy', 'Cult', 'Gangsta', 'Top 40', 'Christian Rap',
  'Pop/Funk', 'Jungle', 'Native American', 'Cabaret', 'New Wave',
  'Psychedelic', 'Rave', 'Showtunes', 'Trailer', 'Lo-Fi', 'Tribal',
  'Acid Punk', 'Acid Jazz', 'Polka', 'Retro', 'Musical', 'Rock & Roll',
  'Hard Rock',
  // 80.. Winamp
  'Folk', 'Folk-Rock', 'National Folk', 'Swing', 'Fast Fusion', 'Bebop',
  'Latin', 'Revival', 'Celtic', 'Bluegrass', 'Avantgarde', 'Gothic Rock',
  'Progressive Rock', 'Psychedelic Rock', 'Symphonic Rock', 'Slow Rock',
  'Big Band', 'Chorus', 'Easy Listening', 'Acoustic', 'Humour', 'Speech',
  'Chanson', 'Opera', 'Chamber Music', 'Sonata', 'Symphony', 'Booty Bass',
  'Primus', 'Porn Groove', 'Satire', 'Slow Jam', 'Club', 'Tango', 'Samba',
  'Folklore', 'Ballad', 'Power Ballad', 'Rhythmic Soul', 'Freestyle', 'Duet',
  'Punk Rock', 'Drum Solo', 'A Cappella', 'Euro-House', 'Dance Hall', 'Goa',
  'Drum & Bass', 'Club-House', 'Hardcore Techno', 'Terror', 'Indie',
  'BritPop', 'Afro-Punk', 'Polsk Punk', 'Beat', 'Christian Gangsta Rap',
  'Heavy Metal', 'Black Metal', 'Crossover', 'Contemporary Christian',
  'Christian Rock', 'Merengue', 'Salsa', 'Thrash Metal', 'Anime', 'JPop',
  'Synthpop',
];

/// Port of `meta_genre_text` (meta.c) over the bytes of a C string [src]:
/// `((` is a literal `(`; `(n)` is ID3v1 genre n, unless text follows it
/// (then the text stands; a second `(m)` after it does not count); `(RX)`
/// and `(CR)` (remix, cover) are not genres; with [bareNumberOk] (ID3 TCON)
/// a bare number is a genre too. The result is cut to [cap] - 1 bytes at a
/// character boundary.
List<int> firmwareGenre(List<int> src, int cap, {required bool bareNumberOk}) {
  if (cap == 0) return const [];
  final nul = src.indexOf(0);
  final whole = nul < 0 ? src : src.sublist(0, nul);
  int at(int i) => i < whole.length ? whole[i] : 0;
  // `genre_ref`: digits from [from] (while under 1000): the name, or null
  // for no digits or a number past the table, and where the digits end.
  (List<int>?, int) ref(int from) {
    var v = 0, p = from;
    while (at(p) >= 0x30 && at(p) <= 0x39 && v < 1000) {
      v = v * 10 + (at(p++) - 0x30);
    }
    if (p == from || v >= _genres.length) return (null, p);
    return (ascii.encode(_genres[v]), p);
  }

  bool starts(String s) {
    for (var i = 0; i < s.length; i++) {
      if (at(i) != s.codeUnitAt(i)) return false;
    }
    return true;
  }

  var out = whole;
  if (at(0) == 0x28 && at(1) == 0x28) {
    out = whole.sublist(1);
  } else if (at(0) == 0x28) {
    var (g, e) = ref(1);
    final rx = starts('(RX)') || starts('(CR)');
    if (rx) e = 3;
    if (rx || (e != 1 && at(e) == 0x29)) {
      final rest = e + 1;
      final more = at(rest) == 0x28 && at(rest + 1) != 0x28;
      out = at(rest) != 0 && !more
          ? whole.sublist(rest)
          : (g != null && !rx ? g : const <int>[]);
    }
  } else if (bareNumberOk) {
    final (g, e) = ref(0);
    if (g != null && at(e) == 0) out = g;
  }
  return firmwareCut(out, cap);
}

/// The displayed genre: [firmwareGenre] over the whole decoded text.
String _genreText(String text, {required bool bareNumberOk}) => utf8.decode(
  firmwareGenre(utf8.encode(text), 1 << 30, bareNumberOk: bareNumberOk),
  allowMalformed: true,
);

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
  var pos = _id3v2Size(src.readAt(0, 10)); // some files carry ID3v2 in front
  if (!_startsWith(src.readAt(pos, 4), 0, 'fLaC')) {
    throw const TagReadException(
      UnreadableReason.wrongFormat,
      'not a FLAC file',
    );
  }
  pos += 4;
  var rate = 0, bits = 0, totalFrames = 0;
  var haveInfo = false;
  final fields = _Fields();
  AudioTags tags() => fields.tags(
    codec: 'flac',
    durationMs: rate == 0 ? 0 : totalFrames * 1000 ~/ rate,
    sampleRate: rate,
    bitDepth: bits,
  );
  try {
    _flacBlocks(src, pos, fields, (r, b, f) {
      rate = r;
      bits = b;
      totalFrames = f;
      haveInfo = true;
    });
  } on TagReadException catch (e) {
    throw e.withPartial(tags());
  }
  if (!haveInfo) {
    throw TagReadException(_damaged, 'no STREAMINFO', partial: tags());
  }
  return tags();
}

/// `flac_parse`'s walk of the metadata blocks from [pos]: STREAMINFO goes to
/// [info], the Vorbis comments into [fields].
void _flacBlocks(
  ByteSource src,
  int pos,
  _Fields fields,
  void Function(int rate, int bits, int totalFrames) info,
) {
  final size = src.length;
  for (var guard = 0; guard < 256; guard++) {
    final bh = _need(src, pos, 4);
    final last = (bh[0] & 0x80) != 0;
    final type = bh[0] & 0x7F;
    final len = (bh[1] << 16) | (bh[2] << 8) | bh[3];
    final body = pos + 4;
    if (type == 0) {
      if (len < 34) throw const TagReadException(_damaged, 'STREAMINFO short');
      final si = _need(src, body, 34);
      info(
        (si[10] << 12) | (si[11] << 4) | (si[12] >> 4),
        (((si[12] & 1) << 4) | (si[13] >> 4)) + 1,
        ((si[13] & 0xF) << 32) | _be32(si, 14),
      );
    } else if (type == 4 && len <= _maxCommentBlock && body + len <= size) {
      _vorbisComments(
        src.readAt(body, len),
        fields,
        forKey: len <= _firmwareCommentScratch,
      );
    }
    pos = body + len;
    if (last) break;
    if (pos >= size) {
      throw const TagReadException(_damaged, 'blocks run past the end');
    }
  }
}

const _damaged = UnreadableReason.damaged;

/// What a tag parser fills: the fields of `audio_tags_t` and
/// `audio_tags_ext_t`.
class _Fields {
  String title = '', titleSort = '', artist = '', album = '';
  String albumArtist = '', genre = '';
  List<int> titleKey = const [], titleSortKey = const [];
  List<int> artistKey = const [], albumKey = const [];
  List<int> albumArtistKey = const [], genreKey = const [];
  List<int> artistSortKey = const [], albumSortKey = const [];
  List<int> albumArtistSortKey = const [];
  int track = 0, disc = 0, year = 0;
  bool compilation = false;

  AudioTags tags({
    required String codec,
    required int durationMs,
    bool durationEstimated = false,
    required int sampleRate,
    int bitDepth = 0,
  }) => AudioTags(
    codec: codec,
    title: title,
    titleSort: titleSort,
    titleKeyText: titleKey,
    titleSortKeyText: titleSortKey,
    artist: artist,
    album: album,
    artistKeyText: artistKey,
    albumKeyText: albumKey,
    albumArtist: albumArtist,
    albumArtistKeyText: albumArtistKey,
    artistSortKeyText: artistSortKey,
    albumSortKeyText: albumSortKey,
    albumArtistSortKeyText: albumArtistSortKey,
    genre: genre,
    genreKeyText: genreKey,
    trackNo: track,
    discNo: disc,
    year: year,
    compilation: compilation,
    durationMs: durationMs,
    durationEstimated: durationEstimated,
    sampleRate: sampleRate,
    bitDepth: bitDepth,
  );
}

/// `vorbis_comments` and `vorbis_field`. A later comment for the same field
/// replaces an earlier one, as in the firmware (so "ALBUM ARTIST" and
/// ALBUMARTIST, DISC and DISCNUMBER, YEAR and DATE: the last one stands,
/// even when its number does not parse). [forKey] is false for a block the
/// firmware skips (over its scratch): the displayed texts are still read,
/// the firmware's texts, numbers and flag are not.
void _vorbisComments(Uint8List b, _Fields t, {required bool forKey}) {
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
      // Numbers and flags: only the head matters (`tmp[12]`: the first 11
      // bytes, as a C string).
      List<int> head() {
        final h = value.length < _fwVorbisNumber
            ? value
            : Uint8List.sublistView(value, 0, _fwVorbisNumber);
        final nul = h.indexOf(0);
        return nul < 0 ? h : h.sublist(0, nul);
      }

      switch (key) {
        case 'TITLE':
          t.title = _text(value, 3);
          if (forKey) t.titleKey = firmwareText(value, 3, _fwTitleCap);
        case 'TITLESORT':
          t.titleSort = _text(value, 3);
          if (forKey) t.titleSortKey = firmwareText(value, 3, _fwSortCap);
        case 'ARTIST':
          t.artist = _text(value, 3);
          if (forKey) t.artistKey = firmwareText(value, 3, _fwArtistCap);
        case 'ALBUM':
          t.album = _text(value, 3);
          if (forKey) t.albumKey = firmwareText(value, 3, _fwAlbumCap);
        case 'TRACKNUMBER':
          if (forKey) t.track = firmwareNumber(head());
        case 'ALBUMARTIST' || 'ALBUM ARTIST':
          t.albumArtist = _text(value, 3);
          if (forKey) {
            t.albumArtistKey = firmwareText(value, 3, _fwAlbumArtistCap);
          }
        case 'GENRE':
          t.genre = _genreText(_text(value, 3), bareNumberOk: false);
          if (forKey) {
            t.genreKey = firmwareGenre(
              firmwareText(value, 3, _fwGenreRaw),
              _fwGenreCap,
              bareNumberOk: false,
            );
          }
        case 'DISCNUMBER' || 'DISC':
          if (forKey) t.disc = firmwareNumber(head());
        case 'DATE' || 'YEAR':
          if (forKey) t.year = firmwareYear(head());
        case 'COMPILATION':
          if (forKey) t.compilation = firmwareFlag(head());
        case 'ARTISTSORT':
          if (forKey) t.artistSortKey = firmwareText(value, 3, _fwSortCap);
        case 'ALBUMSORT':
          if (forKey) t.albumSortKey = firmwareText(value, 3, _fwSortCap);
        case 'ALBUMARTISTSORT':
          if (forKey) {
            t.albumArtistSortKey = firmwareText(value, 3, _fwSortCap);
          }
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

/// `id3v2_tags`: the text frames (pictures come with the art cache).
void _id3v2Tags(ByteSource src, Uint8List h, int total, _Fields t) {
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

/// `id3_text_frame`: v2.3/2.4 four-letter ids and their v2.2 three-letter
/// twins; a later frame for the same field replaces an earlier one.
void _id3TextFrame(String id, Uint8List d, _Fields t) {
  if (d.length < 2) return;
  final enc = d[0] > 3 ? 0 : d[0];
  final s = Uint8List.sublistView(d, 1);
  switch (id) {
    case 'TIT2' || 'TT2':
      t.title = _text(s, enc);
      t.titleKey = firmwareText(s, enc, _fwTitleCap);
    case 'TSOT' || 'TST':
      t.titleSort = _text(s, enc);
      t.titleSortKey = firmwareText(s, enc, _fwSortCap);
    case 'TPE1' || 'TP1':
      t.artist = _text(s, enc);
      t.artistKey = firmwareText(s, enc, _fwArtistCap);
    case 'TALB' || 'TAL':
      t.album = _text(s, enc);
      t.albumKey = firmwareText(s, enc, _fwAlbumCap);
    case 'TPE2' || 'TP2':
      t.albumArtist = _text(s, enc);
      t.albumArtistKey = firmwareText(s, enc, _fwAlbumArtistCap);
    case 'TSOP' || 'TSP':
      t.artistSortKey = firmwareText(s, enc, _fwSortCap);
    case 'TSOA' || 'TSA':
      t.albumSortKey = firmwareText(s, enc, _fwSortCap);
    case 'TSO2' || 'TS2':
      t.albumArtistSortKey = firmwareText(s, enc, _fwSortCap);
    case 'TCON' || 'TCO':
      t.genre = _genreText(_text(s, enc), bareNumberOk: true);
      t.genreKey = firmwareGenre(
        firmwareText(s, enc, _fwGenreRaw),
        _fwGenreCap,
        bareNumberOk: true,
      );
    case 'TRCK' || 'TRK':
      t.track = firmwareNumber(firmwareText(s, enc, _fwId3Number));
    case 'TPOS' || 'TPA':
      t.disc = firmwareNumber(firmwareText(s, enc, _fwId3Number));
    case 'TDRC' || 'TYER' || 'TYE':
      t.year = firmwareYear(firmwareText(s, enc, _fwId3Number));
    case 'TCMP' || 'TCP':
      t.compilation = firmwareFlag(firmwareText(s, enc, _fwId3Flag));
  }
}

/// `mp3_parse`: tags, then the first frame header confirmed by the next one,
/// then the duration from Xing/Info (minus the LAME delay and padding, as
/// dr_mp3 delivers it), VBRI, or the bitrate (estimated).
AudioTags _readMp3(ByteSource src) {
  final size = src.length;
  final h10 = src.readAt(0, 10);
  final start = _id3v2Size(h10);
  final tags = _Fields();
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
  if (h == null) {
    // The ID3 tag was read: the device lists the file by it, as damaged
    // (`mp3_parse` returns META_E_FORMAT, which `play_from_meta` in
    // lib_build.c maps to LIB_PLAY_DAMAGED).
    throw TagReadException(
      _damaged,
      'no MPEG audio frame',
      partial: tags.tags(codec: 'mp3', durationMs: 0, sampleRate: 0),
    );
  }

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
  // exact and there is no LAME tag. `vbri_parse`: the fixed part must be
  // there and sane.
  if (frames == 0) frames = _vbriFrames(fr, got);
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
  return tags.tags(
    codec: 'mp3',
    durationMs: totalSamples * 1000 ~/ h.rate,
    durationEstimated: estimated,
    sampleRate: h.rate,
  );
}

/// `vbri_parse`: the VBRI frame count, or 0 when the header is absent, short
/// (under 62 bytes from the frame start) or not sane.
int _vbriFrames(Uint8List fr, int got) {
  const fixed = 62; // VBRI_FIXED: from the frame start to the table
  if (got < fixed || !_startsWith(fr, 36, 'VBRI')) return 0;
  final frames = _be32(fr, 50);
  final entries = (fr[54] << 8) | fr[55];
  final scale = (fr[56] << 8) | fr[57];
  final entryBytes = (fr[58] << 8) | fr[59];
  final framesPerEntry = (fr[60] << 8) | fr[61];
  final sane =
      frames != 0 &&
      entries != 0 &&
      scale != 0 &&
      framesPerEntry != 0 &&
      entryBytes >= 1 &&
      entryBytes <= 4;
  return sane ? frames : 0;
}

/// Port of the firmware's `meta_text_to_utf8()` (app/audio/meta.c): the text
/// as the device stores it, for the sort key. [enc] 0 Latin-1, 1 UTF-16 with
/// a BOM, 2 UTF-16BE, 3 UTF-8; [cap] the buffer size with its NUL. UTF-8 is
/// copied byte for byte (malformed bytes too, by the lead byte's length),
/// whole characters only; UTF-16 surrogates (pairs or lone) become `?`. The
/// result stops at the first zero byte, as a C string does.
List<int> firmwareText(Uint8List src, int enc, int cap) {
  final out = <int>[];
  if (cap == 0) return out;
  final n = src.length;
  bool put(int cp) {
    final List<int> b;
    if (cp < 0x80) {
      b = [cp];
    } else if (cp < 0x800) {
      b = [0xC0 | (cp >> 6), 0x80 | (cp & 0x3F)];
    } else {
      b = [0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F)];
    }
    if (out.length + b.length + 1 > cap) return false; // never cut a character
    out.addAll(b);
    return true;
  }

  if (enc == 0 || enc == 3) {
    for (var i = 0; i < n && src[i] != 0; i++) {
      if (enc == 3) {
        final c = src[i];
        final len = c < 0x80
            ? 1
            : (c >> 5) == 6
            ? 2
            : (c >> 4) == 14
            ? 3
            : (c >> 3) == 30
            ? 4
            : 1;
        if (i + len > n || out.length + len + 1 > cap) break;
        out.addAll(src.sublist(i, i + len));
        i += len - 1;
      } else if (!put(src[i])) {
        break;
      }
    }
  } else {
    var le = false;
    var i = 0;
    if (enc == 1 && n >= 2) {
      if (src[0] == 0xFF && src[1] == 0xFE) {
        le = true;
        i = 2;
      } else if (src[0] == 0xFE && src[1] == 0xFF) {
        i = 2;
      }
    }
    for (; i + 1 < n; i += 2) {
      var u = le ? src[i] | (src[i + 1] << 8) : (src[i] << 8) | src[i + 1];
      if (u == 0) break;
      if (u >= 0xD800 && u < 0xE000) {
        if (u < 0xDC00 && i + 3 < n) {
          final lo = le
              ? src[i + 2] | (src[i + 3] << 8)
              : (src[i + 2] << 8) | src[i + 3];
          if (lo >= 0xDC00 && lo < 0xE000) i += 2; // the low half too
        }
        u = 0x3F; // '?'
      }
      if (!put(u)) break;
    }
  }
  final nul = out.indexOf(0);
  return nul < 0 ? out : out.sublist(0, nul);
}

// ---- MP4 / M4A ----------------------------------------------------------------

int _be16(Uint8List b, int o) => (b[o] << 8) | b[o + 1];
int _be64(Uint8List b, int o) => (_be32(b, o) << 32) | _be32(b, o + 4);

int _fourcc(String s) =>
    (s.codeUnitAt(0) << 24) |
    (s.codeUnitAt(1) << 16) |
    (s.codeUnitAt(2) << 8) |
    s.codeUnitAt(3);

final _cNam = (0xA9 << 24) | (0x6E << 16) | (0x61 << 8) | 0x6D; // ©nam
final _cArt = (0xA9 << 24) | (0x41 << 16) | (0x52 << 8) | 0x54; // ©ART
final _cAlb = (0xA9 << 24) | (0x61 << 16) | (0x6C << 8) | 0x62; // ©alb
final _cGen = (0xA9 << 24) | (0x67 << 16) | (0x65 << 8) | 0x6E; // ©gen
final _cDay = (0xA9 << 24) | (0x64 << 16) | (0x61 << 8) | 0x79; // ©day

/// A box: its type and payload range (`box_t` in mp4.c).
typedef _Box = ({int type, int body, int end});

/// `pctx_t`: what the walk of the boxes gathers.
final class _Mp4 {
  _Mp4(this.src);

  final ByteSource src;
  final t = _Fields();
  int boxes = 0;
  bool inAudioTrak = false, gotAudio = false, aac = false, other = false;
  int timescale = 0, duration = 0, movieTs = 0;
  bool elstOk = false;
  int elstMediaTime = 0, elstSeg = 0;
  bool itunOk = false;
  int itunSkip = 0, itunPad = 0, itunLen = 0;
  // From the ALAC sample entry and the sample tables.
  Uint8List? cookie;
  int sampleCount = 0;
  bool haveStsz = false, haveStsc = false, haveStco = false;
  int stscCount = 0, stcoCount = 0;

  Uint8List read(int at, int n) => src.readAt(at, n);
}

/// `read_box`: the box at [pos] within [limit], or null.
_Box? _readBox(_Mp4 p, int pos, int limit) {
  if (pos + 8 > limit) return null;
  final h = p.read(pos, 8);
  if (h.length != 8) return null;
  var size = _be32(h, 0);
  final type = _be32(h, 4);
  var hl = 8;
  if (size == 1) {
    final x = p.read(pos + 8, 8);
    if (x.length != 8) return null;
    size = _be64(x, 0);
    hl = 16;
  } else if (size == 0) {
    size = limit - pos;
  }
  if (size < hl || size > limit - pos) return null;
  return (type: type, body: pos + hl, end: pos + size);
}

int _hexVal(int c) => c >= 0x30 && c <= 0x39
    ? c - 0x30
    : c >= 0x61 && c <= 0x66
    ? c - 0x61 + 10
    : c >= 0x41 && c <= 0x46
    ? c - 0x41 + 10
    : -1;

/// `freeform_item`: a `----` item; only com.apple.iTunes iTunSMPB is read.
void _freeformItem(_Mp4 p, _Box it) {
  var pos = it.body;
  var isSmpb = false;
  for (var i = 0; i < 4; i++) {
    final c = _readBox(p, pos, it.end);
    if (c == null) break;
    if (c.type == _fourcc('name') && c.end - c.body == 4 + 8) {
      isSmpb =
          ascii.decode(p.read(c.body + 4, 8), allowInvalid: true) == 'iTunSMPB';
    } else if (c.type == _fourcc('data') && isSmpb && c.end - c.body > 8) {
      var n = c.end - c.body - 8;
      if (n > 71) n = 71;
      final v = p.read(c.body + 8, n);
      if (v.isEmpty) return;
      var s = 0;
      final e = v.length;
      // `smpb_token`: the next hexadecimal token, or null.
      int? token() {
        while (s < e && v[s] == 0x20) {
          s++;
        }
        var digits = 0, x = 0;
        while (s < e && _hexVal(v[s]) >= 0 && digits < 16) {
          x = (x << 4) | _hexVal(v[s]);
          s++;
          digits++;
        }
        if (digits == 0 || (s < e && v[s] != 0x20 && v[s] != 0)) return null;
        return x;
      }

      final t0 = token();
      final pr = t0 == null ? null : token();
      final pd = pr == null ? null : token();
      final len = pd == null ? null : token();
      // Unsigned compares, as in the C: a token of 2^63 or more is negative
      // here and huge there.
      if (len != null &&
          pr! >= 0 &&
          pr < 0x10000 &&
          pd! >= 0 &&
          pd < 0x100000) {
        p.itunOk = true;
        p.itunSkip = pr;
        p.itunPad = pd;
        p.itunLen = len;
      }
      return;
    }
    pos = c.end;
  }
}

/// `ilst_item`: one iTunes tag item, its `data` box at most 96 bytes (a
/// longer one is skipped by the firmware; its text is still displayed).
void _ilstItem(_Mp4 p, _Box it) {
  if (it.type == _fourcc('covr')) return; // pictures: the art cache
  if (it.type == _fourcc('----')) {
    _freeformItem(p, it);
    return;
  }
  final d = _readBox(p, it.body, it.end);
  if (d == null || d.type != _fourcc('data') || d.end - d.body < 8) return;
  final n = d.end - d.body - 8;
  final type = it.type;
  final t = p.t;
  final isText =
      type == _cNam ||
      type == _cArt ||
      type == _cAlb ||
      type == _fourcc('aART') ||
      type == _cGen;
  if (n > 96) {
    // The firmware skips it; the whole text is still shown.
    if (isText && n <= _maxText * 4) {
      final v = p.read(d.body + 8, n);
      if (v.length != n) return;
      final text = _text(v, 3);
      if (type == _cNam) t.title = text;
      if (type == _cArt) t.artist = text;
      if (type == _cAlb) t.album = text;
      if (type == _fourcc('aART')) t.albumArtist = text;
      if (type == _cGen) t.genre = _genreText(text, bareNumberOk: false);
    }
    return;
  }
  if (p.read(d.body, 4).length != 4) return;
  final v = p.read(d.body + 8, n);
  if (v.length != n) return;
  if (type == _cNam) {
    t.title = _text(v, 3);
    t.titleKey = firmwareText(v, 3, _fwTitleCap);
  } else if (type == _cArt) {
    t.artist = _text(v, 3);
    t.artistKey = firmwareText(v, 3, _fwArtistCap);
  } else if (type == _cAlb) {
    t.album = _text(v, 3);
    t.albumKey = firmwareText(v, 3, _fwAlbumCap);
  } else if (type == _fourcc('trkn') && n >= 4) {
    t.track = _be16(v, 2);
  } else if (type == _fourcc('aART')) {
    t.albumArtist = _text(v, 3);
    t.albumArtistKey = firmwareText(v, 3, _fwAlbumArtistCap);
  } else if (type == _fourcc('sonm')) {
    t.titleSort = _text(v, 3);
    t.titleSortKey = firmwareText(v, 3, _fwSortCap);
  } else if (type == _fourcc('soar')) {
    t.artistSortKey = firmwareText(v, 3, _fwSortCap);
  } else if (type == _fourcc('soal')) {
    t.albumSortKey = firmwareText(v, 3, _fwSortCap);
  } else if (type == _fourcc('soaa')) {
    t.albumArtistSortKey = firmwareText(v, 3, _fwSortCap);
  } else if (type == _cGen) {
    t.genre = _genreText(_text(v, 3), bareNumberOk: false);
    t.genreKey = firmwareGenre(
      firmwareText(v, 3, _fwGenreRaw),
      _fwGenreCap,
      bareNumberOk: false,
    );
  } else if (type == _fourcc('gnre') && n >= 2) {
    // ID3v1 genre number + 1 (0 wraps to a number past the table: none).
    final i = _be16(v, 0) - 1;
    if (i >= 0 && i < _genres.length) {
      final g = ascii.encode(_genres[i]);
      t.genre = _genres[i];
      t.genreKey = firmwareGenre(g, _fwGenreCap, bareNumberOk: false);
    }
  } else if (type == _cDay) {
    // `char y[8] = {0}; memcpy(y, v, n < 4 ? n : 4)`
    final y = v.sublist(0, n < 4 ? n : 4);
    final nul = y.indexOf(0);
    t.year = firmwareYear(nul < 0 ? y : y.sublist(0, nul));
  } else if (type == _fourcc('disk') && n >= 4) {
    t.disc = _be16(v, 2);
  } else if (type == _fourcc('cpil') && n >= 1) {
    t.compilation = v[0] != 0;
  }
}

/// `leaf`: a box that is not a container.
void _mp4Leaf(_Mp4 p, _Box b, int parent) {
  final len = b.end - b.body;
  if (parent == _fourcc('ilst')) {
    _ilstItem(p, b);
    return;
  }
  if (b.type == _fourcc('mvhd') && len >= 20) {
    final h = p.read(b.body, 24);
    final got = h.length;
    p.movieTs = got >= 24 && h[0] == 1
        ? _be32(h, 20)
        : got >= 20 && h[0] == 0
        ? _be32(h, 12)
        : 0;
    return;
  }
  if (b.type == _fourcc('elst') && !p.gotAudio && len >= 8) {
    final h = p.read(b.body, 8);
    if (h.length != 8) return;
    final ver = h[0], n = _be32(h, 4), esz = ver == 1 ? 20 : 12;
    p.elstOk = false;
    var found = 0;
    for (var i = 0; i < n && i < 8 && 8 + (i + 1) * esz <= len; i++) {
      final e = p.read(b.body + 8 + i * esz, esz);
      if (e.length != esz) break;
      final mt = ver == 1 ? _be64(e, 8) : _be32(e, 4).toSigned(32);
      if (mt < 0) continue; // an empty edit (a delay)
      found++;
      p.elstMediaTime = mt;
      p.elstSeg = ver == 1 ? _be64(e, 0) : _be32(e, 0);
    }
    p.elstOk = found == 1;
    return;
  }
  if (b.type == _fourcc('mdhd') && !p.gotAudio) {
    if (len >= 24) {
      final h = p.read(b.body, 32);
      if (h.length >= 24) {
        if (h[0] == 1 && len >= 32 && h.length >= 32) {
          p.timescale = _be32(h, 20);
          p.duration = _be64(h, 24);
        } else {
          p.timescale = _be32(h, 12);
          p.duration = _be32(h, 16);
        }
      }
    }
    return;
  }
  if (!p.inAudioTrak) return;
  if (b.type == _fourcc('stsd')) {
    if (len < 8 + 36) return;
    final e = p.read(b.body + 8, 36);
    if (e.length != 36) return;
    final fmt = _be32(e, 4), esize = _be32(e, 0), ver = _be16(e, 16);
    final child = b.body + 8 + 36 + (ver == 1 ? 16 : 0);
    if (fmt == _fourcc('mp4a')) {
      p.aac = true;
      return;
    }
    if (fmt != _fourcc('alac')) {
      p.other = true;
      return;
    }
    final eend = b.body + 8 + esize;
    final c = _readBox(p, child, eend < b.end ? eend : b.end);
    if (c != null && c.type == _fourcc('alac')) {
      final cl = c.end - c.body;
      if (cl >= 4 + 24) {
        final n = cl - 4 < 64 ? cl - 4 : 64; // MP4_COOKIE_MAX
        final k = p.read(c.body + 4, n);
        if (k.length == n) p.cookie = k;
      }
    }
  } else if (b.type == _fourcc('stsz')) {
    final h = p.read(b.body, 12);
    if (len >= 12 && h.length == 12) {
      final fixed = _be32(h, 4);
      p.sampleCount = _be32(h, 8);
      p.haveStsz = true;
      if (fixed == 0 && p.sampleCount * 4 > len - 12) p.sampleCount = 0;
    }
  } else if (b.type == _fourcc('stsc')) {
    final h = p.read(b.body, 8);
    if (len >= 8 && h.length == 8) {
      p.stscCount = _be32(h, 4);
      p.haveStsc = true;
      if (p.stscCount * 12 > len - 8) p.stscCount = 0;
    }
  } else if (b.type == _fourcc('stco') || b.type == _fourcc('co64')) {
    final h = p.read(b.body, 8);
    if (len >= 8 && h.length == 8) {
      p.stcoCount = _be32(h, 4);
      p.haveStco = true;
      final w = b.type == _fourcc('co64') ? 8 : 4;
      if (p.stcoCount * w > len - 8) p.stcoCount = 0;
    }
  }
}

final _mp4Containers = {
  for (final s in ['moov', 'mdia', 'minf', 'stbl', 'udta', 'ilst', 'edts'])
    _fourcc(s),
};

/// `walk`: the boxes from [pos] to [end], at most 20000 in all.
void _mp4Walk(_Mp4 p, int pos, int end, int depth, int parent) {
  while (p.boxes < 20000) {
    final b = _readBox(p, pos, end);
    if (b == null) break;
    p.boxes++;
    if (_mp4Containers.contains(b.type)) {
      if (depth < 8) _mp4Walk(p, b.body, b.end, depth + 1, b.type);
    } else if (b.type == _fourcc('meta')) {
      if (depth < 8) _mp4Walk(p, b.body + 4, b.end, depth + 1, b.type);
    } else if (b.type == _fourcc('trak')) {
      if (!p.gotAudio) {
        p.inAudioTrak = false;
        p.elstOk = false;
        _mp4Walk(p, b.body, b.end, depth + 1, b.type);
        if (p.inAudioTrak) p.gotAudio = true;
        p.inAudioTrak = false;
      }
    } else if (b.type == _fourcc('hdlr')) {
      final h = p.read(b.body, 12);
      if (b.end - b.body >= 12 &&
          h.length == 12 &&
          _be32(h, 8) == _fourcc('soun')) {
        p.inAudioTrak = true;
      }
    } else {
      _mp4Leaf(p, b, parent);
    }
    pos = b.end;
  }
}

final _u64Max = (BigInt.one << 64) - BigInt.one;

/// [v] as the C's `uint64_t`: Dart's ints are signed, so a field of 2^63 or
/// more reads as negative here.
BigInt _u64(int v) => BigInt.from(v).toUnsigned(64);

/// `scale_u64`: v * mul / div in unsigned 64-bit arithmetic, or null on a
/// zero divisor or when v * mul would overflow 64 bits.
BigInt? _scale(int v, int mul, int div) {
  final u = _u64(v), m = BigInt.from(mul);
  if (div == 0 || (mul != 0 && u > _u64Max ~/ m)) return null;
  return u * m ~/ BigInt.from(div);
}

/// `mp4_parse`: the tags, and for ALAC the duration as the firmware works
/// it out (the media header's length at the cookie's rate, trimmed by a
/// single-edit edit list, else by iTunSMPB). AAC: the firmware recognises
/// it but cannot play it, so it has no duration on the device; the phone
/// plays it, so its duration is the media header's.
AudioTags _readMp4(ByteSource src) {
  final p = _Mp4(src);
  final h = src.readAt(0, 8);
  if (h.length != 8) {
    throw const TagReadException(_damaged, 'file is cut short');
  }
  if (!_startsWith(h, 4, 'ftyp')) {
    throw const TagReadException(
      UnreadableReason.wrongFormat,
      'not an MP4 file',
    );
  }
  _mp4Walk(p, 0, src.length, 0, 0);
  if (!p.gotAudio) {
    // The device keeps the tags it walked past (`META_E_NO_AUDIO`).
    throw TagReadException(
      _damaged,
      'no audio track',
      partial: p.t.tags(codec: 'mp4', durationMs: 0, sampleRate: 0),
    );
  }
  if (p.aac) {
    // The app's own rule (the device works out no AAC duration): the media
    // header's length, read unsigned, capped at 2^40 ms.
    var ms = 0;
    if (p.timescale != 0) {
      final v =
          _u64(p.duration) * BigInt.from(1000) ~/ BigInt.from(p.timescale);
      ms = v > BigInt.one << 40 ? 1 << 40 : v.toInt();
    }
    return p.t.tags(codec: 'aac', durationMs: ms, sampleRate: 0);
  }
  final cookie = p.cookie;
  if (p.other || cookie == null || cookie.length < 24) {
    return p.t.tags(codec: 'mp4', durationMs: 0, sampleRate: 0);
  }
  // The cookie is authoritative for the stream format. A file the device
  // refuses (`mp4_parse`'s checks before the duration) has none there.
  final frameLength = _be32(cookie, 0);
  final bits = cookie[5], channels = cookie[9];
  final rate = _be32(cookie, 20);
  final none = p.t.tags(codec: 'alac', durationMs: 0, sampleRate: rate);
  if (!p.haveStsz ||
      !p.haveStsc ||
      !p.haveStco ||
      p.sampleCount == 0 ||
      p.stscCount == 0 ||
      p.stcoCount == 0 ||
      channels < 1 ||
      channels > 2 ||
      (rate != 44100 && rate != 48000 && rate != 88200 && rate != 96000) ||
      (bits != 16 && bits != 20 && bits != 24) ||
      frameLength == 0 ||
      frameLength > 16384) {
    return none;
  }
  var total = 0;
  if (p.timescale != 0) {
    // duration * rate without overflow, clamped to 2^40 frames; the
    // duration is unsigned (a v1 media header's 64 bits).
    final d = _u64(p.duration), ts = BigInt.from(p.timescale);
    final secs = d ~/ ts, rem = d % ts, r = BigInt.from(rate);
    total = secs > (BigInt.one << 40) ~/ r
        ? 1 << 40
        : (secs * r + rem * r ~/ ts).toInt();
  }
  final media = p.sampleCount * frameLength;
  var trimmed = false;
  if (p.elstOk && p.timescale != 0 && p.elstMediaTime >= 0) {
    final skip = _scale(p.elstMediaTime, rate, p.timescale);
    final BigInt? len;
    if (p.elstSeg != 0 && p.movieTs != 0) {
      len = _scale(p.elstSeg, rate, p.movieTs);
    } else {
      final t = BigInt.from(total);
      len = skip == null ? null : (t > skip ? t - skip : BigInt.zero);
    }
    final m = BigInt.from(media);
    if (skip != null &&
        len != null &&
        len != BigInt.zero &&
        skip < BigInt.from(0x10000) &&
        skip <= m &&
        len <= m - skip) {
      total = len.toInt();
      trimmed = true;
    }
  }
  // iTunSMPB's length is unsigned: one of 2^63 or more (negative here) does
  // not fit the media.
  if (!trimmed &&
      p.itunOk &&
      p.itunLen > 0 &&
      p.itunSkip <= media &&
      p.itunLen <= media - p.itunSkip) {
    total = p.itunLen;
  }
  return p.t.tags(
    codec: 'alac',
    durationMs: total * 1000 ~/ rate,
    sampleRate: rate,
    bitDepth: bits,
  );
}

// ---- WAV --------------------------------------------------------------------

/// `wav_parse` (app/audio/wav.c): no tags; the duration when the firmware
/// can play the file (integer PCM, mono or stereo, 16, 24 or 32 bits,
/// 44.1 to 96 kHz). A file it cannot play keeps no duration, as on the
/// device; one that is not a WAV, or is damaged, is an error.
AudioTags _readWav(ByteSource src) {
  final size = src.length;
  final h = src.readAt(0, 12);
  if (h.length != 12) {
    throw const TagReadException(_damaged, 'file is cut short');
  }
  if (!_startsWith(h, 0, 'RIFF') || !_startsWith(h, 8, 'WAVE')) {
    throw const TagReadException(
      UnreadableReason.wrongFormat,
      'not a WAV file',
    );
  }
  int le16(Uint8List b, int o) => b[o] | (b[o + 1] << 8);
  var haveFmt = false;
  var tag = 0, rate = 0, align = 0, bits = 0, ch = 0;
  var pos = 12;
  for (var guard = 0; guard < 64; guard++) {
    final ck = src.readAt(pos, 8);
    if (pos + 8 > size || ck.length != 8) {
      throw TagReadException(
        _damaged,
        haveFmt ? 'no audio data' : 'no format chunk',
      );
    }
    var chunk = _le32(ck, 4);
    final body = pos + 8;
    if (_startsWith(ck, 0, 'fmt ')) {
      if (chunk < 16) throw const TagReadException(_damaged, 'bad format');
      final n = chunk < 40 ? chunk : 40;
      final f = src.readAt(body, n);
      if (f.length != n) throw const TagReadException(_damaged, 'bad format');
      tag = le16(f, 0);
      ch = le16(f, 2);
      rate = _le32(f, 4);
      align = le16(f, 12);
      bits = le16(f, 14);
      if (tag == 0xFFFE) {
        if (chunk < 40 || n < 40) {
          throw const TagReadException(_damaged, 'bad format');
        }
        tag = le16(f, 24);
      }
      haveFmt = true;
    } else if (_startsWith(ck, 0, 'data')) {
      if (!haveFmt) throw const TagReadException(_damaged, 'no format chunk');
      const none = AudioTags(codec: 'wav');
      if (tag != 1 ||
          ch < 1 ||
          ch > 2 ||
          (bits != 16 && bits != 24 && bits != 32) ||
          (rate != 44100 && rate != 48000 && rate != 88200 && rate != 96000)) {
        return none; // the device cannot play it: no duration there
      }
      if (align != ch * bits ~/ 8) {
        throw const TagReadException(_damaged, 'bad format');
      }
      final avail = size > body ? size - body : 0;
      if (chunk == 0 || chunk > avail) chunk = avail;
      chunk -= chunk % align;
      if (chunk == 0) throw const TagReadException(_damaged, 'no audio data');
      final frames = chunk ~/ align;
      return AudioTags(
        codec: 'wav',
        durationMs: frames * 1000 ~/ rate,
        sampleRate: rate,
        bitDepth: bits,
      );
    }
    pos = body + chunk + (chunk & 1);
  }
  throw const TagReadException(_damaged, 'no audio data');
}
