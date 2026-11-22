// SPDX-License-Identifier: MPL-2.0

/// The [LibraryView] over the scanned drift database: what the browsing
/// screens show.
///
/// One load of the tracks table (joined to its source) is grouped once and
/// kept until the table changes (drift's table updates), so a screen that
/// asks for artists, then each artist's albums, reads the database once.
/// Sorting is the firmware's (`app/library/lib_build.c`): by collation key,
/// and equal keys in scan order (`key_tie`, then the first track's id), so
/// two names that fold to the same key keep a stable order. Songs use the
/// stored `title_sort_key`.
///
/// What the database holds today is what the lists can show. The minimal scan
/// stores each track's artist and album as text and nothing else about them,
/// so until phase 3 brings the album artist, year, disc, genre and
/// compilation flag:
///
/// - an album is one per (artist, album) pair and its artist is the track's
///   artist, as the firmware does when there is no album artist; an artist is
///   a distinct artist text;
/// - a track without an artist is "Unknown Artist" and a track without an
///   album tag takes the name of its folder (the source's name for a file at
///   the root), as `index_file` in `lib_build.c` does (ADR 0017, D5);
/// - genres and compilations are empty; albums have no year and one disc;
///   an album's total time is known only when every song has a duration;
/// - art is a placeholder (seeded from the album).
library;

import 'dart:async';

import 'package:drift/drift.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/domain/library/collation.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/library_view/reason_words.dart';

/// The artist of a track with none (`lib_build.c`: `"Unknown Artist"`).
const unknownArtist = 'Unknown Artist';

/// A [LibraryView] over [LibraryDatabase]. Call [dispose] when done.
final class DriftLibraryView implements LibraryView {
  DriftLibraryView(this._db);

  final LibraryDatabase _db;
  Future<_Lib>? _lib;
  StreamSubscription<Set<TableUpdate>>? _changes;

  /// Stops watching the database.
  Future<void> dispose() async => _changes?.cancel();

  Future<_Lib> _load() {
    _changes ??= _db
        .tableUpdates(TableUpdateQuery.onTable(_db.tracks))
        .listen((_) => _lib = null);
    return _lib ??= _read();
  }

  Future<_Lib> _read() async {
    final q =
        _db.select(_db.tracks).join([
          innerJoin(_db.sources, _db.sources.id.equalsExp(_db.tracks.sourceId)),
        ])..orderBy([
          OrderingTerm.asc(_db.tracks.titleSortKey),
          OrderingTerm.asc(_db.tracks.id),
        ]);
    final rows = [
      for (final r in await q.get())
        _Row(r.readTable(_db.tracks), r.readTable(_db.sources).displayName),
    ];
    return _Lib(rows);
  }

  @override
  Future<List<Song>> songs() async => [
    for (final r in (await _load()).rows) r.song,
  ];

  @override
  Future<Song?> song(String songId) async {
    final id = int.tryParse(songId);
    if (id == null) return null;
    return (await _load()).byId[id]?.song;
  }

  @override
  Future<List<Artist>> artists() async => (await _load()).artists;

  @override
  Future<Artist?> artist(String artistId) async {
    for (final a in (await _load()).artists) {
      if (a.id == artistId) return a;
    }
    return null;
  }

  // No year in the database yet: an artist's albums are in album order.
  @override
  Future<List<Album>> albumsOfArtist(String artistId) async => [
    for (final g in (await _load()).byAlbum)
      if (_artistId(g.artist) == artistId) g.album,
  ];

  @override
  Future<List<Song>> songsOfArtist(String artistId) async => [
    for (final g in (await _load()).byAlbum)
      if (_artistId(g.artist) == artistId)
        for (final r in g.rows) r.song,
  ];

  @override
  Future<List<Album>> albums() async => [
    for (final g in (await _load()).byAlbum) g.album,
  ];

  @override
  Future<List<Album>> albumsForCoverFlow() async => [
    for (final g in (await _load()).byArtist) g.album,
  ];

  @override
  Future<AlbumDetail?> album(String albumId) async {
    final g = (await _load()).groups[albumId];
    if (g == null) return null;
    return AlbumDetail(
      album: g.album,
      discs: [
        Disc(number: 1, songs: [for (final r in g.rows) r.song]),
      ],
    );
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

int _byKey(String a, String b) => compareKeys(collateKey(a), collateKey(b));

/// FNV-1a, 32 bit: a stable hash (String.hashCode is not).
int _fnv(String s) {
  var h = 0x811c9dc5;
  for (final c in s.codeUnits) {
    h = ((h ^ c) * 0x01000193) & 0xffffffff;
  }
  return h;
}

/// The loaded table, grouped.
final class _Lib {
  _Lib(this.rows) {
    byId = {for (final r in rows) r.t.id: r};
    final g = <String, _Group>{};
    for (final r in rows) {
      (g[r.albumKey] ??= _Group(r)).add(r);
    }
    groups = g;
    for (final x in g.values) {
      x.sortRows();
    }
    // The firmware's order: key, then the first track's id (`ent_cmp`).
    byAlbum = g.values.toList()
      ..sort((a, b) {
        final c = _byKey(a.title, b.title);
        return c != 0 ? c : a.firstId.compareTo(b.firstId);
      });
    byArtist = [...byAlbum]
      ..sort((a, b) {
        final c = _byKey(a.artist, b.artist);
        if (c != 0) return c;
        final d = _byKey(a.title, b.title);
        return d != 0 ? d : a.firstId.compareTo(b.firstId);
      });

    final byName = <String, List<_Group>>{};
    for (final x in byAlbum) {
      (byName[x.artist] ??= []).add(x);
    }
    int firstOf(List<_Group> l) =>
        l.map((x) => x.firstId).reduce((a, b) => a < b ? a : b);
    final names = byName.keys.toList()
      ..sort((a, b) {
        final c = _byKey(a, b);
        return c != 0 ? c : firstOf(byName[a]!).compareTo(firstOf(byName[b]!));
      });
    artists = [
      for (final name in names)
        Artist(
          id: _artistId(name),
          name: name,
          albumCount: byName[name]!.length,
          songCount: byName[name]!.fold(0, (n, x) => n + x.rows.length),
          sortLetter: _letter(name),
        ),
    ];
  }

  final List<_Row> rows;
  late final Map<int, _Row> byId;
  late final Map<String, _Group> groups;
  late final List<_Group> byAlbum;
  late final List<_Group> byArtist;
  late final List<Artist> artists;
}

final class _Row {
  _Row(this.t, String sourceName)
    : artist = t.artist.isEmpty ? unknownArtist : t.artist,
      album = t.album.isNotEmpty ? t.album : _folderName(t.relPath, sourceName);

  final TrackRow t;
  final String artist;
  final String album;

  /// One album per (artist, album) pair, compared exactly as the firmware's
  /// pool does (`strcmp`).
  String get albumKey => 'album:$artist|$album';

  late final Song song = () {
    final reason = UnreadableReason.fromCode(t.reasonCode);
    return Song(
      id: '${t.id}',
      title: Track.displayTitleOf(t.title, t.relPath),
      artist: artist,
      album: album,
      albumId: albumKey,
      uri: t.uri,
      track: t.trackNo == 0 ? null : t.trackNo,
      duration: t.durationMs == 0 ? null : Duration(milliseconds: t.durationMs),
      unplayableReason: reason == null
          ? null
          : unreadableReasonText(reason, t.codec),
      sortLetter: collateLetter(t.titleSortKey),
    );
  }();
}

/// The name of the folder that holds [relPath], or [sourceName] for a file
/// at the source's root.
String _folderName(String relPath, String sourceName) {
  final parts = relPath.split('/');
  return parts.length >= 2 ? parts[parts.length - 2] : sourceName;
}

final class _Group {
  _Group(_Row first)
    : artist = first.artist,
      title = first.album,
      key = first.albumKey,
      firstId = first.t.id;

  final String artist;
  final String title;
  final String key;
  int firstId;
  final rows = <_Row>[];

  void add(_Row r) {
    rows.add(r);
    if (r.t.id < firstId) firstId = r.t.id;
  }

  /// Track number (none first, as `disc<<16|track` sorts in the firmware's
  /// `alb_cmp`), then the title's key, then the scan order.
  void sortRows() => rows.sort((a, b) {
    final t = a.t.trackNo.compareTo(b.t.trackNo);
    if (t != 0) return t;
    final c = compareKeys(a.t.titleSortKey, b.t.titleSortKey);
    return c != 0 ? c : a.t.id.compareTo(b.t.id);
  });

  late final Album album = () {
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
  }();
}
