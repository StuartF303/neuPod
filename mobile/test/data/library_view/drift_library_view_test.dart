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
// - lib_build.c (`key_tie`, `ent_cmp`, `key_cmp`, `alb_cmp`): songs with
//   equal collation keys fall back to walk order (the track id is its place
//   in the walk); an album's tracks sort by disc<<16|track (an untagged 0
//   first), then the key, then walk order.
// - lib_build.c (`stage_entity`): artists and albums with equal whole keys
//   are folded into one row named after the first in walk order; an album's
//   group is its artist's entity id, or LIB_NONE for a compilation ("Various
//   Artists" in any ASCII case, `index_file`), which makes no artist.
// - Walk order is the app's model (a lead decision): depth first, each
//   folder's entries in byte order of their names.
// - lib_build.c `cut()` / POOL_STR: a title is stored cut to 64 bytes, so
//   the key is built from at most 63 bytes of it.

import 'dart:convert';
import 'dart:io';

import 'package:drift/drift.dart' show Value;
import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/data/scan/tag_reader.dart';
import 'package:neupod/domain/library/collation.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/domain/library_view/library_view.dart';

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
      // Zero-padded, so walk order (byte order of the names) is the order
      // they were added in.
      relPath: path ?? 'Dir/f${'$n'.padLeft(4, '0')}.flac',
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

  test('equal keys: songs keep both in walk order, entities fold into one '
      'named by the first', () async {
    // "Ångström" and "Angstrom" fold to the same key. Songs are rows of
    // their own (`key_cmp`: key, then the id, its place in the walk); artists
    // and albums are one row each, named after the run's first entry in walk
    // order (`ent_cmp`, `stage_entity`).
    expect(collateKey('Ångström'), collateKey('Angstrom'));
    await add('Ångström', artist: 'Ångström', album: 'Ångström');
    await add('Angstrom', artist: 'Angstrom', album: 'Angstrom');
    expect((await view.songs()).map((s) => s.title), ['Ångström', 'Angstrom']);
    expect((await view.artists()).map((a) => a.name), ['Ångström']);
    expect((await view.albums()).map((a) => a.title), ['Ångström']);

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
    expect((await v2.artists()).map((a) => a.name), ['Angstrom']);
    expect((await v2.albums()).map((a) => a.title), ['Angstrom']);
    final album = (await v2.albums()).single;
    expect(album.songCount, 2);
    expect((await v2.artists()).single.songCount, 2);
  });

  // The firmware's own builder on the same tracks, walked in the same order
  // (the C creates the files in the app's walk order, whatever the order of
  // the lines; the test writes the rows in line order):
  // fixtures/<name>.tsv is given to scripts/app/library_entities.c (lib_build()
  // + lib_list_row() on a FatFs image) and fixtures/<name>.expected is what it
  // printed. Regenerate with mobile/tool/library_entities/gen_library_entities.sh
  // (CI checks the fixtures still match the firmware).
  //
  // fold.tsv: names with equal collation keys and different text ("The
  // Beatles" / "Beatles" / "BEATLES", "AC/DC" / "Ac/Dc", "Ångström" /
  // "Angstrom"); two artists that differ only after 63 bytes (meta.h `char
  // artist[64]`); "Greatest Hits" under four artists, two of them one.
  // folders.tsv: untagged albums named after their folder (`index_file`,
  // ADR 0017 D5), "Live" under three artists, "live" folding into "Live", a
  // file at the root of /Music, a tag equal to a folder's name.
  // compilation.tsv: "various artists", "Various Artists" and "VARIOUS
  // ARTISTS" are compilations (no artist; their albums last in the album
  // table, so after Abba's and Zed's "Hits" in the Albums list, and "hits"
  // folding into the compilation "Hits"); "Various Artist" is an artist.
  // walk.tsv: lines out of walk order, so the first in the walk is not the
  // first row written: "The Beatles" (a/2.flac) names the artist although
  // "Beatles" (b/1.flac) and "BEATLES" ("a b/1.flac": the folder "a" comes
  // before "a b", though ' ' < '/') were written first; "Live" (a/3.flac)
  // names the album over "live" (b/2.flac); two songs called "Same" come in
  // walk order. The C creates the files in walk order (`walk_cmp`), so its
  // own walk is the app's model.
  for (final name in ['fold', 'folders', 'compilation', 'walk']) {
    test(
      '$name: artists, albums and songs as the firmware builds them',
      () async {
        final fx = _EntityFixture.read(name);
        for (final t in fx.tracks) {
          await add(
            t.title,
            path: t.path,
            artist: t.artist,
            album: t.album,
            trackNo: t.track,
          );
        }
        // The firmware holds an artist's text in 63 bytes and shows it so; the
        // app shows the whole text (only the keys follow the device).
        expect([
          for (final a in await view.artists())
            [_held(a.name), '${a.albumCount}'],
        ], fx.lists['artists']);
        // A compilation's list row has no artist (`lib_list_row`); the app
        // gives it "Various Artists" for its header (`album_header`).
        String albumArtist(Album a) => a.artistId == null
            ? (a.compilation && a.artist == 'Various Artists' ? '' : '?')
            : _held(a.artist);
        expect([
          for (final a in await view.albums())
            [a.title, albumArtist(a), '${a.songCount}'],
        ], fx.lists['albums list']);
        expect([
          for (final a in await view.albumsForCoverFlow())
            [a.title, albumArtist(a)],
        ], fx.lists['album table']);
        expect([
          for (final s in await view.songs())
            [s.title, _held(s.artist), s.album],
        ], fx.lists['songs']);
      },
    );
  }

  test('a folded artist and album hold every song of the run', () async {
    final fx = _EntityFixture.read('fold');
    for (final t in fx.tracks) {
      await add(
        t.title,
        path: t.path,
        artist: t.artist,
        album: t.album,
        trackNo: t.track,
      );
    }
    final artists = await view.artists();
    final beatles = artists.firstWhere((a) => a.name == 'The Beatles');
    expect(beatles.songCount, 3);
    expect((await view.albumsOfArtist(beatles.id)).map((a) => a.title), [
      'Abbey Road',
      'Help!',
    ]);
    expect((await view.songsOfArtist(beatles.id)).map((s) => s.title), [
      'Come Together',
      'Something',
      'Help',
    ]);
    final abbey = (await view.albums()).first;
    expect(abbey.title, 'Abbey Road');
    expect(abbey.artistId, beatles.id);
    expect((await view.album(abbey.id))!.songs.map((s) => s.title), [
      'Come Together',
      'Something',
    ]);
    final songs = await view.songs();
    expect(songs.firstWhere((s) => s.title == 'Something').albumId, abbey.id);
    // The long artist: one entity, named by its first track; both songs keep
    // their own text.
    final long = artists.firstWhere((a) => a.name.startsWith('LLL'));
    expect(long.name.substring(63), 'x');
    expect(long.songCount, 2);
    expect(
      songs.where((s) => s.album == 'Long').map((s) => s.artist.substring(63)),
      ['x', 'y'],
    );
  });

  test('a load in flight when the database changes is not kept', () async {
    await add('One', artist: 'A', album: 'X');
    final first = view.songs();
    await add('Two', artist: 'A', album: 'X');
    await first; // whatever it read
    await Future<void>.delayed(Duration.zero);
    expect(await view.songs(), hasLength(2));
  });

  test('a failed load is not kept', () async {
    await add('One', artist: 'A', album: 'X');
    await db.customStatement('ALTER TABLE tracks RENAME TO tracks_away');
    await expectLater(view.songs(), throwsA(anything));
    await db.customStatement('ALTER TABLE tracks_away RENAME TO tracks');
    expect(await view.songs(), hasLength(1));
  });

  test('a change to the sources is seen too', () async {
    await add('Rooted', path: 'Rooted.flac');
    expect((await view.songs()).single.album, 'Music');
    await (db.update(db.sources)..where((s) => s.id.equals(src))).write(
      const SourcesCompanion(displayName: Value('Tunes')),
    );
    await Future<void>.delayed(Duration.zero);
    expect((await view.songs()).single.album, 'Tunes');
  });

  test('after dispose every call reads the database again', () async {
    await add('One', artist: 'A', album: 'X');
    expect(await view.songs(), hasLength(1));
    await view.dispose();
    await add('Two', artist: 'A', album: 'X');
    expect(await view.songs(), hasLength(2));
    await view.dispose(); // twice is harmless
  });

  test('titles are stored cut at 63 bytes: the rest does not order', () async {
    final a = 'a' * 70;
    await add('${a}x');
    await add('${a}b');
    // Same stored key, so walk order, although "b" < "x".
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
    // A file at the root of the source: lib_build.c:483-484 takes the leaf of
    // c->path, which for a root file is the music folder itself
    // (LIB_ROOT_DEFAULT "/Music" gives "Music"), so the source's own name.
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

  test('compilations are their own list and make no artist', () async {
    final fx = _EntityFixture.read('compilation');
    for (final t in fx.tracks) {
      await add(
        t.title,
        path: t.path,
        artist: t.artist,
        album: t.album,
        trackNo: t.track,
      );
    }
    expect((await view.artists()).map((a) => a.name), [
      'Abba',
      'Various Artist',
      'Zed',
    ]);
    final comps = await view.compilations();
    expect(comps.map((a) => (a.title, a.songCount)), [('Hits', 2), ('Mix', 1)]);
    for (final a in comps) {
      expect(a.compilation, isTrue);
      expect(a.artistId, isNull);
      expect(a.artist, 'Various Artists');
    }
    final detail = (await view.album(comps.first.id))!;
    // Each song keeps its own artist text.
    expect(detail.songs.map((s) => s.artist), [
      'various artists',
      'Various Artists',
    ]);
    expect(detail.songs.every((s) => s.albumId == comps.first.id), isTrue);
  });

  test('walk order: depth first, names in byte order', () {
    expect(walkCompare('a/2.flac', 'b/1.flac'), lessThan(0));
    expect(walkCompare('a/x.flac', 'a b/x.flac'), lessThan(0));
    expect(walkCompare('a b/x.flac', 'a/x.flac'), greaterThan(0));
    expect(walkCompare('B/1.flac', 'a/1.flac'), lessThan(0)); // bytes
    expect(walkCompare('Echo.flac', 'Q/Live/01.flac'), lessThan(0));
    expect(walkCompare('Z/1.flac', 'a.flac'), lessThan(0));
    expect(walkCompare('/a/1.flac', 'a/1.flac'), 0); // empty segments
  });

  test('artists and albums are keyed on the firmware\'s text: two emoji '
      'artists are one', () async {
    // ID3v2.3 UTF-16 "😀 Band" and "😃 Band": the firmware stores both as
    // "? Band" (meta.c), so it has one artist and one album. The vectors are
    // checked against the compiled C in tag_key_vectors_test.dart.
    const dir = 'test/fixtures/tag_keys';
    AudioTags read(String name) =>
        readTags(BytesSource(File('$dir/$name').readAsBytesSync()), 'mp3');
    // b is written first but walks second: the names come from a.
    for (final (path, name) in [
      ('b/1.mp3', 'utf16_band_b.mp3'),
      ('a/1.mp3', 'utf16_band_a.mp3'),
    ]) {
      final t = read(name);
      await addTrack(
        db,
        sourceId: src,
        relPath: path,
        title: t.title,
        artist: t.artist,
        album: t.album,
        artistKeyText: t.artistKeyText,
        albumKeyText: t.albumKeyText,
      );
    }
    final artist = (await view.artists()).single;
    expect(artist.name, '\u{1F600} Band');
    expect(artist.songCount, 2);
    final album = (await view.albums()).single;
    expect(album.title, startsWith('\u{1F600} Hits'));
    expect(album.songCount, 2);
    expect((await view.songs()).map((s) => s.artist), [
      '\u{1F603} Band', // "Grin"
      '\u{1F600} Band', // "Smile"
    ]);
  });

  test('a row from before schema version 2 is keyed on its displayed text '
      'until a rescan', () async {
    await addTrack(
      db,
      sourceId: src,
      relPath: 'a/1.flac',
      title: 'One',
      artist: 'The Beatles',
      album: 'Help!',
      legacy: true,
    );
    await addTrack(
      db,
      sourceId: src,
      relPath: 'b/1.flac',
      title: 'Two',
      artist: 'Beatles',
      album: 'help!',
    );
    await addTrack(
      db,
      sourceId: src,
      relPath: 'c/1.flac',
      title: 'Three',
      artist: 'VARIOUS ARTISTS',
      album: 'Mix',
      legacy: true,
    );
    expect((await view.artists()).single.name, 'The Beatles');
    expect((await view.albums()).map((a) => (a.title, a.songCount)), [
      ('Help!', 2),
      ('Mix', 1),
    ]);
    expect((await view.compilations()).single.title, 'Mix');
  });

  test('the firmware\'s text decides: no artist or album there is Unknown '
      'Artist and the folder', () async {
    // A FLAC whose comment block is over the firmware's scratch: the app
    // shows the tags, the firmware never read them.
    await addTrack(
      db,
      sourceId: src,
      relPath: 'Live/1.flac',
      title: 'Big',
      artist: 'Shown',
      album: 'Shown Album',
      artistKeyText: const [],
      albumKeyText: const [],
    );
    await addTrack(
      db,
      sourceId: src,
      relPath: 'Live/2.flac',
      title: 'Plain',
      artist: 'Unknown Artist',
      album: 'Live',
    );
    final artist = (await view.artists()).single;
    expect(artist.songCount, 2);
    expect((await view.albums()).single.songCount, 2);
  });

  test('a leading slash in a path does not give an empty album', () async {
    await addTrack(db, sourceId: src, relPath: '/Rooted.flac', title: 'R');
    await addTrack(db, sourceId: src, relPath: '/Dir/In.flac', title: 'I');
    final songs = {for (final s in await view.songs()) s.title: s.album};
    expect(songs, {'I': 'Dir', 'R': 'Music'});
  });
}

/// Text as the firmware holds a tag: at most 63 bytes (meta.h, `cut()`).
String _held(String s) =>
    utf8.decode(firmwareCut(utf8.encode(s), 64), allowMalformed: true);

/// `fixtures/<name>.tsv` (the tracks, in the order the test writes them) and
/// `fixtures/<name>.expected` (what scripts/app/library_entities.c printed
/// for them).
final class _EntityFixture {
  _EntityFixture(this.tracks, this.lists);

  factory _EntityFixture.read(String name) {
    const dir = 'test/data/library_view/fixtures';
    const root = '/Music/';
    final tracks = <_FixtureTrack>[];
    for (final line in File('$dir/$name.tsv').readAsLinesSync()) {
      if (line.isEmpty) continue;
      final f = line.split('\t');
      if (!f[0].startsWith(root)) {
        throw FormatException('not under /Music', line);
      }
      tracks.add((
        path: f[0].substring(root.length),
        title: f[1],
        artist: f[2],
        album: f[3],
        track: int.parse(f[4]),
      ));
    }
    final lists = <String, List<List<String>>>{};
    var at = <List<String>>[];
    for (final line in File('$dir/$name.expected').readAsLinesSync()) {
      if (line.startsWith('# ')) {
        // "# artists (name, albums)" -> "artists"
        at = lists[line.substring(2).split(' (').first] = [];
      } else if (line.isNotEmpty) {
        at.add(line.split('\t'));
      }
    }
    return _EntityFixture(tracks, lists);
  }

  final List<_FixtureTrack> tracks;
  final Map<String, List<List<String>>> lists;
}

typedef _FixtureTrack = ({
  String path,
  String title,
  String artist,
  String album,
  int track,
});
