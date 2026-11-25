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
// WIRING: set the wired* values below to adapters over the app's code; each
// group's skip lifts by itself once its function is set.
@Tags(['spec'])
library;

import 'package:flutter_test/flutter_test.dart';

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

typedef LibraryFromTracks = LibraryView Function(List<TrackInput> tracks);

// WIRE: replace null with adapters over the app's code.
final LibraryFromTracks? wiredLibraryFromTracks = null;

Object _skipIf(Object? wired, String item) =>
    wired == null ? 'awaiting implementation of $item' : false;

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

LibraryView _lib(List<TrackInput> tracks) => wiredLibraryFromTracks!(tracks);

List<String> _titles(AlbumView a) => [for (final t in a.tracks) t.title];

void main() {
  group(
    'library rules',
    skip: _skipIf(
      wiredLibraryFromTracks,
      'the library model (scan results '
      'to browsing lists)',
    ),
    () {
      group('untagged files (owner decision 15)', () {
        test('only the album comes from the path', () {
          final lib = _lib([_bare('Marrow/1977 Bone Dry/01 Dust.flac')]);
          expect(lib.artists(), ['Unknown Artist']);
          final albums = lib.albumsOf('Unknown Artist');
          expect(albums, hasLength(1));
          expect(albums.single.name, '1977 Bone Dry');
          expect(albums.single.year, isNull);
          expect(albums.single.tracks.single.track, isNull);
          expect(albums.single.tracks.single.title, '01 Dust.flac');
          expect(lib.songs().single.title, '01 Dust.flac');
        });

        test('a ", The" folder name is not changed', () {
          final lib = _lib([
            _bare('Hollow Pines, The/Cedar, The/01 Frost.flac'),
          ]);
          expect(lib.artists(), ['Unknown Artist']);
          expect(lib.albumsOf('Unknown Artist').single.name, 'Cedar, The');
        });

        test('tags, where present, are used', () {
          final lib = _lib([
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
        test('disc number tags over two folders make one album', () {
          final lib = _lib([
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

        test('"(Disc n)" in the album tags: two albums, as on the device', () {
          final lib = _lib([
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
        });

        test('"(Disc n)" in untagged folder names: two albums', () {
          final lib = _lib([
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

        test('the same album name by two artists is two albums', () {
          final lib = _lib([
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
            artist: 'Pale Harbour',
            albumArtist: 'Various Artists',
            album: 'Summer Static',
          ),
          _t(
            'Various Artists/2001 Summer Static/02 Ripple.flac',
            title: 'Ripple',
            artist: 'Glass Fox',
            albumArtist: 'Various Artists',
            album: 'Summer Static',
          ),
          _t(
            'Compilations/2003 Night Drive/01 Drift.mp3',
            title: 'Drift',
            artist: 'Neon Saint',
            album: 'Night Drive',
            compilation: true,
            format: 'MP3',
          ),
          _t(
            'Compilations/2003 Night Drive/02 Overpass.flac',
            title: 'Overpass',
            artist: 'Low Orbit',
            album: 'Night Drive',
            compilation: true,
          ),
        ];

        test('"Various Artists" and the flag both make a compilation', () {
          final names = [for (final a in _lib(tracks()).compilations()) a.name];
          expect(names, unorderedEquals(['Summer Static', 'Night Drive']));
        });

        test('"Various Artists" in any letter case makes a compilation', () {
          // Firmware: ieq_str(aa, "Various Artists"), app/library/lib_build.c.
          final lib = _lib([
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
        });

        test('compilation albums keep all their tracks', () {
          final comps = {
            for (final a in _lib(tracks()).compilations()) a.name: a,
          };
          expect(_titles(comps['Summer Static']!), ['Undertow', 'Ripple']);
          expect(_titles(comps['Night Drive']!), ['Drift', 'Overpass']);
        });

        test('contributors are not artists for their compilation tracks', () {
          final lib = _lib(tracks());
          expect(lib.artists(), ['Pale Harbour']);
          expect(lib.artists(), isNot(contains('Various Artists')));
          expect(lib.artists(), isNot(contains('Glass Fox')));
          expect(lib.artists(), isNot(contains('Neon Saint')));
        });

        test("a compilation is not under a contributor's albums", () {
          final albums = _lib(tracks()).albumsOf('Pale Harbour');
          expect([for (final a in albums) a.name], ['Tidewater']);
        });

        test('a compilation track still shows its own artist', () {
          final songs = {for (final s in _lib(tracks()).songs()) s.title: s};
          expect(songs['Drift']!.artist, 'Neon Saint');
          expect(songs['Ripple']!.artist, 'Glass Fox');
        });
      });

      group('unplayable files', () {
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
          _bare(
            'Broken Things/2020 Damage/01 Zero Bytes.flac',
            playable: false,
          ),
        ];

        test('a file that cannot play is listed, greyed, never hidden', () {
          final songs = {for (final s in _lib(tracks()).songs()) s.title: s};
          expect(songs.keys, containsAll(['Hoot', 'Screech', 'Zero Bytes']));
          expect(songs['Hoot']!.greyed, isTrue);
          expect(songs['Zero Bytes']!.greyed, isTrue);
          expect(songs['Screech']!.greyed, isFalse);
          expect(songs['Screech']!.reason, isNull);
        });

        test(
          'the reason at the right says the format: "Opus, can\'t play"',
          () {
            final songs = {for (final s in _lib(tracks()).songs()) s.title: s};
            expect(songs['Hoot']!.reason, "Opus, can't play");
          },
        );

        test('an album lists its unplayable tracks, greyed, in place', () {
          final album = _lib(tracks()).albumsOf('Night Owl').single;
          expect(_titles(album), ['Hoot', 'Screech']);
          expect([for (final t in album.tracks) t.greyed], [true, false]);
        });
      });

      group('sort order of the lists', () {
        test('Songs sort by the collation rule', () {
          final lib = _lib([
            for (final title in ['Ash', 'The Apple', '7 Seconds', '#1 Fan'])
              _t('A/2000 B/$title.flac', title: title, artist: 'A', album: 'B'),
          ]);
          expect(
            [for (final s in lib.songs()) s.title],
            ['#1 Fan', '7 Seconds', 'The Apple', 'Ash'],
          );
        });

        test('Artists sort by the collation rule, sort tag aside', () {
          final lib = _lib([
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
    },
  );
}
