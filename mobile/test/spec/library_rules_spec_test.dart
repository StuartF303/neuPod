// SPDX-License-Identifier: MPL-2.0

// Spec suite: the library's naming and grouping rules (docs/app/README.md,
// The library and Formats):
//
// - Where tags are missing, the device's fallback (owner decision 15): the
//   album is the folder's name, the artist "Unknown Artist", the title the
//   file name; nothing else is taken from the path (no Artist/Year Album/NN
//   Title parsing, no ", The" handling).
// - The discs of one album (one album tag, disc number tags) are one album
//   with disc sections; names that differ by "(Disc n)" stay apart.
// - A track whose album artist is "Various Artists", or whose compilation
//   flag is set, belongs to Compilations and is not listed under each
//   contributing artist's albums.
// - A file the platform cannot decode is listed greyed with the reason at the
//   right ("Opus, can't play"); it is never hidden.
//
// Where the specification is open (see the PR and the ambiguity list in
// mobile/README.md) this suite asserts nothing either way.
//
// WIRING: wiredLibraryFromTracks writes the tracks into an in-memory drift
// database as the scanner stores them (test/data/library_view/seed.dart) and
// reads the lists back through the app's DriftLibraryView, the LibraryView
// the screens use.
//
// The player is the iPhone's (PlayerPlatform.ios): that is where an Opus file
// cannot play until the Xiph decoders land (owner decision 5), so where its
// row reads "Opus, can't play". On Android the same file plays.
@Tags(['spec'])
library;

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/domain/library_view/library_view.dart' as app;
import 'package:neupod/domain/library_view/reason_words.dart';

import '../data/library_view/seed.dart';

/// One scanned file as the library builder receives it: its path relative to
/// the library folder and the tags read from it (null: tag absent).
typedef TrackInput = ({
  String path,
  String? title,
  String? artist,
  String? albumArtist,
  String? album,
  int? track,
  int? disc,
  int? year,
  bool compilation,

  /// False when the platform cannot decode the file.
  bool playable,

  /// The format's display name, e.g. 'FLAC', 'Opus'.
  String format,
});

/// A row of the Songs list.
typedef SongRow = ({
  String title,
  String artist,
  String album,
  bool greyed,

  /// The text at the right of a greyed row; null on a playable row.
  String? reason,
});

/// A track row in an album, in album order (disc, then track).
typedef AlbumTrack = ({int? disc, int? track, String title, bool greyed});

typedef AlbumView = ({
  String name,
  String? artist,
  int? year,
  List<AlbumTrack> tracks,
});

/// The browsing lists, as the screens show them.
abstract interface class LibraryView {
  /// Music > Artists: album artists, display names, in display order.
  List<String> artists();

  /// The albums of an artist from [artists] (not its compilation tracks).
  List<AlbumView> albumsOf(String artist);

  /// Music > Compilations.
  List<AlbumView> compilations();

  /// Music > Songs, in display order.
  List<SongRow> songs();
}

typedef LibraryFromTracks = Future<LibraryView> Function(
  List<TrackInput> tracks,
);

/// The lists of the app's [app.LibraryView], read once, so the tests can ask
/// for them without waiting.
final class _Snapshot implements LibraryView {
  _Snapshot(this._artists, this._albums, this._compilations, this._songs);

  final List<String> _artists;
  final Map<String, List<AlbumView>> _albums;
  final List<AlbumView> _compilations;
  final List<SongRow> _songs;

  @override
  List<String> artists() => _artists;

  @override
  List<AlbumView> albumsOf(String artist) => _albums[artist] ?? const [];

  @override
  List<AlbumView> compilations() => _compilations;

  @override
  List<SongRow> songs() => _songs;
}

Future<AlbumView> _albumView(app.LibraryView view, app.Album a) async {
  final detail = (await view.album(a.id))!;
  return (
    name: detail.album.title,
    artist: detail.album.artist,
    year: detail.album.year,
    tracks: [
      for (final s in detail.songs)
        (disc: s.disc, track: s.track, title: s.title, greyed: !s.playable),
    ],
  );
}

/// Reads every list of [view] into a [_Snapshot].
Future<LibraryView> snapshotOf(app.LibraryView view) async {
  final artists = await view.artists();
  return _Snapshot(
    [for (final a in artists) a.name],
    {
      for (final a in artists)
        a.name: [
          for (final al in await view.albumsOfArtist(a.id))
            await _albumView(view, al),
        ],
    },
    [for (final c in await view.compilations()) await _albumView(view, c)],
    [
      for (final s in await view.songs())
        (
          title: s.title,
          artist: s.artist,
          album: s.album,
          greyed: !s.playable,
          reason: s.unplayableReason,
        ),
    ],
  );
}

Future<LibraryView> _fromTracks(List<TrackInput> tracks) async {
  final db = LibraryDatabase(NativeDatabase.memory());
  final view = DriftLibraryView(db, platform: PlayerPlatform.ios);
  try {
    final source = await addSource(db);
    for (final t in tracks) {
      final codec = t.format.toLowerCase();
      await addTrack(
        db,
        sourceId: source,
        relPath: t.path,
        title: t.title ?? '',
        artist: t.artist ?? '',
        albumArtist: t.albumArtist ?? '',
        album: t.album ?? '',
        trackNo: t.track ?? 0,
        discNo: t.disc ?? 0,
        year: t.year ?? 0,
        compilation: t.compilation,
        codec: codec,
        // A file the player cannot decode is greyed by the view; any other
        // unplayable file is one the scanner could not read: damaged.
        reason: t.playable || cannotDecode(PlayerPlatform.ios, codec)
            ? null
            : UnreadableReason.damaged.code,
      );
    }
    return await snapshotOf(view);
  } finally {
    await view.dispose();
    await db.close();
  }
}

final LibraryFromTracks wiredLibraryFromTracks = _fromTracks;

TrackInput _t(
  String path, {
  String? title,
  String? artist,
  String? albumArtist,
  String? album,
  int? track,
  int? disc,
  int? year,
  bool compilation = false,
  bool playable = true,
  String format = 'FLAC',
}) => (
  path: path,
  title: title,
  artist: artist,
  albumArtist: albumArtist,
  album: album,
  track: track,
  disc: disc,
  year: year,
  compilation: compilation,
  playable: playable,
  format: format,
);

/// An untagged file.
TrackInput _bare(String path, {bool playable = true, String format = 'FLAC'}) =>
    _t(path, playable: playable, format: format);

Future<LibraryView> _lib(List<TrackInput> tracks) =>
    wiredLibraryFromTracks(tracks);

List<String> _titles(AlbumView a) => [for (final t in a.tracks) t.title];

void main() {
  group('library rules', () {
    group('untagged files (owner decision 15)', () {
      test('only the album comes from the path', () async {
        final lib = await _lib([_bare('Marrow/1977 Bone Dry/01 Dust.flac')]);
        expect(lib.artists(), ['Unknown Artist']);
        final albums = lib.albumsOf('Unknown Artist');
        expect(albums, hasLength(1));
        expect(albums.single.name, '1977 Bone Dry');
        expect(albums.single.year, isNull);
        expect(albums.single.tracks.single.track, isNull);
        expect(albums.single.tracks.single.title, '01 Dust.flac');
        expect(lib.songs().single.title, '01 Dust.flac');
      });

      test('a ", The" folder name is not changed', () async {
        final lib = await _lib([
          _bare('Hollow Pines, The/Cedar, The/01 Frost.flac'),
        ]);
        expect(lib.artists(), ['Unknown Artist']);
        expect(lib.albumsOf('Unknown Artist').single.name, 'Cedar, The');
      });

      test('tags, where present, are used', () async {
        final lib = await _lib([
          _t(
            'Folder Artist/2000 Folder Album/01 File Title.flac',
            title: 'Tag Title',
            artist: 'Tag Artist',
            albumArtist: 'Tag Artist',
            album: 'Tag Album',
            track: 7,
            year: 1990,
          ),
        ]);
        expect(lib.artists(), ['Tag Artist']);
        final album = lib.albumsOf('Tag Artist').single;
        expect(album.name, 'Tag Album');
        expect(album.year, 1990);
        expect(album.tracks.single.track, 7);
        expect(album.tracks.single.title, 'Tag Title');
      });
    });

    group('discs', () {
      test('disc number tags over two folders make one album', () async {
        final lib = await _lib([
          _t(
            'Zed Zone/2005 Greatest Hits CD2/01 Zinc.flac',
            title: 'Zinc',
            artist: 'Zed Zone',
            album: 'Greatest Hits',
            track: 1,
            disc: 2,
          ),
          _t(
            'Zed Zone/2005 Greatest Hits/02 Zebra.flac',
            title: 'Zebra',
            artist: 'Zed Zone',
            album: 'Greatest Hits',
            track: 2,
            disc: 1,
          ),
          _t(
            'Zed Zone/2005 Greatest Hits/01 Zulu.flac',
            title: 'Zulu',
            artist: 'Zed Zone',
            album: 'Greatest Hits',
            track: 1,
            disc: 1,
          ),
        ]);
        final albums = lib.albumsOf('Zed Zone');
        expect(albums, hasLength(1));
        expect(albums.single.name, 'Greatest Hits');
        expect(_titles(albums.single), ['Zulu', 'Zebra', 'Zinc']);
        expect([for (final t in albums.single.tracks) t.disc], [1, 1, 2]);
      });

      test(
        '"(Disc n)" in the album tags: two albums, as on the device',
        () async {
          final lib = await _lib([
            for (final (d, n, title) in [
              (2, 1, 'Return'),
              (1, 2, 'Middle Distance'),
              (1, 1, 'Opening'),
            ])
              _t(
                'Kestrel Lane/2016 Long Division (Disc $d)/0$n $title.flac',
                title: title,
                artist: 'Kestrel Lane',
                album: 'Long Division (Disc $d)',
                track: n,
                year: 2016,
              ),
          ]);
          final albums = {
            for (final a in lib.albumsOf('Kestrel Lane')) a.name: a,
          };
          expect(albums.keys, [
            'Long Division (Disc 1)',
            'Long Division (Disc 2)',
          ]);
          expect(_titles(albums['Long Division (Disc 1)']!), [
            'Opening',
            'Middle Distance',
          ]);
          expect(_titles(albums['Long Division (Disc 2)']!), ['Return']);
        },
      );

      test('"(Disc n)" in untagged folder names: two albums', () async {
        final lib = await _lib([
          _bare('Quiet Engine/2015 Machines (Disc 2)/01 Spring.flac'),
          _bare('Quiet Engine/2015 Machines (Disc 1)/02 Piston.flac'),
          _bare('Quiet Engine/2015 Machines (Disc 1)/01 Gear.flac'),
        ]);
        final albums = {
          for (final a in lib.albumsOf('Unknown Artist')) a.name: a,
        };
        expect(albums.keys, [
          '2015 Machines (Disc 1)',
          '2015 Machines (Disc 2)',
        ]);
        expect(_titles(albums['2015 Machines (Disc 1)']!), [
          '01 Gear.flac',
          '02 Piston.flac',
        ]);
      });

      test('the same album name by two artists is two albums', () async {
        final lib = await _lib([
          _t(
            'Zed Zone/2005 Greatest Hits/01 Zulu.flac',
            title: 'Zulu',
            artist: 'Zed Zone',
            album: 'Greatest Hits',
          ),
          _t(
            'Ember Field/2005 Greatest Hits/01 Spark.mp3',
            title: 'Spark',
            artist: 'Ember Field',
            album: 'Greatest Hits',
            format: 'MP3',
          ),
        ]);
        expect(_titles(lib.albumsOf('Zed Zone').single), ['Zulu']);
        expect(_titles(lib.albumsOf('Ember Field').single), ['Spark']);
      });
    });

    group('compilations', () {
      // Track numbers set: without them an album is in title order, as on
      // the device (`alb_cmp` in lib_build.c: disc and track, then the
      // title's key).
      List<TrackInput> tracks() => [
        _t(
          'Pale Harbour/2004 Tidewater/01 Low Light.flac',
          title: 'Low Light',
          artist: 'Pale Harbour',
          albumArtist: 'Pale Harbour',
          album: 'Tidewater',
        ),
        _t(
          'Various Artists/2001 Summer Static/01 Undertow.flac',
          title: 'Undertow',
          track: 1,
          artist: 'Pale Harbour',
          albumArtist: 'Various Artists',
          album: 'Summer Static',
        ),
        _t(
          'Various Artists/2001 Summer Static/02 Ripple.flac',
          title: 'Ripple',
          track: 2,
          artist: 'Glass Fox',
          albumArtist: 'Various Artists',
          album: 'Summer Static',
        ),
        _t(
          'Compilations/2003 Night Drive/01 Drift.mp3',
          title: 'Drift',
          track: 1,
          artist: 'Neon Saint',
          album: 'Night Drive',
          compilation: true,
          format: 'MP3',
        ),
        _t(
          'Compilations/2003 Night Drive/02 Overpass.flac',
          title: 'Overpass',
          track: 2,
          artist: 'Low Orbit',
          album: 'Night Drive',
          compilation: true,
        ),
      ];

      test('"Various Artists" and the flag both make a compilation', () async {
        final names = [
          for (final a in (await _lib(tracks())).compilations()) a.name,
        ];
        expect(names, unorderedEquals(['Summer Static', 'Night Drive']));
      });

      test(
        '"Various Artists" in any letter case makes a compilation',
        () async {
          // Firmware: ieq_str(aa, "Various Artists"), app/library/lib_build.c.
          final lib = await _lib([
            _t(
              'VA/2001 Mix/01 One.flac',
              title: 'One',
              artist: 'Glass Fox',
              albumArtist: 'various artists',
              album: 'Mix',
            ),
          ]);
          expect([for (final a in lib.compilations()) a.name], ['Mix']);
          expect(lib.artists(), isEmpty);
        },
      );

      test('compilation albums keep all their tracks', () async {
        final comps = {
          for (final a in (await _lib(tracks())).compilations()) a.name: a,
        };
        expect(_titles(comps['Summer Static']!), ['Undertow', 'Ripple']);
        expect(_titles(comps['Night Drive']!), ['Drift', 'Overpass']);
      });

      test(
        'contributors are not artists for their compilation tracks',
        () async {
          final lib = (await _lib(tracks()));
          expect(lib.artists(), ['Pale Harbour']);
          expect(lib.artists(), isNot(contains('Various Artists')));
          expect(lib.artists(), isNot(contains('Glass Fox')));
          expect(lib.artists(), isNot(contains('Neon Saint')));
        },
      );

      test("a compilation is not under a contributor's albums", () async {
        final albums = (await _lib(tracks())).albumsOf('Pale Harbour');
        expect([for (final a in albums) a.name], ['Tidewater']);
      });

      test('a compilation track still shows its own artist', () async {
        final songs = {
          for (final s in (await _lib(tracks())).songs()) s.title: s,
        };
        expect(songs['Drift']!.artist, 'Neon Saint');
        expect(songs['Ripple']!.artist, 'Glass Fox');
      });
    });

    group('unplayable files', () {
      // Hoot's tags stand for the library model only: the scanner does not
      // open Ogg or Opus files (nor does the firmware), so a real one gets
      // the untagged fallback of owner decision 15.
      List<TrackInput> tracks() => [
        _t(
          'Night Owl/2020 Calls/01 Hoot.opus',
          title: 'Hoot',
          artist: 'Night Owl',
          album: 'Calls',
          track: 1,
          playable: false,
          format: 'Opus',
        ),
        _t(
          'Night Owl/2020 Calls/02 Screech.flac',
          title: 'Screech',
          artist: 'Night Owl',
          album: 'Calls',
          track: 2,
        ),
        _bare('Broken Things/2020 Damage/01 Zero Bytes.flac', playable: false),
      ];

      test('a file that cannot play is listed, greyed, never hidden', () async {
        final songs = {
          for (final s in (await _lib(tracks())).songs()) s.title: s,
        };
        // The untagged file's title is its file name (owner decision 15).
        expect(
          songs.keys,
          containsAll(['Hoot', 'Screech', '01 Zero Bytes.flac']),
        );
        expect(songs['Hoot']!.greyed, isTrue);
        expect(songs['01 Zero Bytes.flac']!.greyed, isTrue);
        expect(songs['Screech']!.greyed, isFalse);
        expect(songs['Screech']!.reason, isNull);
      });

      test(
        'the reason at the right says the format: "Opus, can\'t play"',
        () async {
          final songs = {
            for (final s in (await _lib(tracks())).songs()) s.title: s,
          };
          expect(songs['Hoot']!.reason, "Opus, can't play");
        },
      );

      test('an album lists its unplayable tracks, greyed, in place', () async {
        final album = (await _lib(tracks())).albumsOf('Night Owl').single;
        expect(_titles(album), ['Hoot', 'Screech']);
        expect([for (final t in album.tracks) t.greyed], [true, false]);
      });
    });

    group('sort order of the lists', () {
      // `alb_cmp` (app/library/lib_build.c): an album's songs are in disc,
      // then track number order (none counts as 0, so first), then the
      // title's collation key; the file names' order plays no part.
      test('an album without track numbers is in title order', () async {
        final lib = await _lib([
          for (final (file, title) in [
            ('01 Zulu.flac', 'Zulu'),
            ('02 The Mike.flac', 'The Mike'),
            ('03 alpha.flac', 'alpha'),
            ('04 Écho.flac', 'Écho'),
          ])
            _t('A/2000 B/$file', title: title, artist: 'A', album: 'B'),
        ]);
        expect(_titles(lib.albumsOf('A').single), [
          'alpha',
          'Écho',
          'The Mike',
          'Zulu',
        ]);
      });

      test('songs without a track number come before numbered ones', () async {
        final lib = await _lib([
          _t(
            'A/2000 B/01 Bee.flac',
            title: 'Bee',
            artist: 'A',
            album: 'B',
            track: 1,
          ),
          _t('A/2000 B/Zed.flac', title: 'Zed', artist: 'A', album: 'B'),
          _t(
            'A/2000 B/Ant.flac',
            title: 'Ant',
            artist: 'A',
            album: 'B',
            track: 2,
          ),
          _t('A/2000 B/Yak.flac', title: 'Yak', artist: 'A', album: 'B'),
        ]);
        expect(_titles(lib.albumsOf('A').single), ['Yak', 'Zed', 'Bee', 'Ant']);
      });

      test('Songs sort by the collation rule', () async {
        final lib = await _lib([
          for (final title in ['Ash', 'The Apple', '7 Seconds', '#1 Fan'])
            _t('A/2000 B/$title.flac', title: title, artist: 'A', album: 'B'),
        ]);
        expect(
          [for (final s in lib.songs()) s.title],
          ['#1 Fan', '7 Seconds', 'The Apple', 'Ash'],
        );
      });

      test('Artists sort by the collation rule, sort tag aside', () async {
        final lib = await _lib([
          for (final artist in ['Zed', 'émile', 'The Band', '10cc', '!!!'])
            _t(
              '$artist/2000 X/01 Y.flac',
              title: 'Y',
              artist: artist,
              albumArtist: artist,
              album: 'X',
            ),
        ]);
        expect(lib.artists(), ['!!!', '10cc', 'The Band', 'émile', 'Zed']);
      });
    });
  });
}
