// SPDX-License-Identifier: MPL-2.0

/// The [LibraryView] over the scanned drift database: what the browsing
/// screens show.
///
/// One load of the tracks table (joined to its source) is grouped once and
/// kept until the tracks or sources change (drift's table updates), so a
/// screen that asks for artists, then each artist's albums, reads the
/// database once.
///
/// Artists, albums and genres are the firmware's entities (`index_file` and
/// `stage_entity` in `app/library/lib_build.c`).
///
/// **Album artist.** Each track has an album artist (`index_file`, `aa`): the
/// album artist tag, else the track's artist, else "Unknown Artist". The
/// Artists list is album artists only (ADR 0017 D4: `stage_entity` takes
/// `aa` for K_ARTIST), and an album belongs to its album artist, so an album
/// whose tracks have different performers and one album artist is one album
/// under that artist. A song row keeps the track's own artist.
///
/// **Keys.** An entity's key is `lib_collate_key()` of its sort text when
/// there is one, else of its name (`ent_fill`): the album artist's sort tag,
/// or the artist's sort tag when there is no album artist tag
/// (`index_file`, `sort_aa`); the album's sort tag; a genre has none. The
/// scan's names are sorted by key (`ent_cmp`) and every run of equal keys is
/// folded into one row, named after the run's first entry in walk order (the
/// `seq` tiebreak). Keys are equal when their whole collation keys are
/// (`ent_cmp_key`: the 24-byte prefix, then `key_tie` on the full key), so
/// "The Beatles" and "Beatles", "AC/DC" and "Ac/Dc", or "Ångström" and
/// "Angstrom" are one artist. An album is (its merged album artist, its album
/// key): the group is the album artist's entity id. The album table is in
/// (artist id, album key) order and an album's id is its place there; the
/// Albums list sorts by key, then that id (`key_cmp` over `albname.rec`), so
/// equal titles come in artist order. An album's songs are in disc, then
/// track number order (`alb_cmp`: `disc<<16|track`, the disc held in a byte
/// so 255 at most, an untagged 0 first), then the title's key, then walk
/// order; its discs are the runs of one disc number.
///
/// **Compilations.** A track is a compilation track when its compilation
/// flag is set or its album artist is "Various Artists" in any ASCII case
/// (`index_file`: `comp = x->compilation || ieq_str(aa, "Various
/// Artists")`). It makes no artist (`stage_entity`: `take = !comp`) and its
/// album's group is `LIB_NONE`, so compilation albums are keyed by the album
/// alone, come last in the album table (after every artist's albums), and so
/// after the other albums of the same title in the Albums list. They are the
/// Compilations list (`comp_first`). An album list row shows no artist for
/// one (`lib_list_row`); its header says "Various Artists" (`album_header`
/// in lib_view.c), which is the [Album]'s artist here.
///
/// **Genres.** Every track with a genre, compilation tracks too, is in its
/// genre (`stage_entity` K_GENRE); a genre's songs are in album order
/// (`by_genre`: the album order, stably grouped by genre). The firmware's
/// genre screen lists the genre's artists (`genre_artists`); this port's
/// [LibraryView] asks for a genre's albums, which are given in album-table
/// order, the order its songs come in.
///
/// **Years.** An album row's year is its first song's (`lvw_row`, LVW_ALBUMS:
/// the first track in album order, 0 for none), which Cover Flow's caption
/// shows (`flow_texts` in ui_library.c); the album's own header takes the
/// first song with a year among its first 100 (`album_header`).
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
/// **Keys from the firmware's text.** Entities are keyed on the tag text as
/// the firmware's parser stores it (`artist_key_text`, `album_key_text`,
/// `album_artist_key_text`, the sort texts, `genre_key_text`: its buffer
/// sizes, UTF-16 surrogates as `?`, malformed UTF-8 raw), as titles are. The
/// rows show the whole decoded tag (owner decision 11). A row written before
/// schema version 3 has no such text until a rescan reads its file again
/// (scan version 3); until then its displayed text, cut to the firmware's
/// buffer, stands in, and it reads as having no album artist, sort tags,
/// genre, disc, year or flag.
///
/// A song row shows the track's own artist text and its merged album's
/// name, as the firmware's does (`lib_list_row` in `lib_reader.c`: the
/// track's `artist_name` and `lib_album()`'s name; `song_row` in
/// `lib_view.c`). Songs sort by the stored `title_sort_key`, then walk
/// order.
///
/// A track without an artist is "Unknown Artist" and a track without an
/// album tag takes the name of its folder (the source's name for a file at
/// the root), as `index_file` does (ADR 0017, D5). An album's total time is
/// known only when every song has a duration.
///
/// **Art** follows the firmware (`domain/library/art_rules.dart`): a song's
/// art is its own pictures (the embedded JPEG, then its folder's cover
/// file); an album's is its first song's in album order, which is what the
/// device's album header and Cover Flow ask for. Both carry the album's
/// placeholder seed, for when no picture decodes.
library;

import 'dart:async';
import 'dart:convert';

import 'package:drift/drift.dart';
import 'package:flutter/foundation.dart' show visibleForTesting;
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/domain/library/art_rules.dart';
import 'package:neupod/domain/library/collation.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/library_view/reason_words.dart';

/// The artist of a track with none (`lib_build.c`: `"Unknown Artist"`).
const unknownArtist = 'Unknown Artist';

/// A [LibraryView] over [LibraryDatabase]. Call [dispose] when done; after
/// that each call reads the database again (nothing is cached unwatched).
final class DriftLibraryView implements LibraryView {
  DriftLibraryView(this._db, {this.platform});

  final LibraryDatabase _db;

  /// The phone whose player decides which formats it cannot decode (those
  /// songs are greyed, `reason_words.dart`); null greys none.
  final PlayerPlatform? platform;
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
        _Row(
          r.readTable(_db.tracks),
          r.readTable(_db.sources).displayName,
          platform,
        ),
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

  // An artist's albums are in album-table order (lib_view.c LVW_ARTIST:
  // `ar.first + i - 1`), that is by album key, as on the device.
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
  Future<List<Album>> albumsByArtist() async => [
    for (final g in (await _load()).table) g.album,
  ];

  @override
  Future<AlbumDetail?> album(String albumId) async {
    final g = (await _load()).albumById[albumId];
    if (g == null) return null;
    return AlbumDetail(album: g.header, discs: g.discs);
  }

  @override
  Future<List<Genre>> genres() async => [
    for (final g in (await _load()).genres) g.genre,
  ];

  @override
  Future<Genre?> genre(String genreId) async =>
      (await _load()).genreById[genreId]?.genre;

  @override
  Future<List<Album>> albumsOfGenre(String genreId) async => [
    for (final g in (await _load()).genreById[genreId]?.albums ?? <_Album>[])
      g.album,
  ];

  // lib_view.c LVW_GENRE_SONGS: `by_genre`, the album order kept within
  // each genre.
  @override
  Future<List<Song>> songsOfGenre(String genreId) async => [
    for (final r in (await _load()).genreById[genreId]?.rows ?? <_Row>[])
      r.song,
  ];

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

/// A tag string: `char artist[64]` / `album[64]` / `album_artist[64]`
/// (meta.h), so at most 63 bytes; `char genre[32]`, so 31.
const _tagCap = 64;
const _genreCap = 32;

/// `TR_DISC` is a byte (lib_build.c: `x->disc > 255 ? 255 : x->disc`).
const _discMax = 255;

/// `HEADER_SONGS_MAX` (lib_view.c): the album header looks at this many
/// songs for its year.
const _headerSongsMax = 100;

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

    // Artists: one per album artist key, named by its first track in walk
    // order (`ent_cmp`, `stage_entity`); compilation tracks make none (`take
    // = !comp`). The table is in key order and an artist's id is its place
    // there.
    final arts = <String, _Artist>{};
    for (final r in rows) {
      if (r.compilation) continue;
      (arts[r.artistHex] ??= _Artist(
        r.artistKey,
        r.artistHex,
      )).take(r, r.albumArtist);
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

    // Genres: one per genre key, compilation tracks included (`stage_entity`
    // K_GENRE: `take = genre != LIB_NONE`), in key order. A genre's songs
    // are the album order (`by_genre`, sorted by genre then by-album
    // position).
    final gens = <String, _Genre>{};
    for (final r in rows) {
      final key = r.genreKey;
      if (key == null) continue;
      (gens[r.genreHex] ??= _Genre(key, r.genreHex)).take(r, r.genre);
    }
    genres = gens.values.toList()..sort((a, b) => compareKeys(a.key, b.key));
    for (final g in table) {
      for (final r in g.rows) {
        final genre = r.genreKey == null ? null : gens[r.genreHex]!;
        if (genre == null) continue;
        genre.rows.add(r);
        if (genre.albums.isEmpty || !identical(genre.albums.last, g)) {
          genre.albums.add(g);
        }
        r.inGenre = genre;
      }
    }
    genreById = {for (final g in genres) g.id: g};
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

  /// The genre table, in key order.
  late final List<_Genre> genres;
  late final Map<String, _Genre> genreById;
}

final class _Row {
  _Row(this.t, String sourceName, this._platform)
    : segments = _segments(t.relPath),
      artist = t.artist.isEmpty ? unknownArtist : t.artist,
      album = t.album.isNotEmpty ? t.album : _folderName(t.relPath, sourceName),
      _folder = _folderName(t.relPath, sourceName);

  final TrackRow t;
  final List<Uint8List> segments;
  final PlayerPlatform? _platform;

  /// The track's own artist text (or "Unknown Artist"), as stored.
  final String artist;

  /// The track's own album text (or its folder's name), as stored.
  final String album;
  final String _folder;

  /// Its place in the walk.
  int walk = 0;

  /// The firmware's text of a tag: [held], or for a row from before it was
  /// stored, [shown] cut to the firmware's buffer of [cap] bytes.
  static Uint8List _fw(Uint8List? held, String shown, int cap) =>
      held ?? firmwareCut(utf8.encode(shown), cap);

  /// The artist as the firmware's `index_file` holds it: the tag as its
  /// parser stored it, or "Unknown Artist", cut to a pool string.
  late final Uint8List _artistText = () {
    final tag = _fw(t.artistKeyText, t.artist, _tagCap);
    return firmwareCut(
      tag.isNotEmpty ? tag : utf8.encode(unknownArtist),
      firmwarePoolCap,
    );
  }();

  /// The album artist tag as the firmware's parser stored it.
  late final Uint8List _albumArtistTag = _fw(
    t.albumArtistKeyText,
    t.albumArtist,
    _tagCap,
  );

  /// The album artist as `index_file` holds it (`aa`): the album artist
  /// tag, else the artist (as held above), cut to a pool string.
  late final Uint8List _albumArtistText = _albumArtistTag.isNotEmpty
      ? firmwareCut(_albumArtistTag, firmwarePoolCap)
      : _artistText;

  /// The album artist as displayed: the tag when the firmware has one, else
  /// the track's artist.
  late final String albumArtist = _albumArtistTag.isNotEmpty
      ? t.albumArtist
      : artist;

  /// `index_file`'s `sort_aa`: the album artist sort tag, else the artist
  /// sort tag when there is no album artist tag.
  late final Uint8List _albumArtistSort = () {
    final aaSort = t.albumArtistSortKeyText ?? Uint8List(0);
    if (aaSort.isNotEmpty) return aaSort;
    return _albumArtistTag.isEmpty
        ? t.artistSortKeyText ?? Uint8List(0)
        : Uint8List(0);
  }();

  /// The album as `index_file` holds it: the tag, or the folder's name
  /// (`index_file`; the folder's name is cut in `walk_dir`).
  late final Uint8List _albumText = () {
    final tag = _fw(t.albumKeyText, t.album, _tagCap);
    return firmwareCut(
      tag.isNotEmpty ? tag : utf8.encode(_folder),
      firmwarePoolCap,
    );
  }();

  /// A compilation track (`index_file` in lib_build.c): the flag, or the
  /// album artist "Various Artists" in any ASCII case.
  late final bool compilation =
      t.compilation || _ieqAscii(_albumArtistText, _variousArtists);

  /// The entity keys: `lib_collate_key()` of the sort text when there is
  /// one, else of the name, as the pool holds it (`ent_fill`). The whole key
  /// is compared (`ent_cmp_key` + `key_tie`, lib_build.c; a key has no zero
  /// byte, so a prefix that is not full is the whole key).
  late final Uint8List artistKey = collateKeyBytes(
    _albumArtistSort.isNotEmpty ? _albumArtistSort : _albumArtistText,
  );
  late final String artistHex = _hex(artistKey);
  late final Uint8List albumKey = () {
    final sort = t.albumSortKeyText ?? Uint8List(0);
    return collateKeyBytes(sort.isNotEmpty ? sort : _albumText);
  }();
  late final String albumHex = _hex(albumKey);

  /// The genre as the firmware holds it, or null for none (`index_file`: a
  /// genre is pooled only when not empty).
  late final Uint8List? genreKey = () {
    final g = _fw(t.genreKeyText, t.genre, _genreCap);
    return g.isEmpty ? null : collateKeyBytes(firmwareCut(g, firmwarePoolCap));
  }();
  late final String genreHex = genreKey == null ? '' : _hex(genreKey!);

  /// The genre as displayed.
  String get genre => t.genre;

  /// The disc as the track table holds it (a byte).
  int get disc => t.discNo > _discMax ? _discMax : t.discNo;

  /// The merged album this track belongs to.
  late final _Album group;

  /// Its genre, or null.
  _Genre? inGenre;

  /// The pictures to try for this track, in the firmware's order
  /// (`art_sources`): the embedded JPEG, then the folder's cover file.
  late final List<ArtFile> artFiles = () {
    final kind = ArtKind.fromCode(t.artKind);
    final embedded = kind == ArtKind.none
        ? null
        : EmbeddedArt(
            offset: t.artOffset ?? 0,
            length: t.artLength ?? 0,
            kind: kind,
            type: 0,
          );
    final folder = t.folderArtUri;
    return [
      for (final s in artSources(embedded, folderHas: folder != null))
        switch (s) {
          ArtSourceKind.embedded => ArtFile(
            uri: t.uri,
            size: t.size,
            mtime: t.mtime,
            offset: embedded!.offset,
            length: embedded.length,
          ),
          ArtSourceKind.folder => ArtFile(
            uri: folder!,
            size: t.folderArtSize ?? 0,
            mtime: t.folderArtMtime ?? 0,
          ),
        },
    ];
  }();

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
      disc: disc == 0 ? null : disc,
      duration: t.durationMs == 0 ? null : Duration(milliseconds: t.durationMs),
      genre: inGenre?.name,
      unplayableReason: reason != null
          ? unreadableReasonText(reason, t.codec)
          : _platform != null && cannotDecode(_platform, t.codec)
          ? cannotDecodeText(t.codec)
          : null,
      sortLetter: collateLetter(t.titleSortKey),
      art: ArtRef(seed: group.seed, sources: artFiles),
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

  /// The placeholder seed of the album and its songs.
  int get seed => _fnv(id);

  @override
  void take(_Row r, String text) {
    rows.add(r);
    super.take(r, text);
  }

  /// Disc, then track number (`disc<<16|track` in the firmware's `alb_cmp`;
  /// none first), then the title's key, then walk order.
  void sortRows() => rows.sort((a, b) {
    final d = a.disc.compareTo(b.disc);
    if (d != 0) return d;
    final t = a.t.trackNo.compareTo(b.t.trackNo);
    if (t != 0) return t;
    final c = compareKeys(a.t.titleSortKey, b.t.titleSortKey);
    return c != 0 ? c : a.walk.compareTo(b.walk);
  });

  /// Its songs in album order, one [Disc] per run of a disc number (the
  /// firmware draws no headings; lead decision, see [Disc]): an album with
  /// no disc numbers is one disc numbered 1; where some songs have one, the
  /// songs without (first, as `alb_cmp` sorts them) are a disc with no
  /// number, drawn under no heading.
  late final List<Disc> discs = () {
    final out = <Disc>[];
    var songs = <Song>[];
    int? at;
    void close() {
      if (at == null) return;
      final n = at;
      out.add(Disc(number: n != 0 ? n : null, songs: songs));
    }

    for (final r in rows) {
      if (at != null && r.disc != at) {
        close();
        songs = <Song>[];
      }
      at = r.disc;
      songs.add(r.song);
    }
    close();
    // No disc numbers at all: one disc, numbered 1.
    if (out.length == 1 && out.single.number == null) {
      return [Disc(number: 1, songs: out.single.songs)];
    }
    return out;
  }();

  Album _make(int year) {
    final known = rows.every((r) => r.t.durationMs > 0);
    return Album(
      id: id,
      title: name,
      artist: artist?.name ?? _variousArtistsName,
      artistId: artist?.id,
      year: year == 0 ? null : year,
      songCount: rows.length,
      totalTime: known
          ? Duration(milliseconds: rows.fold(0, (n, r) => n + r.t.durationMs))
          : null,
      discCount: discs.length,
      compilation: artist == null,
      // rows are in album order (sortRows): the first is the first song.
      art: ArtRef(seed: seed, sources: rows.first.artFiles),
      sortLetter: collateLetter(key),
    );
  }

  /// The album as its list rows show it: the year of its first song
  /// (`lvw_row`, LVW_ALBUMS).
  late final Album album = _make(rows.isEmpty ? 0 : rows.first.t.year);

  /// The album as its own screen's header shows it: the first year among
  /// its first 100 songs (`album_header`).
  late final Album header = _make(
    rows
        .take(_headerSongsMax)
        .map((r) => r.t.year)
        .firstWhere((y) => y != 0, orElse: () => 0),
  );
}

/// A genre (`stage_entity` K_GENRE): its songs in album order and the albums
/// they are on, in album-table order.
final class _Genre extends _Entity {
  _Genre(super.key, super.hex);

  String get id => 'genre:$hex';
  final rows = <_Row>[];
  final albums = <_Album>[];

  late final Genre genre = Genre(
    id: id,
    name: name,
    albumCount: albums.length,
    songCount: rows.length,
    sortLetter: collateLetter(key),
  );
}
