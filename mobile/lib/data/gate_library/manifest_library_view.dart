// SPDX-License-Identifier: MPL-2.0

/// The design gate's library: a [LibraryView] over the generated test
/// library's `manifest.json` (`mobile/tool/gen_test_library`), so the gate
/// shows real names, in the owner's folder convention, with the awkward cases
/// (articles, accents, symbols, discs, compilations, unplayable files).
///
/// It shows each track as the manifest's `expect` says the app should, which
/// is the specification's reading of the tags and the folder convention
/// (`Artist/Year Album/NN Title.ext`). Tracks without an `expect` (hidden
/// files, files the specification leaves open) are not shown.
///
/// What the manifest does not hold is stubbed, and the gate says so:
///
/// - **Duration.** The manifest has none (the fixtures are a second or two of
///   tone), so each song gets a stable made-up length of 2:30 to 7:00 from its
///   path, for the album header's total time. The real adapter reads it.
/// - **Art.** An album gets the folder's `Folder.jpg` or `cover.jpg` when the
///   manifest lists one and [libraryDir] is given; otherwise a placeholder
///   cover whose colours come from [ArtRef.seed]. Embedded art is not
///   extracted here.
/// - **Playback.** With [libraryDir] a song's [Song.uri] is the generated
///   file's path, which the player can open. Without it the uri is
///   `gate-sample:<path>` and the app maps the song to a bundled test tone.
library;

import 'dart:convert';

import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/library_view/sort_name.dart';

/// The artist shown for a compilation's album.
const variousArtists = 'Various Artists';

/// A [LibraryView] built from the test library's manifest.
final class ManifestLibraryView implements LibraryView {
  ManifestLibraryView._(this._songs, this._albums, this._songsByAlbum);

  /// Reads [manifestJson], the text of `manifest.json`. [libraryDir] is the
  /// folder that holds `manifest.json` and `library/` (null: no files).
  factory ManifestLibraryView.parse(String manifestJson, {String? libraryDir}) {
    final manifest = jsonDecode(manifestJson) as Map<String, Object?>;
    final root = manifest['library'] as String? ?? 'library';
    String? full(String rel) => libraryDir == null
        ? null
        : '$libraryDir/$root/$rel'.replaceAll('//', '/');

    // Art files by folder.
    final folderArt = <String, String>{};
    final coverArt = <String, String>{};
    for (final o in (manifest['otherFiles'] as List? ?? const [])) {
      final m = o as Map<String, Object?>;
      final path = m['path'] as String;
      final kind = m['kind'] as String?;
      final dir = _dirOf(path);
      if (kind == 'folderJpg') folderArt[dir] = path;
      if (kind == 'coverJpg') coverArt[dir] = path;
    }

    final songs = <_Raw>[];
    for (final t in (manifest['tracks'] as List? ?? const [])) {
      final m = t as Map<String, Object?>;
      final e = m['expect'] as Map<String, Object?>?;
      if (e == null) continue;
      final path = m['path'] as String;
      if (path.split('/').any((p) => p.startsWith('.'))) continue;
      final tags = m['tags'] as Map<String, Object?>?;
      songs.add(
        _Raw(
          path: path,
          title: e['title'] as String,
          // A compilation has no artist of its own; for anything else a
          // missing one falls back to the folder convention.
          albumArtist:
              (e['artist'] as String?) ??
              ((e['compilation'] as bool? ?? false)
                  ? null
                  : _artistFromFolder(path)),
          performer: (e['performer'] ?? e['artist']) as String?,
          album: e['album'] as String? ?? _albumFromFolder(path),
          year: e['year'] as int?,
          disc: e['disc'] as int?,
          track: e['track'] as int?,
          compilation: e['compilation'] as bool? ?? false,
          playable: e['playable'] as bool? ?? true,
          genre: _nonEmpty(tags?['genre'] as String?),
        ),
      );
    }

    // Albums: one per (album artist, album name); a compilation's artist is
    // Various Artists whatever its tracks' own artists.
    final groups = <String, List<_Raw>>{};
    for (final s in songs) {
      groups.putIfAbsent(s.albumKey, () => []).add(s);
    }

    final albums = <String, Album>{};
    final songsByAlbum = <String, List<Song>>{};
    for (final MapEntry(:key, value: raws) in groups.entries) {
      final first = raws.first;
      final albumId = 'album:$key';
      final years = raws.map((r) => r.year).whereType<int>();
      final discs = {for (final r in raws) r.disc ?? 1};

      // Art: Folder.jpg, then cover.jpg, in any of the album's folders.
      String? artRel;
      for (final r in raws) {
        artRel ??= folderArt[_dirOf(r.path)];
      }
      for (final r in raws) {
        artRel ??= coverArt[_dirOf(r.path)];
      }

      final out = <Song>[];
      for (final r in raws) {
        out.add(
          Song(
            id: r.path,
            title: r.title,
            artist: r.performer ?? r.albumArtist ?? 'Unknown Artist',
            album: first.album,
            albumId: albumId,
            uri: full(r.path) ?? 'gate-sample:${r.path}',
            track: r.track,
            disc: r.disc,
            duration: Duration(seconds: 150 + _fnv(r.path) % 271),
            genre: r.genre,
            unplayableReason: r.playable ? null : 'Damaged file',
            sortLetter: sortLetterOf(r.title),
          ),
        );
      }
      out.sort(_byDiscTrack);
      songsByAlbum[albumId] = out;

      final compilation = first.isCompilation;
      final artist = compilation
          ? variousArtists
          : (first.albumArtist ?? 'Unknown Artist');
      albums[albumId] = Album(
        id: albumId,
        title: first.album,
        artist: artist,
        artistId: compilation ? null : 'artist:$artist',
        year: years.isEmpty ? null : years.reduce((a, b) => a < b ? a : b),
        songCount: out.length,
        totalTime: out.fold<Duration>(
          Duration.zero,
          (sum, s) => sum + (s.duration ?? Duration.zero),
        ),
        discCount: discs.length,
        compilation: compilation,
        art: ArtRef(
          path: artRel == null ? null : full(artRel),
          seed: _fnv(key),
        ),
        sortLetter: sortLetterOf(first.album),
      );
    }

    final allSongs = [for (final l in songsByAlbum.values) ...l]
      ..sort((a, b) => _cmp(sortKey(a.title), sortKey(b.title), a.id, b.id));
    return ManifestLibraryView._(allSongs, albums, songsByAlbum);
  }

  final List<Song> _songs;
  final Map<String, Album> _albums;
  final Map<String, List<Song>> _songsByAlbum;

  // Albums in the order an artist's list shows them: year, then title.
  static int _byYear(Album a, Album b) {
    final ay = a.year ?? 1 << 30;
    final by = b.year ?? 1 << 30;
    if (ay != by) return ay.compareTo(by);
    return compareNames(a.title, b.title);
  }

  static int _byArtistYear(Album a, Album b) {
    final c = compareNames(a.artist, b.artist);
    return c != 0 ? c : _byYear(a, b);
  }

  static int _byDiscTrack(Song a, Song b) {
    final d = (a.disc ?? 1).compareTo(b.disc ?? 1);
    if (d != 0) return d;
    final at = a.track ?? 1 << 30;
    final bt = b.track ?? 1 << 30;
    if (at != bt) return at.compareTo(bt);
    return compareNames(a.title, b.title);
  }

  List<Album> get _all => _albums.values.toList();

  List<Song> _songsOf(Iterable<Album> albums) => [
    for (final a in albums) ..._songsByAlbum[a.id]!,
  ];

  @override
  Future<List<Song>> songs() async => List.unmodifiable(_songs);

  @override
  Future<Song?> song(String songId) async {
    for (final s in _songs) {
      if (s.id == songId) return s;
    }
    return null;
  }

  @override
  Future<List<Artist>> artists() async {
    final byId = <String, List<Album>>{};
    for (final a in _albums.values) {
      if (a.artistId != null) (byId[a.artistId!] ??= []).add(a);
    }
    final out = [
      for (final MapEntry(:key, :value) in byId.entries)
        Artist(
          id: key,
          name: value.first.artist,
          albumCount: value.length,
          songCount: value.fold(0, (n, a) => n + a.songCount),
          sortLetter: sortLetterOf(value.first.artist),
        ),
    ]..sort((a, b) => compareNames(a.name, b.name));
    return out;
  }

  @override
  Future<Artist?> artist(String artistId) async {
    for (final a in await artists()) {
      if (a.id == artistId) return a;
    }
    return null;
  }

  @override
  Future<List<Album>> albumsOfArtist(String artistId) async =>
      _all.where((a) => a.artistId == artistId).toList()..sort(_byYear);

  @override
  Future<List<Song>> songsOfArtist(String artistId) async =>
      _songsOf(await albumsOfArtist(artistId));

  @override
  Future<List<Album>> albums() async => _all
    ..sort((a, b) {
      final c = compareNames(a.title, b.title);
      return c != 0 ? c : compareNames(a.artist, b.artist);
    });

  @override
  Future<List<Album>> albumsForCoverFlow() async => _all..sort(_byArtistYear);

  @override
  Future<AlbumDetail?> album(String albumId) async {
    final album = _albums[albumId];
    if (album == null) return null;
    final byDisc = <int, List<Song>>{};
    for (final s in _songsByAlbum[albumId]!) {
      (byDisc[s.disc ?? 1] ??= []).add(s);
    }
    final numbers = byDisc.keys.toList()..sort();
    return AlbumDetail(
      album: album,
      discs: [for (final n in numbers) Disc(number: n, songs: byDisc[n]!)],
    );
  }

  Map<String, List<Song>> get _byGenre {
    final out = <String, List<Song>>{};
    for (final s in _songs) {
      if (s.genre != null) (out[s.genre!] ??= []).add(s);
    }
    return out;
  }

  @override
  Future<List<Genre>> genres() async => [
    for (final MapEntry(:key, :value) in _byGenre.entries)
      Genre(
        id: 'genre:$key',
        name: key,
        albumCount: {for (final s in value) s.albumId}.length,
        songCount: value.length,
        sortLetter: sortLetterOf(key),
      ),
  ]..sort((a, b) => compareNames(a.name, b.name));

  @override
  Future<Genre?> genre(String genreId) async {
    for (final g in await genres()) {
      if (g.id == genreId) return g;
    }
    return null;
  }

  @override
  Future<List<Album>> albumsOfGenre(String genreId) async {
    final name = genreId.substring('genre:'.length);
    final ids = {for (final s in _byGenre[name] ?? const <Song>[]) s.albumId};
    return [for (final id in ids) _albums[id]!]..sort(_byArtistYear);
  }

  @override
  Future<List<Song>> songsOfGenre(String genreId) async {
    final name = genreId.substring('genre:'.length);
    final inGenre = {for (final s in _byGenre[name] ?? const <Song>[]) s.id};
    return [
      for (final a in await albumsOfGenre(genreId))
        for (final s in _songsByAlbum[a.id]!)
          if (inGenre.contains(s.id)) s,
    ];
  }

  @override
  Future<List<Album>> compilations() async =>
      _all.where((a) => a.compilation).toList()
        ..sort((a, b) => compareNames(a.title, b.title));
}

/// One manifest track, as the specification says it should show.
final class _Raw {
  _Raw({
    required this.path,
    required this.title,
    required this.albumArtist,
    required this.performer,
    required this.album,
    required this.year,
    required this.disc,
    required this.track,
    required this.compilation,
    required this.playable,
    required this.genre,
  });

  final String path;
  final String title;
  final String? albumArtist;
  final String? performer;
  final String album;
  final int? year;
  final int? disc;
  final int? track;
  final bool compilation;
  final bool playable;
  final String? genre;

  bool get isCompilation => compilation;

  String get albumKey => isCompilation
      ? 'va|${album.toLowerCase()}'
      : '${albumArtist!.toLowerCase()}|${album.toLowerCase()}';
}

String _dirOf(String path) {
  final i = path.lastIndexOf('/');
  return i < 0 ? '' : path.substring(0, i);
}

/// `Artist/2016 Long Division (Disc 1)/01 x.flac` to `Long Division`: the
/// folder name without its year and a trailing `(Disc n)`.
String _albumFromFolder(String path) {
  final parts = path.split('/');
  var name = parts.length >= 2 ? parts[parts.length - 2] : 'Unknown Album';
  name = name.replaceFirst(RegExp(r'^\d{4}\s+'), '');
  name = name.replaceFirst(RegExp(r'\s*\(Disc\s*\d+\)\s*$'), '');
  return name.isEmpty ? 'Unknown Album' : name;
}

/// `Lanterns, The/2011 Night Market/01 x.mp3` to `The Lanterns`: the first
/// folder, with a trailing `, The` shown as a leading `The` (the owner's
/// convention). Null for a file at the library root.
String? _artistFromFolder(String path) {
  final parts = path.split('/');
  if (parts.length < 3) return null;
  final name = parts.first;
  return name.endsWith(', The')
      ? 'The ${name.substring(0, name.length - 5)}'
      : name;
}

String? _nonEmpty(String? s) => s == null || s.isEmpty ? null : s;

int _cmp(String a, String b, String ida, String idb) {
  final c = a.compareTo(b);
  return c != 0 ? c : ida.compareTo(idb);
}

/// FNV-1a, 32 bit: a stable hash (String.hashCode is not).
int _fnv(String s) {
  var h = 0x811c9dc5;
  for (final c in s.codeUnits) {
    h = ((h ^ c) * 0x01000193) & 0xffffffff;
  }
  return h;
}
