// SPDX-License-Identifier: MPL-2.0

/// The [LibraryView] over the scanned drift database: what the browsing
/// screens show.
///
/// Every call reads the database again, so a scan that finishes while a
/// screen is open shows on the next read. Sorting is the firmware's
/// collation (`domain/library/collation.dart`, owner decision 11): songs by
/// the stored `title_sort_key`, artists and albums by the key of their name.
///
/// What the database holds today is what the lists can show. The minimal scan
/// stores each track's artist and album as text and nothing else about them,
/// so until phase 3 brings the album artist, year, disc, genre and
/// compilation flag:
///
/// - an album is one per (artist, album) pair and its artist is the track's
///   artist; an artist is a distinct artist text;
/// - genres and compilations are empty; albums have no year and one disc;
///   an album's total time is known only when every song has a duration;
/// - an empty artist or album shows as "Unknown Artist" / "Unknown Album" in
///   the Artists and Albums lists (the specification does not word it; song
///   rows skip the missing part);
/// - art is a placeholder (seeded from the album).
library;

import 'package:drift/drift.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/domain/library/collation.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/library_view/reason_words.dart';

const unknownArtist = 'Unknown Artist';
const unknownAlbum = 'Unknown Album';

/// A [LibraryView] over [LibraryDatabase].
final class DriftLibraryView implements LibraryView {
  DriftLibraryView(this._db);

  final LibraryDatabase _db;

  Future<List<_Row>> _rows() async {
    final rows =
        await (_db.select(_db.tracks)..orderBy([
              (t) => OrderingTerm.asc(t.titleSortKey),
              (t) => OrderingTerm.asc(t.id),
            ]))
            .get();
    return [for (final r in rows) _Row(r)];
  }

  /// Every album, with its songs in track order.
  Future<List<_AlbumGroup>> _groups() async {
    final byKey = <String, _AlbumGroup>{};
    for (final r in await _rows()) {
      (byKey[r.albumKey] ??= _AlbumGroup(r)).rows.add(r);
    }
    final out = byKey.values.toList();
    for (final g in out) {
      g.rows.sort(_byTrack);
    }
    return out;
  }

  static int _byTrack(_Row a, _Row b) {
    // Untagged track numbers (0) come last, then by title as listed.
    final at = a.t.trackNo == 0 ? 1 << 30 : a.t.trackNo;
    final bt = b.t.trackNo == 0 ? 1 << 30 : b.t.trackNo;
    if (at != bt) return at.compareTo(bt);
    final c = compareKeys(a.t.titleSortKey, b.t.titleSortKey);
    return c != 0 ? c : a.t.id.compareTo(b.t.id);
  }

  static int _byTitle(Album a, Album b) {
    final c = compareKeys(collateKey(a.title), collateKey(b.title));
    return c != 0 ? c : compareKeys(collateKey(a.artist), collateKey(b.artist));
  }

  static int _byArtist(Album a, Album b) {
    final c = compareKeys(collateKey(a.artist), collateKey(b.artist));
    return c != 0 ? c : _byTitle(a, b);
  }

  @override
  Future<List<Song>> songs() async => [for (final r in await _rows()) r.song];

  @override
  Future<Song?> song(String songId) async {
    final id = int.tryParse(songId);
    if (id == null) return null;
    final r = await (_db.select(
      _db.tracks,
    )..where((t) => t.id.equals(id))).getSingleOrNull();
    return r == null ? null : _Row(r).song;
  }

  @override
  Future<List<Artist>> artists() async {
    final byName = <String, List<_AlbumGroup>>{};
    for (final g in await _groups()) {
      (byName[g.artist] ??= []).add(g);
    }
    return [
      for (final MapEntry(:key, :value) in byName.entries)
        Artist(
          id: _artistId(key),
          name: key,
          albumCount: value.length,
          songCount: value.fold(0, (n, g) => n + g.rows.length),
          sortLetter: _letter(key),
        ),
    ]..sort((a, b) => compareKeys(collateKey(a.name), collateKey(b.name)));
  }

  @override
  Future<Artist?> artist(String artistId) async {
    for (final a in await artists()) {
      if (a.id == artistId) return a;
    }
    return null;
  }

  // No year in the database yet: an artist's albums are by title.
  @override
  Future<List<Album>> albumsOfArtist(String artistId) async => [
    for (final g in await _groups())
      if (_artistId(g.artist) == artistId) g.album,
  ]..sort(_byTitle);

  @override
  Future<List<Song>> songsOfArtist(String artistId) async {
    final groups = [
      for (final g in await _groups())
        if (_artistId(g.artist) == artistId) g,
    ]..sort((a, b) => _byTitle(a.album, b.album));
    return [
      for (final g in groups)
        for (final r in g.rows) r.song,
    ];
  }

  @override
  Future<List<Album>> albums() async =>
      [for (final g in await _groups()) g.album]..sort(_byTitle);

  @override
  Future<List<Album>> albumsForCoverFlow() async =>
      [for (final g in await _groups()) g.album]..sort(_byArtist);

  @override
  Future<AlbumDetail?> album(String albumId) async {
    for (final g in await _groups()) {
      if (g.key == albumId) {
        return AlbumDetail(
          album: g.album,
          discs: [
            Disc(number: 1, songs: [for (final r in g.rows) r.song]),
          ],
        );
      }
    }
    return null;
  }

  // Genres and compilations are not stored yet (phase 3).
  @override
  Future<List<Genre>> genres() async => const [];

  @override
  Future<Genre?> genre(String genreId) async => null;

  @override
  Future<List<Album>> albumsOfGenre(String genreId) async => const [];

  @override
  Future<List<Song>> songsOfGenre(String genreId) async => const [];

  @override
  Future<List<Album>> compilations() async => const [];
}

String _artistId(String name) => 'artist:$name';

String _letter(String name) => collateLetter(collateKey(name));

/// FNV-1a, 32 bit: a stable hash (String.hashCode is not).
int _fnv(String s) {
  var h = 0x811c9dc5;
  for (final c in s.codeUnits) {
    h = ((h ^ c) * 0x01000193) & 0xffffffff;
  }
  return h;
}

final class _Row {
  _Row(this.t);

  final TrackRow t;

  String get artist => t.artist.isEmpty ? unknownArtist : t.artist;
  String get albumName => t.album.isEmpty ? unknownAlbum : t.album;
  String get albumKey =>
      'album:${t.artist.toLowerCase()}|${t.album.toLowerCase()}';

  Song get song {
    final reason = UnreadableReason.fromCode(t.reasonCode);
    return Song(
      id: '${t.id}',
      title: Track.displayTitleOf(t.title, t.relPath),
      artist: t.artist,
      album: t.album,
      albumId: albumKey,
      uri: t.uri,
      track: t.trackNo == 0 ? null : t.trackNo,
      duration: t.durationMs == 0 ? null : Duration(milliseconds: t.durationMs),
      unplayableReason: reason == null
          ? null
          : unreadableReasonText(reason, t.codec),
      sortLetter: collateLetter(t.titleSortKey),
    );
  }
}

final class _AlbumGroup {
  _AlbumGroup(_Row first)
    : artist = first.artist,
      title = first.albumName,
      key = first.albumKey;

  final String artist;
  final String title;
  final String key;
  final rows = <_Row>[];

  Album get album {
    final known = rows.every((r) => r.t.durationMs > 0);
    return Album(
      id: key,
      title: title,
      artist: artist,
      artistId: _artistId(artist),
      songCount: rows.length,
      totalTime: known
          ? Duration(milliseconds: rows.fold(0, (n, r) => n + r.t.durationMs))
          : null,
      art: ArtRef(seed: _fnv(key)),
      sortLetter: _letter(title),
    );
  }
}
