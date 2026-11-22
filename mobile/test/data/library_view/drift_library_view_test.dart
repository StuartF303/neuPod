// SPDX-License-Identifier: MPL-2.0

// The library view over a seeded in-memory drift database. Expectations come
// from the specification (docs/app/README.md, The library: the sort rule is
// the firmware's collation, owner decision 11: leading "The "/"A "/"An "
// skipped, accents folded, symbols, then digits, then letters) and from the
// LibraryView port's documentation, not from the adapter's code.

import 'package:drift/drift.dart' hide isNull, isNotNull;
import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/domain/library/collation.dart';
import 'package:neupod/domain/library/track.dart';

void main() {
  late LibraryDatabase db;
  late DriftLibraryView view;
  var n = 0;

  setUp(() async {
    db = LibraryDatabase(NativeDatabase.memory());
    view = DriftLibraryView(db);
    await db
        .into(db.sources)
        .insert(
          SourcesCompanion.insert(
            kind: 'local_folder',
            uriOrBookmark: 'm',
            displayName: 'Music',
          ),
        );
  });
  tearDown(() => db.close());

  Future<void> add(
    String title, {
    String artist = '',
    String album = '',
    int trackNo = 0,
    int durationMs = 0,
    int? reason,
    String codec = 'flac',
  }) async {
    n++;
    await db
        .into(db.tracks)
        .insert(
          TracksCompanion.insert(
            sourceId: 1,
            relPath: 'f$n.$codec',
            uri: 'file:///music/f$n.$codec',
            size: 1,
            mtime: 1,
            codec: codec,
            title: title,
            titleSort: '',
            titleSortKey: songSortKey(
              titleText: title.codeUnits,
              titleSortText: const [],
              fileName: 'f$n',
            ),
            artist: artist,
            album: album,
            trackNo: trackNo,
            durationMs: durationMs,
            sampleRate: 44100,
            bitDepth: 16,
            reasonCode: Value(reason),
            scanVersion: 1,
          ),
        );
  }

  test('an empty database gives empty lists and no detail', () async {
    expect(await view.songs(), isEmpty);
    expect(await view.artists(), isEmpty);
    expect(await view.albums(), isEmpty);
    expect(await view.albumsForCoverFlow(), isEmpty);
    expect(await view.genres(), isEmpty);
    expect(await view.compilations(), isEmpty);
    expect(await view.song('1'), isNull);
    expect(await view.album('album:x|y'), isNull);
  });

  test('songs come in the firmware order, with their letters', () async {
    // The golden table's kinds: symbols, digits, articles, accents.
    for (final t in [
      'Zed',
      '99 Problems',
      'The Apple',
      '!Aktion',
      'Émile',
      'abba',
      'A Perfect Circle',
      'Beta',
    ]) {
      await add(t);
    }
    final songs = await view.songs();
    expect(songs.map((s) => s.title), [
      '!Aktion',
      '99 Problems',
      'abba',
      'The Apple',
      'Beta',
      'Émile',
      'A Perfect Circle',
      'Zed',
    ]);
    expect(songs.map((s) => s.sortLetter), [
      '#',
      '#',
      'A',
      'A',
      'B',
      'E',
      'P',
      'Z',
    ]);
  });

  test('a song carries its fields; zero means not tagged', () async {
    await add(
      'Salt Road',
      artist: 'Tern',
      album: 'Coast',
      trackNo: 3,
      durationMs: 215000,
    );
    await add('No Tags');
    final s = (await view.songs()).firstWhere((s) => s.title == 'Salt Road');
    expect(s.artist, 'Tern');
    expect(s.album, 'Coast');
    expect(s.track, 3);
    expect(s.duration, const Duration(seconds: 215));
    expect(s.uri, startsWith('file:///music/'));
    expect(s.playable, isTrue);
    expect(await view.song(s.id), isNotNull);
    expect((await view.song(s.id))!.title, 'Salt Road');
    final bare = (await view.songs()).firstWhere((s) => s.title == 'No Tags');
    expect(bare.track, isNull);
    expect(bare.duration, isNull);
  });

  test('a file that could not be read is greyed with a reason', () async {
    await add('Broken', reason: UnreadableReason.damaged.code);
    await add('Elsewhere', reason: UnreadableReason.notDownloaded.code);
    final songs = {for (final s in await view.songs()) s.title: s};
    expect(songs['Broken']!.playable, isFalse);
    expect(songs['Broken']!.unplayableReason, 'Damaged');
    expect(songs['Elsewhere']!.unplayableReason, 'Not downloaded');
  });

  test('artists and albums are grouped and sorted by collation', () async {
    await add('One', artist: 'The Beatles', album: 'Abbey Road', trackNo: 2);
    await add('Two', artist: 'The Beatles', album: 'Abbey Road', trackNo: 1);
    await add('Three', artist: 'The Beatles', album: 'Help!', trackNo: 1);
    await add('Four', artist: 'Abba', album: 'Arrival', trackNo: 1);
    await add('Five', artist: 'Émilie', album: 'Zèbre', trackNo: 1);
    await add('Six', artist: '2Pac', album: 'All Eyez', trackNo: 1);

    final artists = await view.artists();
    // Digits before letters; "The Beatles" sorts under B, after Abba.
    expect(artists.map((a) => a.name), [
      '2Pac',
      'Abba',
      'The Beatles',
      'Émilie',
    ]);
    expect(artists.map((a) => a.sortLetter), ['#', 'A', 'B', 'E']);
    final beatles = artists[2];
    expect(beatles.albumCount, 2);
    expect(beatles.songCount, 3);

    expect((await view.albums()).map((a) => a.title), [
      'Abbey Road',
      'All Eyez',
      'Arrival',
      'Help!',
      'Zèbre',
    ]);
    // Cover Flow: by artist, then title.
    expect((await view.albumsForCoverFlow()).map((a) => a.artist), [
      '2Pac',
      'Abba',
      'The Beatles',
      'The Beatles',
      'Émilie',
    ]);
    expect((await view.albumsOfArtist(beatles.id)).map((a) => a.title), [
      'Abbey Road',
      'Help!',
    ]);
    expect((await view.songsOfArtist(beatles.id)).map((s) => s.title), [
      'Two',
      'One',
      'Three',
    ]);
    expect(await view.artist(beatles.id), isNotNull);
  });

  test('an album lists its songs in track order with its count', () async {
    await add(
      'Second',
      artist: 'A',
      album: 'Al',
      trackNo: 2,
      durationMs: 60000,
    );
    await add('First', artist: 'A', album: 'Al', trackNo: 1, durationMs: 90000);
    await add('Loose', artist: 'A', album: 'Al', durationMs: 30000);
    final album = (await view.albums()).single;
    expect(album.songCount, 3);
    expect(album.totalTime, const Duration(seconds: 180));
    final detail = (await view.album(album.id))!;
    expect(detail.songs.map((s) => s.title), ['First', 'Second', 'Loose']);
    expect(detail.hasDiscHeadings, isFalse);
    expect(detail.songs.first.albumId, album.id);
  });

  test('missing artist or album show as unknown in the lists', () async {
    await add('Lone');
    expect((await view.artists()).single.name, unknownArtist);
    expect((await view.albums()).single.title, unknownAlbum);
  });

  test('every list sorts as collateCompare does', () async {
    const names = [
      'the zebra',
      'Ångström',
      'Ångstrom',
      '#hash',
      '7 Seconds',
      'Æon',
      'An Orchard',
      'a b',
      'ab',
    ];
    for (final x in names) {
      await add(x, artist: x, album: x);
    }
    int byKey(String a, String b) => collateCompare(a, null, b, null);
    final expected = [...names]..sort(byKey);
    expect(
      (await view.songs()).map((s) => collateKey(s.title)),
      expected.map(collateKey),
    );
    expect(
      (await view.artists()).map((a) => collateKey(a.name)),
      expected.map(collateKey),
    );
    expect(
      (await view.albums()).map((a) => collateKey(a.title)),
      expected.map(collateKey),
    );
  });
}
