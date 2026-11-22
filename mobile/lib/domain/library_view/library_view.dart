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
/// design, drawn; its Open question 6), Cover Flow's albums by artist then
/// year, an album's tracks by disc then track number.
library;

/// How an album's art is found. The domain does not decode images: a file
/// path when the album has a cover, and a seed so that an album without one
/// still gets a stable placeholder.
final class ArtRef {
  const ArtRef({this.path, required this.seed});

  /// A cover image file (`Folder.jpg`, `cover.jpg`, or extracted from a tag),
  /// or null.
  final String? path;

  /// A stable number for the placeholder cover's colours.
  final int seed;

  @override
  bool operator ==(Object other) =>
      other is ArtRef && other.path == path && other.seed == seed;

  @override
  int get hashCode => Object.hash(path, seed);
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
    this.sortLetter = '#',
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

  /// The fast-scroll letter of the title: `A` to `Z`, or `#`.
  final String sortLetter;

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
final class Disc {
  const Disc({required this.number, required this.songs});

  /// The disc number (1 when the album has no disc numbers).
  final int number;
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

  /// Every album for Cover Flow: by artist, then year, then title.
  Future<List<Album>> albumsForCoverFlow();

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
