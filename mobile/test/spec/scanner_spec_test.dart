// SPDX-License-Identifier: MPL-2.0

// Spec suite: the scanner over the generated test library
// (mobile/tool/gen_test_library).
//
// - The read-only guarantee (docs/app/README.md, Getting music onto the
//   phone, Not goals and "It has failed if"; TEAM-BRIEF.md Rules): the app
//   never writes to, moves or deletes a music file, and never writes into a
//   music folder. A scan and a rescan must leave every byte, size,
//   modification time and permission in the tree as it was, and add or
//   remove nothing.
// - Conformance with the generated library's manifest: every file whose
//   expectation the specification settles shows as expected.
//
// It runs the app's real scanner on the host against a plain folder
// (dart:io), so it needs a scanner entry point that takes a directory. On a
// device the same check belongs in integration_test/.
//
// WIRING: set wiredLibraryFromFolder to an adapter that scans [root] with
// the app's scanner (into an in-memory database) and returns the browsing
// lists through the LibraryView of library_rules_spec_test.dart.
@Tags(['spec'])
library;

import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import '../../tool/gen_test_library/gen_test_library.dart';
import 'library_rules_spec_test.dart' show AlbumView, LibraryView;

/// Scans [root] with the app's scanner. Called twice per test for the
/// rescan; the adapter may keep its database between the calls.
typedef LibraryFromFolder = Future<LibraryView> Function(Directory root);

// WIRE: replace null with an adapter over the app's scanner.
final LibraryFromFolder? wiredLibraryFromFolder = null;

final Object _skip = wiredLibraryFromFolder == null
    ? 'awaiting implementation of the scanner (folder to library)'
    : false;

/// Everything about a tree that a write, move or delete would change.
Map<String, String> _snapshot(Directory root) {
  final out = <String, String>{};
  final entities = root.listSync(recursive: true, followLinks: false)
    ..sort((a, b) => a.path.compareTo(b.path));
  for (final e in [root, ...entities]) {
    final s = e.statSync();
    final rel = e.path.substring(root.path.length);
    final content = e is File
        ? base64.encode(e.readAsBytesSync())
        : (e is Link ? e.targetSync() : '');
    out[rel] =
        '${s.type} ${s.size} ${s.modified.microsecondsSinceEpoch} '
        '${s.mode} $content';
  }
  return out;
}

void main() {
  group('scanner', skip: _skip, () {
    late Directory tmp;
    late GenResult gen;

    setUp(() {
      tmp = Directory.systemTemp.createTempSync('neupod_spec_');
      gen = generate(outDir: Directory('${tmp.path}/gen'));
    });

    tearDown(() => tmp.deleteSync(recursive: true));

    test('a scan and a rescan change nothing in the library tree', () async {
      final before = _snapshot(gen.library);
      expect(before.length, greaterThan(50));
      await wiredLibraryFromFolder!(gen.library);
      expect(_snapshot(gen.library), before, reason: 'after the first scan');
      await wiredLibraryFromFolder!(gen.library);
      expect(_snapshot(gen.library), before, reason: 'after the rescan');
    });

    test(
      'the library matches the manifest where the spec is settled',
      () async {
        final lib = await wiredLibraryFromFolder!(gen.library);
        final manifest =
            jsonDecode(gen.manifest.readAsStringSync()) as Map<String, Object?>;
        final tracks = (manifest['tracks']! as List<Object?>)
            .cast<Map<String, Object?>>();
        final songs = lib.songs();
        final byTitle = {for (final s in songs) s.title: s};
        final compilations = lib.compilations();
        final albumsByKase = <String, Set<String>>{};

        for (final t in tracks) {
          final e = t['expect'] as Map<String, Object?>?;
          if (e == null || t['ambiguous'] != null) continue;
          final title = e['title']! as String;
          final where = t['path']! as String;
          final song = byTitle[title];
          expect(song, isNotNull, reason: '$where is listed in Songs');
          expect(song!.greyed, !(e['playable']! as bool), reason: where);
          if (e['performer'] != null) {
            expect(song.artist, e['performer'], reason: where);
          }

          final List<AlbumView> pool;
          if (e['compilation']! as bool) {
            pool = compilations;
          } else {
            final artist = e['artist']! as String;
            expect(lib.artists(), contains(artist), reason: where);
            pool = lib.albumsOf(artist);
          }
          final holding = [
            for (final a in pool)
              if (a.tracks.any((x) => x.title == title)) a,
          ];
          expect(holding, hasLength(1), reason: '$where is on one album');
          final album = holding.single;
          final row = album.tracks.firstWhere((x) => x.title == title);
          if (e['album'] != null) expect(album.name, e['album'], reason: where);
          if (e['year'] != null) expect(album.year, e['year'], reason: where);
          if (e['disc'] != null) expect(row.disc, e['disc'], reason: where);
          if (e['track'] != null) expect(row.track, e['track'], reason: where);
          expect(row.greyed, !(e['playable']! as bool), reason: where);
          albumsByKase
              .putIfAbsent(t['case']! as String, () => {})
              .add('${album.artist}/${album.name}');
        }

        // Every disc of a multi-disc case is the same album.
        for (final kase in [
          'disc-in-album-tag',
          'disc-tags-two-folders',
          'disc-folder-no-tags',
        ]) {
          expect(albumsByKase[kase], hasLength(1), reason: kase);
        }
        // Files that are not audio are not songs.
        expect(byTitle.keys, isNot(contains('notes')));
        expect(byTitle.keys, isNot(contains('notes.txt')));
        expect(byTitle.keys, isNot(contains('Folder')));
      },
    );
  });
}
