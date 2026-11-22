// SPDX-License-Identifier: MPL-2.0

// The library view over a seeded in-memory drift database. Expectations come
// from the specification (docs/app/README.md, The library: the sort rule is
// the firmware's collation, owner decision 11) and from the firmware's own
// index builder (app/library/lib_build.c), not from the adapter's code:
//
// - lib_build.c:360-362 (`index_file`): a track's artist is the tag or
//   "Unknown Artist"; its album is the tag or the leaf (the containing
//   folder's name); the album artist is the album artist tag or the artist.
//   ADR 0017 D5 rejects "Unknown Album".
// - lib_build.c:185-226 (`key_tie`, `ent_cmp`, `key_cmp`, `alb_cmp`): equal
//   collation keys fall back to scan order (the id); an album's tracks sort
//   by disc<<16|track (an untagged 0 first), then the key, then the id.
// - lib_build.c `cut()` / POOL_STR: a title is stored cut to 64 bytes, so
//   the key is built from at most 63 bytes of it.

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/domain/library/collation.dart';
import 'package:neupod/domain/library/track.dart';

import 'seed.dart';

void main() {
  late LibraryDatabase db;
  late DriftLibraryView view;
  late int src;
  var n = 0;

  setUp(() async {
    db = LibraryDatabase(NativeDatabase.memory());
    view = DriftLibraryView(db);
    src = await addSource(db, name: 'Music');
  });
  tearDown(() async {
    await view.dispose();
    await db.close();
  });

  Future<void> add(
    String title, {
    String artist = '',
    String album = '',
    String? path,
    String titleSort = '',
    int trackNo = 0,
    int durationMs = 0,
    int? reason,
  }) async {
    n++;
    await addTrack(
      db,
      sourceId: src,
      relPath: path ?? 'Dir/f$n.flac',
      title: title,
      titleSort: titleSort,
      artist: artist,
      album: album,
      trackNo: trackNo,
      durationMs: durationMs,
      reason: reason,
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

  test('symbols, then digits, then letters at their boundaries', () async {
    // '!' and '~' are ASCII symbols (as themselves), '€' another symbol
    // (0x7F + UTF-8, after them), digits 0xB0.., letters 0xC0..
    for (final t in ['a', '9e', '0d', '€c', '~b', '!a']) {
      await add(t);
    }
    expect((await view.songs()).map((s) => s.title), [
      '!a',
      '~b',
      '€c',
      '0d',
      '9e',
      'a',
    ]);
  });

  test('a sort tag orders a title; the file name stands in for none', () async {
    await add('Zebra', titleSort: 'Aardvark');
    await add('Beta');
    // No title: the key is the file name with its extension ("The " skipped),
    // and the row shows the name without it.
    await add('', path: 'Dir/The Wanderer.flac');
    await add('Wander');
    await add('Wanderlust');
    final titles = (await view.songs()).map((s) => s.title).toList();
    expect(titles, [
      'Zebra', // sorts as "Aardvark"
      'Beta',
      'Wander',
      'The Wanderer', // "wanderer.flac" between "wander" and "wanderlust"
      'Wanderlust',
    ]);
  });

  test('equal keys keep scan order, whichever came first', () async {
    // "Ångström" and "Angstrom" fold to the same key.
    expect(collateKey('Ångström'), collateKey('Angstrom'));
    await add('Ångström', artist: 'Ångström', album: 'Ångström');
    await add('Angstrom', artist: 'Angstrom', album: 'Angstrom');
    expect((await view.songs()).map((s) => s.title), ['Ångström', 'Angstrom']);
    expect((await view.artists()).map((a) => a.name), ['Ångström', 'Angstrom']);
    expect((await view.albums()).map((a) => a.title), ['Ångström', 'Angstrom']);

    final other = LibraryDatabase(NativeDatabase.memory());
    addTearDown(other.close);
    final s2 = await addSource(other);
    for (final (i, name) in ['Angstrom', 'Ångström'].indexed) {
      await addTrack(
        other,
        sourceId: s2,
        relPath: 'D/$i.flac',
        title: name,
        artist: name,
        album: name,
      );
    }
    final v2 = DriftLibraryView(other);
    addTearDown(v2.dispose);
    expect((await v2.songs()).map((s) => s.title), ['Angstrom', 'Ångström']);
    expect((await v2.artists()).map((a) => a.name), ['Angstrom', 'Ångström']);
    expect((await v2.albums()).map((a) => a.title), ['Angstrom', 'Ångström']);
  });

  test('titles are stored cut at 63 bytes: the rest does not order', () async {
    final a = 'a' * 70;
    await add('${a}x');
    await add('${a}b');
    // Same stored key, so scan order, although "b" < "x".
    expect((await view.songs()).map((s) => s.title.substring(70)), ['x', 'b']);
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
    expect((await view.song(s.id))!.title, 'Salt Road');
    final bare = (await view.songs()).firstWhere((s) => s.title == 'No Tags');
    expect(bare.track, isNull);
    expect(bare.duration, isNull);
  });

  test('no artist is Unknown Artist; no album is the folder name', () async {
    await add('Lone', path: 'Rock/2001 Nowhere/01 Lone.flac');
    await add('Rooted', path: 'Rooted.flac');
    final songs = {for (final s in await view.songs()) s.title: s};
    expect(songs['Lone']!.artist, unknownArtist);
    expect(songs['Lone']!.album, '2001 Nowhere');
    // A file at the root of the source: the source's own name.
    expect(songs['Rooted']!.album, 'Music');
    expect((await view.artists()).single.name, 'Unknown Artist');
    expect((await view.albums()).map((a) => a.title), [
      '2001 Nowhere',
      'Music',
    ]);
    expect(
      (await view.albums()).any((a) => a.title.contains('Unknown Album')),
      isFalse,
    );
  });

  test('a file that could not be read is greyed with a reason', () async {
    await add('Broken', reason: UnreadableReason.damaged.code);
    await add('Elsewhere', reason: UnreadableReason.notDownloaded.code);
    final songs = {for (final s in await view.songs()) s.title: s};
    expect(songs['Broken']!.playable, isFalse);
    // The firmware's wording (library_service.c: "Can't play: damaged file").
    expect(songs['Broken']!.unplayableReason, 'Damaged file');
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
    expect((await view.artist(beatles.id))!.name, 'The Beatles');
  });

  test('an album lists its songs by track number, none first', () async {
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
    // alb_cmp: disc<<16|track, so an untagged 0 sorts first.
    expect(detail.songs.map((s) => s.title), ['Loose', 'First', 'Second']);
    expect(detail.hasDiscHeadings, isFalse);
    expect(detail.songs.first.albumId, album.id);
  });

  test('the view follows the database after a change', () async {
    await add('One', artist: 'A', album: 'X');
    expect(await view.songs(), hasLength(1));
    await add('Two', artist: 'A', album: 'X');
    // The table update arrives on a later turn of the event loop.
    await Future<void>.delayed(Duration.zero);
    expect(await view.songs(), hasLength(2));
    expect((await view.albums()).single.songCount, 2);
  });
}
