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
/// (`ent_cmp`) and every run of equal keys is folded into one row, named
/// after the run's first entry in walk order (the `seq` tiebreak). Keys are
/// equal when their whole collation keys are (`ent_cmp_key`: the 24-byte
/// prefix, then `key_tie` on the full key when the prefix is full), so "The
/// Beatles" and "Beatles", "AC/DC" and "Ac/Dc", or "Ångström" and "Angstrom"
/// are one artist. An album is (its merged artist, its album key): the group
/// is the album artist's entity id. The album table is in (artist id, album
/// key) order and an album's id is its place there; the Albums list sorts by
/// key, then that id (`key_cmp` over `albname.rec`), so equal titles come in
/// artist order.
///
/// **Compilations.** A track whose album artist is "Various Artists" in any
/// ASCII case is a compilation track (`index_file`: `comp = ... ||
/// ieq_str(aa, "Various Artists")`). It makes no artist (`stage_entity`:
/// `take = !comp`) and its album's group is `LIB_NONE`, so compilation
/// albums are keyed by the album alone, come last in the album table (after
/// every artist's albums), and so after the other albums of the same title
/// in the Albums list. They are the Compilations list (`comp_first`). An
/// album list row shows no artist for one (`lib_list_row`); its header says
/// "Various Artists" (`album_header` in lib_view.c), which is the [Album]'s
/// artist here.
///
/// **Walk order.** Where the firmware takes "the first" (an entity's name,
/// the order of songs or album tracks with equal keys) it means the order of
/// its walk of the card: `walk_dir` reads each folder with `f_readdir`, which
/// gives the entries in the order of their directory entries on the card.
/// The phone cannot see that order. So, as a lead decision, the app's walk
/// order is a depth-first walk of each track's relative path, folder by
/// folder, a folder's entries (files and folders together) in byte order of
/// their UTF-8 names ([walkCompare]); tracks of different sources go in
/// source id order. It is the same on every rescan, and it does not depend
/// on the order rows were written. The C that makes the tests' expected
/// lists (scripts/app/library_entities.c) creates its files on the FatFs
/// image in this same order, so the firmware's walk there is this one.
///
/// **Keys from the firmware's text.** Artists and albums are keyed on the
/// tag text as the firmware's parser stores it (`artist_key_text`,
/// `album_key_text`: at most 63 bytes, UTF-16 surrogates as `?`, malformed
/// UTF-8 raw), as titles are. The rows show the whole decoded tag (owner
/// decision 11). A row written before schema version 2 has no firmware text
/// until a rescan reads its file again (scan version 2); until then its
/// displayed text, cut to 63 bytes, stands in.
///
/// A song row shows the track's own artist text and its merged album's
/// name, as the firmware's does (`lib_list_row` in `lib_reader.c`: the
/// track's `artist_name` and `lib_album()`'s name; `song_row` in
/// `lib_view.c`). Songs sort by the stored `title_sort_key`, then walk
/// order.
///
/// What the database holds today is what the lists can show. The minimal scan
/// stores each track's artist and album and nothing else about them, so
/// until phase 3 brings the album artist, the sort tags, year, disc, genre
/// and compilation flag:
///
/// - an album's artist is the track's artist, as the firmware does when there
///   is no album artist, and names are keyed without sort tags; only
///   "Various Artists" makes a compilation (the flag is not read yet);
/// - a track without an artist is "Unknown Artist" and a track without an
///   album tag takes the name of its folder (the source's name for a file at
///   the root), as `index_file` in `lib_build.c` does (ADR 0017, D5);
/// - genres are empty; albums have no year and one disc; an album's total
///   time is known only when every song has a duration;
/// - art is a placeholder (seeded from the album).
library;

import 'dart:async';
import 'dart:convert';

import 'package:drift/drift.dart';
import 'package:flutter/foundation.dart' show visibleForTesting;
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
    final q = _db.select(_db.tracks).join([
      innerJoin(_db.sources, _db.sources.id.equalsExp(_db.tracks.sourceId)),
    ]);
    final rows = [
      for (final r in await q.get())
        _Row(r.readTable(_db.tracks), r.readTable(_db.sources).displayName),
    ];
    return _Lib(rows);
  }

  @override
  Future<List<Song>> songs() async => [
    for (final r in (await _load()).songs) r.song,
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

  // Genres are not stored yet (phase 3).
  @override
  Future<List<Genre>> genres() async => const [];

  @override
  Future<Genre?> genre(String genreId) async => null;

  @override
  Future<List<Album>> albumsOfGenre(String genreId) async => const [];

  @override
  Future<List<Song>> songsOfGenre(String genreId) async => const [];

  // lib_reader.c LIB_LIST_COMPILATIONS: the album table from `comp_first`.
  @override
  Future<List<Album>> compilations() async => [
    for (final g in (await _load()).table)
      if (g.artist == null) g.album,
  ];
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

/// FNV-1a, 32 bit: a stable hash (String.hashCode is not).
int _fnv(String s) {
  var h = 0x811c9dc5;
  for (final c in s.codeUnits) {
    h = ((h ^ c) * 0x01000193) & 0xffffffff;
  }
  return h;
}

/// The album artist that makes a compilation (`index_file` in lib_build.c).
final _variousArtists = ascii.encode('Various Artists');

/// `ieq_str()` (lib_build.c): equal bytes, with ASCII `A`..`Z` folded
/// to lower case and nothing else.
bool _ieqAscii(List<int> a, List<int> b) {
  if (a.length != b.length) return false;
  for (var i = 0; i < a.length; i++) {
    var x = a[i], y = b[i];
    if (x >= 0x41 && x <= 0x5A) x += 32;
    if (y >= 0x41 && y <= 0x5A) y += 32;
    if (x != y) return false;
  }
  return true;
}

/// The segments of [relPath] as UTF-8, empty ones (a leading `/`, a `//`)
/// left out.
List<Uint8List> _segments(String relPath) => [
  for (final s in relPath.split('/'))
    if (s.isNotEmpty) utf8.encode(s),
];

int _compareSegments(List<Uint8List> a, List<Uint8List> b) {
  final n = a.length < b.length ? a.length : b.length;
  for (var i = 0; i < n; i++) {
    final c = compareKeys(a[i], b[i]);
    if (c != 0) return c;
  }
  return a.length.compareTo(b.length);
}

/// The app's walk order of two paths in one source (see the library doc
/// above: a lead decision, since the card's directory-entry order is not
/// available to the app): depth first, folder by folder, each folder's
/// entries in byte order of their UTF-8 names. So `a/2.flac` comes before
/// `b/1.flac`, and the folder `a` before the folder `a b` (whole names are
/// compared, not the paths as strings, where ' ' < '/').
/// `scripts/app/library_entities.c` (`walk_cmp`) creates its files in this
/// order.
@visibleForTesting
int walkCompare(String a, String b) =>
    _compareSegments(_segments(a), _segments(b));

/// The loaded table, folded into the firmware's entities.
final class _Lib {
  _Lib(List<_Row> loaded) {
    // Walk order: by source, then the depth-first walk of each source.
    rows = [...loaded]
      ..sort((a, b) {
        final c = a.t.sourceId.compareTo(b.t.sourceId);
        return c != 0 ? c : _compareSegments(a.segments, b.segments);
      });
    for (final (i, r) in rows.indexed) {
      r.walk = i;
    }
    byId = {for (final r in rows) r.t.id: r};

    // The Songs list: the title's key, then walk order (`key_cmp`,
    // lib_build.c; a track's id is its place in the walk).
    songs = [...rows]
      ..sort((a, b) {
        final c = compareKeys(a.t.titleSortKey, b.t.titleSortKey);
        return c != 0 ? c : a.walk.compareTo(b.walk);
      });

    // Artists: one per artist key, named by its first track in walk order
    // (`ent_cmp`, `stage_entity`); compilation tracks make none (`take =
    // !comp`).
    // The table is in key order and an artist's id is its place there.
    final arts = <String, _Artist>{};
    for (final r in rows) {
      if (r.compilation) continue;
      (arts[r.artistHex] ??= _Artist(
        r.artistKey,
        r.artistHex,
      )).take(r, r.artist);
    }
    artists = arts.values.toList()..sort((a, b) => compareKeys(a.key, b.key));
    for (final (i, a) in artists.indexed) {
      a.index = i;
    }

    // Albums: one per (artist id, album key), a compilation's group being
    // LIB_NONE (`stage_entity`), named the same way; the table is in
    // that order (`ent_cmp`), so compilations come last.
    final albs = <String, _Album>{};
    for (final r in rows) {
      final a = r.compilation ? null : arts[r.artistHex]!;
      final g = albs['${a?.hex ?? 'comp'}|${r.albumHex}'] ??= _Album(
        a,
        r.albumKey,
        r.albumHex,
      );
      g.take(r, r.album);
      r.group = g;
    }
    final none = artists.length; // LIB_NONE: after every artist id
    table = albs.values.toList()
      ..sort((a, b) {
        final c = (a.artist?.index ?? none).compareTo(b.artist?.index ?? none);
        return c != 0 ? c : compareKeys(a.key, b.key);
      });
    for (final (i, g) in table.indexed) {
      g.index = i;
      g.sortRows();
      g.artist?.albums.add(g);
    }

    // The Albums list: `albname.rec` sorted by `key_cmp`, the key and then
    // the album's id (lib_build.c).
    byName = [...table]
      ..sort((a, b) {
        final c = compareKeys(a.key, b.key);
        return c != 0 ? c : a.index.compareTo(b.index);
      });

    artistById = {for (final a in artists) a.id: a};
    albumById = {for (final g in table) g.id: g};
  }

  /// Every track, in walk order.
  late final List<_Row> rows;
  late final Map<int, _Row> byId;

  /// The Songs list order.
  late final List<_Row> songs;

  /// The artist table, in key order.
  late final List<_Artist> artists;

  /// The album table: (artist id, album key), compilations last.
  late final List<_Album> table;

  /// The Albums list order.
  late final List<_Album> byName;
  late final Map<String, _Artist> artistById;
  late final Map<String, _Album> albumById;
}

final class _Row {
  _Row(this.t, String sourceName)
    : segments = _segments(t.relPath),
      artist = t.artist.isEmpty ? unknownArtist : t.artist,
      album = t.album.isNotEmpty ? t.album : _folderName(t.relPath, sourceName),
      _folder = _folderName(t.relPath, sourceName);

  final TrackRow t;
  final List<Uint8List> segments;

  /// The track's own artist text (or "Unknown Artist"), as stored.
  final String artist;

  /// The track's own album text (or its folder's name), as stored.
  final String album;
  final String _folder;

  /// Its place in the walk.
  int walk = 0;

  /// The artist as the firmware's `index_file` holds it: the tag as its
  /// parser stored it, or "Unknown Artist" (`index_file`), cut to a pool
  /// string. This is also the album artist (386), since the album artist
  /// tag is not stored yet.
  late final Uint8List _artistText = () {
    final tag = t.artistKeyText ?? firmwareCut(utf8.encode(t.artist), _tagCap);
    return firmwareCut(
      tag.isNotEmpty ? tag : utf8.encode(unknownArtist),
      firmwarePoolCap,
    );
  }();

  /// The album as `index_file` holds it: the tag, or the folder's name
  /// (`index_file`; the folder's name is cut in `walk_dir`).
  late final Uint8List _albumText = () {
    final tag = t.albumKeyText ?? firmwareCut(utf8.encode(t.album), _tagCap);
    return firmwareCut(
      tag.isNotEmpty ? tag : utf8.encode(_folder),
      firmwarePoolCap,
    );
  }();

  /// A compilation track (`index_file` in lib_build.c).
  late final bool compilation = _ieqAscii(_artistText, _variousArtists);

  /// The entity keys: `lib_collate_key()` of the text as the pool holds it.
  /// The whole key is compared (`ent_cmp_key` + `key_tie`,
  /// lib_build.c; a key has no zero byte, so a prefix that is not
  /// full is the whole key).
  late final Uint8List artistKey = collateKeyBytes(_artistText);
  late final String artistHex = _hex(artistKey);
  late final Uint8List albumKey = collateKeyBytes(_albumText);
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
///
/// The firmware takes the leaf of the folder being walked
/// (`walk_dir` in lib_build.c), which for a file at the root of the music folder
/// is that folder's own name ("Music" for `/Music`). The folder the user
/// picked is the app's equivalent of `/Music`, so a root file takes the
/// source's display name. Empty segments (a leading `/`) are skipped, so
/// they cannot give an empty name.
String _folderName(String relPath, String sourceName) {
  final parts = [
    for (final p in relPath.split('/'))
      if (p.isNotEmpty) p,
  ];
  return parts.length >= 2 ? parts[parts.length - 2] : sourceName;
}

/// A firmware entity: a collation key, named by its first track in walk
/// order, with its place in its table.
abstract base class _Entity {
  _Entity(this.key, this.hex);

  final Uint8List key;
  final String hex;
  String name = '';
  int _firstWalk = -1;
  int index = 0;

  void take(_Row r, String text) {
    if (_firstWalk < 0 || r.walk < _firstWalk) {
      _firstWalk = r.walk;
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

/// The artist shown for a compilation (`album_header` in lib_view.c).
const _variousArtistsName = 'Various Artists';

final class _Album extends _Entity {
  _Album(this.artist, super.key, super.hex);

  /// Its artist, or null for a compilation (group `LIB_NONE`).
  final _Artist? artist;
  final rows = <_Row>[];

  String get id => 'album:${artist?.hex ?? 'comp'}|$hex';

  @override
  void take(_Row r, String text) {
    rows.add(r);
    super.take(r, text);
  }

  /// Track number (none first, as `disc<<16|track` sorts in the firmware's
  /// `alb_cmp`), then the title's key, then walk order.
  void sortRows() => rows.sort((a, b) {
    final t = a.t.trackNo.compareTo(b.t.trackNo);
    if (t != 0) return t;
    final c = compareKeys(a.t.titleSortKey, b.t.titleSortKey);
    return c != 0 ? c : a.walk.compareTo(b.walk);
  });

  late final Album album = () {
    final known = rows.every((r) => r.t.durationMs > 0);
    return Album(
      id: id,
      title: name,
      artist: artist?.name ?? _variousArtistsName,
      artistId: artist?.id,
      songCount: rows.length,
      totalTime: known
          ? Duration(milliseconds: rows.fold(0, (n, r) => n + r.t.durationMs))
          : null,
      compilation: artist == null,
      art: ArtRef(seed: _fnv(id)),
      sortLetter: collateLetter(key),
    );
  }();
}
