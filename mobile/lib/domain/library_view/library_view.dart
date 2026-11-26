// SPDX-License-Identifier: MPL-2.0

/// A read-only view of the library for the browsing screens: what Home's
/// Music menu lists (artists, an artist's albums, an album's tracks with
/// discs, songs, genres, compilations), already sorted and counted.
///
/// This is the port the screens depend on. The app's adapter reads the
/// scanned drift database (`data/library_view`); tests use a fake over the
/// generated test library's manifest (`test/support`).
///
/// Every list is sorted by the firmware's collation (owner decision 11),
/// except where noted: an artist's albums come in year order (UI styles
/// design, drawn; its Open question 6), the album table by artist, an
/// album's tracks by disc then track number.
library;

/// One place a picture can be read from: an embedded picture in a track
/// ([offset] and [length] within the track's file) or a whole cover file
/// ([length] null). [size] and [mtime] are the file's, so that a changed file
/// is a different picture to the art cache.
final class ArtFile {
  const ArtFile({
    required this.uri,
    required this.size,
    required this.mtime,
    this.offset = 0,
    this.length,
  });

  /// What the folder-access plugin opens (`content://` or `file://`).
  final String uri;
  final int size;
  final int mtime;
  final int offset;

  /// The picture's bytes, or null for the whole file.
  final int? length;

  /// Identifies this picture: changes when the file does.
  String get id => '$uri|$size|$mtime|$offset|${length ?? '*'}';

  @override
  bool operator ==(Object other) => other is ArtFile && other.id == id;

  @override
  int get hashCode => id.hashCode;

  @override
  String toString() => 'ArtFile($id)';
}

/// How an album's or a track's art is found. The domain does not decode
/// images: the pictures to try, in the firmware's order (see
/// `domain/library/art_rules.dart`: the embedded JPEG, then the folder's
/// cover file; the first that decodes is the art), or a ready image file
/// ([path], test libraries), and a seed so that something without art still
/// gets a stable placeholder.
final class ArtRef {
  const ArtRef({this.path, required this.seed, this.sources = const []});

  /// No art: the placeholder of seed 0.
  static const none = ArtRef(seed: 0);

  /// A ready image file to show as it is, or null.
  final String? path;

  /// A stable number for the placeholder cover's colours.
  final int seed;

  /// The pictures to try, in order; empty when there is none.
  final List<ArtFile> sources;

  /// The art cache's key: every source's [ArtFile.id], in order (so a changed
  /// file, or a cover file added or removed, is new art). Null with no
  /// sources.
  String? get cacheKey =>
      sources.isEmpty ? null : sources.map((s) => s.id).join('\n');

  /// Whether there is a picture to show (it may still fail to decode).
  bool get hasPicture => path != null || sources.isNotEmpty;

  @override
  bool operator ==(Object other) =>
      other is ArtRef &&
      other.path == path &&
      other.seed == seed &&
      other.cacheKey == cacheKey;

  @override
  int get hashCode => Object.hash(path, seed, cacheKey);
}

/// The two sizes the art cache keeps per picture, as the longest side in
/// pixels.
enum ArtSize {
  /// Rows, the album header and the Music menu (drawn at most 96 design px,
  /// decoded at twice that by `CoverArt`).
  thumb(192, 't'),

  /// Home, Now Playing and Cover Flow (up to 220 design px) and the lock
  /// screen and notification.
  full(600, 'f');

  const ArtSize(this.pixels, this.tag);

  final int pixels;
  final String tag;

  /// The smallest size at least [pixels] wide, or [full].
  static ArtSize forPixels(double pixels) =>
      pixels <= thumb.pixels ? thumb : full;
}

/// One song.
final class Song {
  const Song({
    required this.id,
    required this.title,
    required this.artist,
    required this.album,
    required this.albumId,
    required this.uri,
    this.track,
    this.disc,
    this.duration,
    this.genre,
    this.unplayableReason,
    this.downloadable = false,
    this.sortLetter = '#',
    this.art = ArtRef.none,
  });

  /// Stable across rescans (the player's queue item id).
  final String id;
  final String title;

  /// The track's own artist (shown on its row).
  final String artist;
  final String album;
  final String albumId;

  /// Where the player opens it (a file URI or path).
  final String uri;

  /// The number on the album, or null.
  final int? track;

  /// The disc, or null for an album without disc numbers.
  final int? disc;
  final Duration? duration;
  final String? genre;

  /// Set when the song cannot be played: the reason shown at the right of its
  /// greyed row (`Opus, can't play`).
  final String? unplayableReason;

  /// An iCloud file that is not on the phone (iOS; owner decision 7): greyed
  /// `Not downloaded`, and Select asks iOS to download it.
  final bool downloadable;

  /// The fast-scroll letter of the title: `A` to `Z`, or `#`.
  final String sortLetter;

  /// The track's own art (Now Playing and the lock screen show it, as the
  /// device does), with its album's placeholder seed.
  final ArtRef art;

  bool get playable => unplayableReason == null;
}

/// An album.
final class Album {
  const Album({
    required this.id,
    required this.title,
    required this.artist,
    required this.songCount,
    required this.art,
    this.artistId,
    this.year,
    this.totalTime,
    this.discCount = 1,
    this.compilation = false,
    this.sortLetter = '#',
  });

  final String id;
  final String title;

  /// The album artist (`Various Artists` for a compilation).
  final String artist;

  /// The artist's id, or null for a compilation.
  final String? artistId;
  final int? year;
  final int songCount;

  /// The sum of the songs' durations, or null when not known.
  final Duration? totalTime;
  final int discCount;
  final bool compilation;

  /// The album's art: its first song's (the device asks for an album's art by
  /// its first song in album order).
  final ArtRef art;

  /// The fast-scroll letter of the title.
  final String sortLetter;
}

/// An artist (album artist) with at least one album that is not a
/// compilation.
final class Artist {
  const Artist({
    required this.id,
    required this.name,
    required this.albumCount,
    required this.songCount,
    this.sortLetter = '#',
  });

  final String id;
  final String name;
  final int albumCount;
  final int songCount;
  final String sortLetter;
}

/// A genre.
final class Genre {
  const Genre({
    required this.id,
    required this.name,
    required this.albumCount,
    required this.songCount,
    this.sortLetter = '#',
  });

  final String id;
  final String name;
  final int albumCount;
  final int songCount;
  final String sortLetter;
}

/// One disc of an album, with its tracks in order.
///
/// The firmware draws no disc headings; its album order is disc, then track
/// (`alb_cmp` in lib_build.c: `disc<<16|track`, so songs with no disc number
/// come first). Lead decision (2026-11-23): an album with no disc numbers at
/// all is one disc numbered 1, and gets no heading (one disc). In an album
/// where some songs have disc numbers, the songs with none keep that place
/// (first) as a disc with no [number], drawn under no heading; each disc
/// number after them gets its heading.
final class Disc {
  const Disc({required this.number, required this.songs});

  /// The disc number: 1 when the album has no disc numbers; null for the
  /// songs without one in an album where others have one (no heading).
  final int? number;
  final List<Song> songs;
}

/// An album and its tracks, grouped by disc.
final class AlbumDetail {
  const AlbumDetail({required this.album, required this.discs});

  final Album album;

  /// In disc order. The screens write a disc heading for each only when
  /// there is more than one ([hasDiscHeadings]).
  final List<Disc> discs;

  bool get hasDiscHeadings => discs.length > 1;

  /// Every song, disc by disc: what Select on a track queues.
  List<Song> get songs => [for (final d in discs) ...d.songs];
}

/// The read-only library the browsing screens show.
abstract interface class LibraryView {
  /// Every song, by title.
  Future<List<Song>> songs();

  /// One song by [Song.id] (what a queue item's id is), or null.
  Future<Song?> song(String songId);

  /// Artists by name.
  Future<List<Artist>> artists();

  /// The artist, or null if it is gone.
  Future<Artist?> artist(String artistId);

  /// An artist's albums (compilations excluded), in year order, then title.
  Future<List<Album>> albumsOfArtist(String artistId);

  /// An artist's songs, album by album (year order), then track order.
  Future<List<Song>> songsOfArtist(String artistId);

  /// Every album (compilations included), by title.
  Future<List<Album>> albums();

  /// Every album by artist: the firmware's album table (artist, then album;
  /// compilations last). The Music menu's Artists preview takes its covers
  /// from it. Cover Flow is not in this order: it shows [albums] (owner
  /// decision 14).
  Future<List<Album>> albumsByArtist();

  /// An album with its tracks, or null if it is gone.
  Future<AlbumDetail?> album(String albumId);

  /// Genres by name.
  Future<List<Genre>> genres();

  /// The genre, or null.
  Future<Genre?> genre(String genreId);

  /// A genre's albums, by artist, year, title.
  Future<List<Album>> albumsOfGenre(String genreId);

  /// A genre's songs, album by album, then track order.
  Future<List<Song>> songsOfGenre(String genreId);

  /// Albums flagged as compilations, or by `Various Artists`, by title.
  Future<List<Album>> compilations();
}
