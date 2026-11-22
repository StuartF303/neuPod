// SPDX-License-Identifier: MPL-2.0

/// The [LibraryView] over the scanned drift database: what the browsing
/// screens show.
///
/// One load of the tracks table (joined to its source) is grouped once and
/// kept until the tracks or sources change (drift's table updates), so a
/// screen that asks for artists, then each artist's albums, reads the
/// database once.
///
/// Artists and albums are the firmware's entities (`stage_entity` in
/// `app/library/lib_build.c`): the scan's names are sorted by collation key
/// (`ent_cmp`, 205-207) and every run of equal keys is folded into one row
/// (778), named after the run's first entry in scan order (the `seq`
/// tiebreak, 207; the name, 781). Keys are equal when their whole collation
/// keys are (`ent_cmp_key`, 198-204: the 24-byte prefix, then `key_tie`,
/// 185-193, on the full key when the prefix is full), so "The Beatles" and
/// "Beatles", "AC/DC" and "Ac/Dc", or "Ångström" and "Angstrom" are one
/// artist. An album is (its merged artist, its album key): the group is the
/// album artist's entity id (710-714). The album table is in (artist id,
/// album key) order and an album's id is its place there; the Albums list
/// sorts by key, then that id (`key_cmp`, 209-215, over `albname.rec`,
/// 797-801 and 1039-1041), so equal titles come in artist order. Scan order
/// here is the track's id.
///
/// A song row shows the track's own artist text and its merged album's
/// name, as the firmware's does (`lib_list_row` in `lib_reader.c`: the
/// track's `artist_name` and `lib_album()`'s name; `song_row` in
/// `lib_view.c`, 120-132). Songs sort by the stored `title_sort_key`.
///
/// What the database holds today is what the lists can show. The minimal scan
/// stores each track's artist and album as text and nothing else about them,
/// so until phase 3 brings the album artist, the sort tags, year, disc, genre
/// and compilation flag:
///
/// - an album's artist is the track's artist, as the firmware does when there
///   is no album artist (lib_build.c:361), and names are keyed without sort
///   tags;
/// - a track without an artist is "Unknown Artist" and a track without an
///   album tag takes the name of its folder (the source's name for a file at
///   the root), as `index_file` in `lib_build.c` does (ADR 0017, D5);
/// - genres and compilations are empty; albums have no year and one disc;
///   an album's total time is known only when every song has a duration;
/// - art is a placeholder (seeded from the album).
library;

import 'dart:async';
import 'dart:convert';

import 'package:drift/drift.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/domain/library/collation.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/library_view/reason_words.dart';

/// The artist of a track with none (`lib_build.c`: `"Unknown Artist"`).
const unknownArtist = 'Unknown Artist';

/// A [LibraryView] over [LibraryDatabase]. Call [dispose] when done; after
/// that each call reads the database again (nothing is cached unwatched).
final class DriftLibraryView implements LibraryView {
  DriftLibraryView(this._db);

  final LibraryDatabase _db;
  Future<_Lib>? _lib;
  StreamSubscription<Set<TableUpdate>>? _changes;
  bool _disposed = false;

  /// Stops watching the database and drops the cache.
  Future<void> dispose() async {
    _disposed = true;
    _lib = null;
    await _changes?.cancel();
    _changes = null;
  }

  Future<_Lib> _load() {
    if (_disposed) return _read();
    _changes ??= _db
        .tableUpdates(TableUpdateQuery.onAllTables([_db.tracks, _db.sources]))
        .listen((_) => _lib = null);
    final cached = _lib;
    if (cached != null) return cached;
    final f = _lib = _read();
    // A failed read is not kept: the next call tries again.
    f.then<void>(
      (_) {},
      onError: (Object _) {
        if (identical(_lib, f)) _lib = null;
      },
    );
    return f;
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
  Future<List<Artist>> artists() async => [
    for (final a in (await _load()).artists) a.artist,
  ];

  @override
  Future<Artist?> artist(String artistId) async =>
      (await _load()).artistById[artistId]?.artist;

  // No year in the database yet: an artist's albums are in album-table order
  // (lib_view.c LVW_ARTIST: `ar.first + i - 1`), that is by album key.
  @override
  Future<List<Album>> albumsOfArtist(String artistId) async => [
    for (final g in (await _load()).artistById[artistId]?.albums ?? <_Album>[])
      g.album,
  ];

  // lib_view.c LVW_ARTIST_SONGS: the artist's albums are contiguous in the
  // by-album order.
  @override
  Future<List<Song>> songsOfArtist(String artistId) async => [
    for (final g in (await _load()).artistById[artistId]?.albums ?? <_Album>[])
      for (final r in g.rows) r.song,
  ];

  @override
  Future<List<Album>> albums() async => [
    for (final g in (await _load()).byName) g.album,
  ];

  @override
  Future<List<Album>> albumsForCoverFlow() async => [
    for (final g in (await _load()).table) g.album,
  ];

  @override
  Future<AlbumDetail?> album(String albumId) async {
    final g = (await _load()).albumById[albumId];
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

String _hex(List<int> key) {
  final b = StringBuffer();
  for (final x in key) {
    b.write(x.toRadixString(16).padLeft(2, '0'));
  }
  return b.toString();
}

/// A tag string: `char artist[64]` / `album[64]` in `audio_tags_t`
/// (meta.h), so at most 63 bytes.
const _tagCap = 64;

/// The firmware's entity key of [text]: `lib_collate_key()` of the string as
/// the pool holds it, cut to [cap] - 1 bytes (`cut()` in lib_build.c). The
/// whole key is compared (`ent_cmp_key` + `key_tie`, lib_build.c:185-204;
/// a key has no zero byte, so a prefix that is not full is the whole key).
Uint8List _entityKey(String text, int cap) =>
    collateKeyBytes(firmwareCut(utf8.encode(text), cap));

/// FNV-1a, 32 bit: a stable hash (String.hashCode is not).
int _fnv(String s) {
  var h = 0x811c9dc5;
  for (final c in s.codeUnits) {
    h = ((h ^ c) * 0x01000193) & 0xffffffff;
  }
  return h;
}

/// The loaded table, folded into the firmware's entities.
final class _Lib {
  _Lib(this.rows) {
    byId = {for (final r in rows) r.t.id: r};

    // Artists: one per artist key, named by its first track in scan order
    // (lib_build.c:205-207, 778-781). The table is in key order and an
    // artist's id is its place there.
    final arts = <String, _Artist>{};
    for (final r in rows) {
      (arts[r.artistHex] ??= _Artist(
        r.artistKey,
        r.artistHex,
      )).take(r, r.artist);
    }
    artists = arts.values.toList()..sort((a, b) => compareKeys(a.key, b.key));
    for (final (i, a) in artists.indexed) {
      a.index = i;
    }

    // Albums: one per (artist id, album key) (lib_build.c:709-720), named the
    // same way; the table is in that order (`ent_cmp`).
    final albs = <String, _Album>{};
    for (final r in rows) {
      final a = arts[r.artistHex]!;
      final g = albs['${a.hex}|${r.albumHex}'] ??= _Album(
        a,
        r.albumKey,
        r.albumHex,
      );
      g.take(r, r.album);
      r.group = g;
    }
    table = albs.values.toList()
      ..sort((a, b) {
        final c = a.artist.index.compareTo(b.artist.index);
        return c != 0 ? c : compareKeys(a.key, b.key);
      });
    for (final (i, g) in table.indexed) {
      g.index = i;
      g.sortRows();
      g.artist.albums.add(g);
    }

    // The Albums list: `albname.rec` sorted by `key_cmp`, the key and then
    // the album's id (lib_build.c:209-215, 797-801, 1039-1041).
    byName = [...table]
      ..sort((a, b) {
        final c = compareKeys(a.key, b.key);
        return c != 0 ? c : a.index.compareTo(b.index);
      });

    artistById = {for (final a in artists) a.id: a};
    albumById = {for (final g in table) g.id: g};
  }

  final List<_Row> rows;
  late final Map<int, _Row> byId;

  /// The artist table, in key order.
  late final List<_Artist> artists;

  /// The album table: (artist id, album key).
  late final List<_Album> table;

  /// The Albums list order.
  late final List<_Album> byName;
  late final Map<String, _Artist> artistById;
  late final Map<String, _Album> albumById;
}

final class _Row {
  _Row(this.t, String sourceName)
    : artist = t.artist.isEmpty ? unknownArtist : t.artist,
      album = t.album.isNotEmpty ? t.album : _folderName(t.relPath, sourceName),
      // A folder's name is cut to a pool string (lib_build.c:485-486, 362).
      _albumCap = t.album.isNotEmpty ? _tagCap : firmwarePoolCap;

  final TrackRow t;

  /// The track's own artist text (or "Unknown Artist"), as stored.
  final String artist;

  /// The track's own album text (or its folder's name), as stored.
  final String album;
  final int _albumCap;

  late final Uint8List artistKey = _entityKey(artist, _tagCap);
  late final String artistHex = _hex(artistKey);
  late final Uint8List albumKey = _entityKey(album, _albumCap);
  late final String albumHex = _hex(albumKey);

  /// The merged album this track belongs to.
  late final _Album group;

  late final Song song = () {
    final reason = UnreadableReason.fromCode(t.reasonCode);
    return Song(
      id: '${t.id}',
      title: Track.displayTitleOf(t.title, t.relPath),
      artist: artist,
      album: group.name,
      albumId: group.id,
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

/// A firmware entity: a collation key, named by its first track in scan
/// order, with its place in its table.
abstract base class _Entity {
  _Entity(this.key, this.hex);

  final Uint8List key;
  final String hex;
  String name = '';
  int _firstId = -1;
  int index = 0;

  void take(_Row r, String text) {
    if (_firstId < 0 || r.t.id < _firstId) {
      _firstId = r.t.id;
      name = text;
    }
  }
}

final class _Artist extends _Entity {
  _Artist(super.key, super.hex);

  String get id => 'artist:$hex';

  /// Its albums, in album-table order.
  final albums = <_Album>[];

  late final Artist artist = Artist(
    id: id,
    name: name,
    albumCount: albums.length,
    songCount: albums.fold(0, (n, g) => n + g.rows.length),
    sortLetter: collateLetter(key),
  );
}

final class _Album extends _Entity {
  _Album(this.artist, super.key, super.hex);

  final _Artist artist;
  final rows = <_Row>[];

  String get id => 'album:${artist.hex}|$hex';

  @override
  void take(_Row r, String text) {
    rows.add(r);
    super.take(r, text);
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
      id: id,
      title: name,
      artist: artist.name,
      artistId: artist.id,
      songCount: rows.length,
      totalTime: known
          ? Duration(milliseconds: rows.fold(0, (n, r) => n + r.t.durationMs))
          : null,
      art: ArtRef(seed: _fnv(id)),
      sortLetter: collateLetter(key),
    );
  }();
}
