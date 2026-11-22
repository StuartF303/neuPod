// SPDX-License-Identifier: MPL-2.0

// An empty scanned library: each browsing list shows the one greyed
// "None yet" row, as the firmware does (docs/app/README.md).

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/ui/screens/browse/albums_screen.dart';
import 'package:neupod/ui/screens/browse/artists_screen.dart';
import 'package:neupod/ui/screens/browse/songs_screen.dart';
import 'package:flutter/widgets.dart';
import 'package:neupod/domain/library_view/library_view.dart';

import 'screens_harness.dart';

void main() {
  for (final (name, screen) in <(String, Widget)>[
    ('Songs', const SongsScreen()),
    ('Artists', const ArtistsScreen()),
    ('Albums', const AlbumsScreen()),
  ]) {
    testWidgets('$name over an empty database shows None yet', (tester) async {
      final db = LibraryDatabase(NativeDatabase.memory());
      addTearDown(db.close);
      // The database answers in real time, outside the test's fake clock, so
      // its answers are fetched first and handed to the screens as they are.
      final real = DriftLibraryView(db);
      final answered = (await tester.runAsync(
        () async => _Answered(
          await real.songs(),
          await real.artists(),
          await real.albums(),
        ),
      ))!;
      await pumpShell(tester, home: (_) => screen, library: answered);
      expect(find.text('None yet'), findsOneWidget);
    });
  }
}

/// The three lists the screens above read, as the drift adapter answered.
class _Answered implements LibraryView {
  _Answered(this._songs, this._artists, this._albums);

  final List<Song> _songs;
  final List<Artist> _artists;
  final List<Album> _albums;

  @override
  Future<List<Song>> songs() async => _songs;

  @override
  Future<List<Artist>> artists() async => _artists;

  @override
  Future<List<Album>> albums() async => _albums;

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      throw UnimplementedError('${invocation.memberName}');
}
