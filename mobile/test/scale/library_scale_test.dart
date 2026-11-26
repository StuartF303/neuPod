// SPDX-License-Identifier: MPL-2.0

// The scale test (docs/app/README.md, How it is tested: "A generated
// 20,000-track library: scan time, memory, scroll smoothness"; PROGRESS phase
// 3). It generates the scale library (mobile/tool/gen_test_library: real
// FLAC, MP3 and M4A files, so the real tag readers run) in a temporary folder,
// scans it with the app's scanner into a database file, and measures:
//
// - the first scan, a rescan with nothing changed, a rescan after 100
//   changes (60 files retagged, 20 removed, 20 added);
// - the database's size, and the process's peak memory (max RSS);
// - the lists the screens read through the app's DriftLibraryView: Artists,
//   Albums (and Cover Flow, which shows the same list, owner decision 14),
//   Songs, an artist's albums, an album's tracks, and Search's prefix match.
//
// Size: NEUPOD_SCALE_TRACKS (default 2,000, run on every PR with generous
// limits, so a regression shows). The full run is 20,000:
//
//   NEUPOD_SCALE_TRACKS=20000 flutter test test/scale
//
// or the app workflow's `scale_tracks` input. NEUPOD_SCALE_REPORT names a
// file to write the numbers to as JSON. The limits are in [_Limits]; the
// targets they come from are in docs/app/decisions/0013-scale-test.md.
//
// Times are on the host, in the test's debug (JIT) VM, so they are an upper
// bound for the code's own cost and say nothing about a phone's storage; the
// phone numbers come from the device check (PROGRESS, phase 3).
@Tags(['scale'])
library;

import 'dart:convert';
import 'dart:io';

import 'package:drift/native.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/domain/library_view/reason_words.dart';
import 'package:neupod/ui/screens/search/search_screen.dart';

import '../../tool/gen_test_library/gen_test_library.dart';
import '../support/host_library.dart';

final int _tracks =
    int.tryParse(Platform.environment['NEUPOD_SCALE_TRACKS'] ?? '') ?? 2000;

/// The limits every run must meet, from the targets in
/// docs/app/decisions/0013-scale-test.md. The spec's own targets are for a
/// mid-range phone, so on the host they are loose: at the 2,000 tracks of
/// every PR they catch a regression of several times, not a slow percent.
abstract final class _Limits {
  /// The spec's first scan: 5,000 FLAC files in five minutes, 60 ms a file.
  static Duration firstScan(int tracks) => Duration(milliseconds: 60 * tracks);

  /// The spec's rescan with nothing changed: ten seconds.
  static const rescanUnchanged = Duration(seconds: 10);

  /// A rescan after 100 changes (lead call): as the unchanged one, plus
  /// 100 files read at the first scan's rate.
  static const rescan100 = Duration(seconds: 16);

  /// The first list after a launch or a scan, which reads the whole table
  /// and folds it into the firmware's entities (lead call).
  static const coldLoad = Duration(seconds: 1);

  /// Any list once loaded (lead call).
  static const listRead = Duration(milliseconds: 100);

  /// One Search keystroke, the prefix match over every name (lead call).
  static const searchKey = Duration(milliseconds: 50);

  /// Bytes of database per track (lead call: 40 MB at 20,000).
  static const dbBytesPerTrack = 2048;
}

/// Runs [body] [times] times and returns the slowest, in microseconds.
Future<int> _slowest(int times, Future<void> Function() body) async {
  var worst = 0;
  for (var i = 0; i < times; i++) {
    final sw = Stopwatch()..start();
    await body();
    final us = sw.elapsedMicroseconds;
    if (us > worst) worst = us;
  }
  return worst;
}

String _ms(int us) => (us / 1000).toStringAsFixed(1);

void main() {
  late Directory tmp;
  setUp(() => tmp = Directory.systemTemp.createTempSync('neupod_scale_'));
  tearDown(() async {
    if (tmp.existsSync()) tmp.deleteSync(recursive: true);
  });

  test(
    'the scale library: scans, rescans and list reads at $_tracks tracks',
    timeout: const Timeout(Duration(minutes: 60)),
    () async {
      final report = <String, Object>{'tracks': _tracks};
      final rssAtStart = ProcessInfo.currentRss;

      final gen = Stopwatch()..start();
      final g = generate(
        outDir: Directory('${tmp.path}/gen'),
        scaleTracks: _tracks,
      );
      report['generateMs'] = gen.elapsedMilliseconds;
      report['libraryBytes'] = g.bytes;

      final dbFile = File('${tmp.path}/library.sqlite');
      // As the app opens it: SQLite in a background isolate (decision 0005).
      final db = LibraryDatabase(NativeDatabase.createInBackground(dbFile));
      addTearDown(db.close);

      // The first scan.
      var sw = Stopwatch()..start();
      var last = await scanHostFolder(db, g.library);
      final firstScan = sw.elapsed;
      expect(last?.done, isTrue);
      expect(last?.error, isNull);
      expect(last?.filesFound, _tracks);
      expect(last?.filesRead, _tracks);
      report['firstScanMs'] = firstScan.inMilliseconds;

      // A rescan with nothing changed: no file is opened.
      sw = Stopwatch()..start();
      last = await scanHostFolder(db, g.library);
      final rescan = sw.elapsed;
      expect(last?.filesFound, _tracks);
      expect(last?.filesRead, 0);
      report['rescanUnchangedMs'] = rescan.inMilliseconds;

      // 100 changes: 60 files retagged (a new title, so a new size), 20
      // removed, 20 new ones added.
      final cases = scaleCases(_tracks);
      final fx = Fixtures.locate();
      final later = DateTime.now().add(const Duration(minutes: 5));
      var retagged = 0;
      for (var i = 7; retagged < 60; i += _tracks ~/ 80) {
        final c = cases[i];
        if (c.tags == null) continue;
        final t = c.tags!;
        final changed = GenFile(
          c.path,
          c.source,
          kase: c.kase,
          tags: TagSet(
            title: '${t.title} (Remastered)',
            artist: t.artist,
            albumArtist: t.albumArtist,
            album: t.album,
            track: t.track,
            disc: t.disc,
            year: t.year,
            genre: t.genre,
            compilation: t.compilation,
          ),
        );
        File('${g.library.path}/${c.path}')
          ..writeAsBytesSync(render(changed, fx))
          ..setLastModifiedSync(later);
        retagged++;
      }
      for (var i = 0; i < 20; i++) {
        File('${g.library.path}/${cases[_tracks - 1 - i * 3].path}')
            .deleteSync();
      }
      for (final c in scaleCases(_tracks + 20).sublist(_tracks)) {
        File('${g.library.path}/${c.path}')
          ..createSync(recursive: true)
          ..writeAsBytesSync(render(c, fx));
      }
      sw = Stopwatch()..start();
      last = await scanHostFolder(db, g.library);
      final rescan100 = sw.elapsed;
      expect(last?.filesFound, _tracks);
      expect(last?.filesRead, 80);
      report['rescan100Ms'] = rescan100.inMilliseconds;

      await db.customStatement('PRAGMA wal_checkpoint(TRUNCATE)');
      final dbBytes = dbFile.lengthSync();
      report['databaseBytes'] = dbBytes;

      // The lists, as the screens read them.
      report['rssBeforeListsBytes'] = ProcessInfo.currentRss;

      // The table read alone, for comparison with the view's first list
      // (which reads it and folds it).
      sw = Stopwatch()..start();
      await db.select(db.tracks).get();
      report['tableReadMs'] = sw.elapsedMilliseconds;

      final view = DriftLibraryView(db, platform: PlayerPlatform.ios);
      addTearDown(view.dispose);
      sw = Stopwatch()..start();
      final artists = await view.artists();
      final coldLoad = sw.elapsed;
      report['coldLoadMs'] = coldLoad.inMilliseconds;

      final albums = await view.albums();
      final songs = await view.songs();
      expect(songs, hasLength(_tracks));
      report['artists'] = artists.length;
      report['albums'] = albums.length;
      report['compilations'] = (await view.compilations()).length;
      final artist = artists[artists.length ~/ 2];
      final album = albums[albums.length ~/ 3];

      final reads = <String, int>{
        'artistsUs': await _slowest(5, view.artists),
        'albumsUs': await _slowest(5, view.albums),
        'coverFlowUs': await _slowest(5, view.albums),
        'songsUs': await _slowest(5, view.songs),
        'artistAlbumsUs': await _slowest(
          5,
          () => view.albumsOfArtist(artist.id),
        ),
        'albumTracksUs': await _slowest(5, () => view.album(album.id)),
      };

      // Search: the app's own search function over this library.
      final container = ProviderContainer(
        overrides: [libraryViewProvider.overrideWith((ref) async => view)],
      );
      addTearDown(container.dispose);
      sw = Stopwatch()..start();
      final search = await container.read(searchFunctionProvider.future);
      report['searchBuildMs'] = sw.elapsedMilliseconds;
      var hits = 0;
      var searchKey = 0;
      for (var round = 0; round < 5; round++) {
        for (final q in ['g', 'gl', 'gla', 'glas', 'glass']) {
          final key = Stopwatch()..start();
          final r = search(q);
          final us = key.elapsedMicroseconds;
          if (us > searchKey) searchKey = us;
          hits = r.artists.length + r.albums.length + r.songs.length;
        }
      }
      expect(hits, greaterThan(0));
      report.addAll(reads);
      report['searchKeyUs'] = searchKey;

      report['rssAfterListsBytes'] = ProcessInfo.currentRss;
      final peakRss = ProcessInfo.maxRss;
      report['rssAtStartBytes'] = rssAtStart;
      report['peakRssBytes'] = peakRss;

      final text = const JsonEncoder.withIndent('  ').convert(report);
      // ignore: avoid_print
      print('Scale test at $_tracks tracks:\n$text');
      final out = Platform.environment['NEUPOD_SCALE_REPORT'];
      if (out != null && out.isNotEmpty) File(out).writeAsStringSync(text);

      expect(
        firstScan,
        lessThan(_Limits.firstScan(_tracks)),
        reason: 'first scan',
      );
      expect(
        rescan,
        lessThan(_Limits.rescanUnchanged),
        reason: 'rescan, nothing changed',
      );
      expect(rescan100, lessThan(_Limits.rescan100), reason: '100 changes');
      expect(coldLoad, lessThan(_Limits.coldLoad), reason: 'first list');
      for (final MapEntry(:key, :value) in reads.entries) {
        expect(
          Duration(microseconds: value),
          lessThan(_Limits.listRead),
          reason: '$key (${_ms(value)} ms)',
        );
      }
      expect(
        Duration(microseconds: searchKey),
        lessThan(_Limits.searchKey),
        reason: 'a Search keystroke (${_ms(searchKey)} ms)',
      );
      expect(dbBytes, lessThan(_Limits.dbBytesPerTrack * _tracks));
    },
  );
}
