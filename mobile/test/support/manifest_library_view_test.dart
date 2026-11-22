// SPDX-License-Identifier: MPL-2.0

// The gate's library view over the generated test library's manifest: what
// the browsing screens are told, in the owner's folder convention.

import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'manifest_library_view.dart';

import '../../tool/gen_test_library/gen_test_library.dart';

void main() {
  final asset = File('test/support/gate_library/manifest.json')
      .readAsStringSync();
  final view = ManifestLibraryView.parse(asset);

  test('test/support/gate_library/manifest.json is the generator\'s manifest', () {
    final dir = Directory.systemTemp.createTempSync('neupod_gate_manifest');
    addTearDown(() => dir.deleteSync(recursive: true));
    final result = generate(outDir: Directory('${dir.path}/out'));
    expect(
      jsonDecode(result.manifest.readAsStringSync()),
      jsonDecode(asset),
      reason:
          'regenerate: dart run tool/gen_test_library/main.dart --out build/'
          'test_library, then copy manifest.json to test/support/gate_library/',
    );
  });

  test('shows what the manifest expects, not what is hidden or open', () async {
    final songs = await view.songs();
    final titles = songs.map((s) => s.title).toList();
    expect(titles, contains('Low Light'));
    expect(titles, isNot(contains('Secret'))); // under a dot folder
    // The WMA and the untagged root file have no expectation: not shown.
    expect(titles, isNot(contains('Old Format')));
    expect(songs.length, 47);
  });

  test('songs are in the firmware\'s order: symbols, digits, then letters '
      'without articles', () async {
    final titles = (await view.songs()).map((s) => s.title).toList();
    expect(titles.indexOf('(Untitled)'), lessThan(titles.indexOf('1999')));
    expect(titles.indexOf('#1 Fan'), lessThan(titles.indexOf('7 Seconds')));
    expect(titles.indexOf('7 Seconds'), lessThan(titles.indexOf('Ash')));
    // "The Apple" sorts under A.
    expect(titles.indexOf('The Apple'), lessThan(titles.indexOf('Breakwater')));
    // "Þögn" folds to th: after "Thaw".
    expect(titles.indexOf('Thaw'), lessThan(titles.indexOf('Þögn')));
  });

  test('artists: ", The" folders become "The X" sorted under X; '
      'compilations are not artists', () async {
    final names = (await view.artists()).map((a) => a.name).toList();
    expect(names, contains('The Lanterns'));
    expect(names, contains('The Hollow Pines'));
    expect(names, isNot(contains('Various Artists')));
    expect(
      names.indexOf('The Hollow Pines'),
      lessThan(names.indexOf('The Lanterns')),
    );
    expect(names.indexOf('The Lanterns'), lessThan(names.indexOf('Weird Owl')));
    final letters = {
      for (final a in await view.artists()) a.name: a.sortLetter,
    };
    expect(letters['The Lanterns'], 'L');
    expect(letters['!Aktion'], '#');
    expect(letters['10 Tonne Truck'], '#');
    expect(letters['Émilie Côté'], 'E');
  });

  test('a sort tag is used instead of the name (owner decision 11)', () async {
    final artists = await view.artists();
    final owl = artists.firstWhere((a) => a.name == 'Weird Owl');
    expect(owl.sortLetter, 'O'); // ARTISTSORT is "Owl, Weird"
    final names = artists.map((a) => a.name).toList();
    expect(names.indexOf('Weird Owl'), lessThan(names.indexOf('Pale Harbour')));
  });

  test('an artist\'s albums are in year order', () async {
    final artist = (await view.artists()).firstWhere(
      (a) => a.name == 'Pale Harbour',
    );
    final albums = await view.albumsOfArtist(artist.id);
    expect(albums.map((a) => a.title), ['Tidewater', 'Undertow EP']);
    expect(albums.map((a) => a.year), [2004, 2006]);
    expect(albums.first.songCount, 3);
  });

  test('discs: "(Disc n)" in the album name and disc tags merge into one '
      'album with disc sections', () async {
    final albums = await view.albums();
    final long = albums.firstWhere((a) => a.title == 'Long Division');
    final detail = (await view.album(long.id))!;
    expect(detail.hasDiscHeadings, isTrue);
    expect(detail.discs.map((d) => d.number), [1, 2]);
    expect(detail.discs.first.songs.map((s) => s.title), [
      'Opening',
      'Middle Distance',
    ]);

    final hits = albums.firstWhere(
      (a) => a.title == 'Greatest Hits' && a.artist == 'Zed Zone',
    );
    expect(hits.discCount, 2);
    expect(hits.songCount, 3);
    // The same album name by another artist is another album.
    expect(
      albums.where((a) => a.title == 'Greatest Hits').map((a) => a.artist),
      containsAll(['Zed Zone', 'Ember Field']),
    );
  });

  test(
    'compilations: Various Artists and the flag, not under an artist',
    () async {
      final c = await view.compilations();
      expect(
        c.map((a) => a.title),
        containsAll(['Night Drive', 'Summer Static']),
      );
      expect(c.every((a) => a.artist == variousArtists), isTrue);
      for (final artist in await view.artists()) {
        final under = await view.albumsOfArtist(artist.id);
        expect(under.any((a) => a.compilation), isFalse);
      }
      final summer = (await view.album(
        c.firstWhere((a) => a.title == 'Summer Static').id,
      ))!;
      // A track keeps its own artist.
      expect(summer.songs.map((s) => s.artist), ['Pale Harbour', 'Glass Fox']);
    },
  );

  test('unplayable files are listed greyed with a reason', () async {
    final damage = (await view.albums()).firstWhere((a) => a.title == 'Damage');
    final songs = (await view.album(damage.id))!.songs;
    expect(songs, hasLength(4));
    expect(songs.every((s) => !s.playable), isTrue);
    expect(songs.first.unplayableReason, isNotEmpty);
  });

  test('genres come from the tags, with their albums and songs', () async {
    final genres = await view.genres();
    expect(genres.map((g) => g.name), ['Pop', 'Rock']);
    final rock = genres.last;
    expect((await view.albumsOfGenre(rock.id)).map((a) => a.title), [
      'Tidewater',
    ]);
    expect((await view.songsOfGenre(rock.id)).map((s) => s.title), [
      'Low Light',
      'Salt Road',
      'Breakwater',
    ]);
  });

  test('Cover Flow order: by artist, then year', () async {
    final albums = await view.albumsForCoverFlow();
    final i = albums.indexWhere((a) => a.title == 'Tidewater');
    expect(albums[i + 1].title, 'Undertow EP');
  });

  test('totals and durations are stubbed but stable', () async {
    final a = (await view.albums()).firstWhere((a) => a.title == 'Tidewater');
    expect(a.totalTime, isNotNull);
    final again = ManifestLibraryView.parse(asset);
    final b = (await again.albums()).firstWhere((a) => a.title == 'Tidewater');
    expect(b.totalTime, a.totalTime);
    expect(b.art.seed, a.art.seed);
  });

  test(
    'with a library folder, songs are file paths and folder art is found',
    () async {
      final dir = Directory.systemTemp.createTempSync('neupod_gate_lib');
      addTearDown(() => dir.deleteSync(recursive: true));
      generate(outDir: dir);
      final real = ManifestLibraryView.parse(
        File('${dir.path}/manifest.json').readAsStringSync(),
        libraryDir: dir.path,
      );
      final song = (await real.songs()).firstWhere(
        (s) => s.title == 'Low Light',
      );
      expect(File(song.uri).existsSync(), isTrue);
      final tide = (await real.albums()).firstWhere(
        (a) => a.title == 'Tidewater',
      );
      expect(tide.art.path, endsWith('Pale Harbour/2004 Tidewater/Folder.jpg'));
      expect(File(tide.art.path!).existsSync(), isTrue);
      final market = (await real.albums()).firstWhere(
        (a) => a.title == 'Night Market',
      );
      expect(market.art.path, endsWith('cover.jpg'));
    },
  );

  test('the view is read-only: every list is a fresh copy', () async {
    final songs = await view.songs();
    expect(() => songs.add(songs.first), throwsUnsupportedError);
  });
}
