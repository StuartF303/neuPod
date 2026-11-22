// SPDX-License-Identifier: MPL-2.0

// What the screens cache from the library (the song count, the search
// index) is read again once a scan finishes.

import 'package:drift/native.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/ui/features/songs/songs_providers.dart';
import 'package:neupod/ui/screens/browse/home_screen.dart';
import 'package:neupod/ui/screens/search/search_screen.dart';

import '../data/library_view/seed.dart';

class _FakeScan extends ScanNotifier {
  @override
  ScanProgress? build() => null;

  // A test seam: the scan's progress as the test says it.
  // ignore: use_setters_to_change_properties
  void say(ScanProgress p) => state = p;
}

void main() {
  test(
    'a finished scan refreshes the song count and the search index',
    () async {
      final db = LibraryDatabase(NativeDatabase.memory());
      addTearDown(db.close);
      final src = await addSource(db);
      final container = ProviderContainer.test(
        overrides: [
          libraryDatabaseProvider.overrideWithValue(db),
          scanProvider.overrideWith(_FakeScan.new),
        ],
      );
      final scan = container.read(scanProvider.notifier) as _FakeScan;

      // Something reads the library before the scan has found anything.
      expect(await container.read(songCountProvider.future), 0);
      var search = await container.read(searchFunctionProvider.future);
      expect(search('tern').songs, isEmpty);

      scan.say(const ScanProgress());
      await addTrack(
        db,
        sourceId: src,
        relPath: 'A/1.flac',
        title: 'Salt Road',
        artist: 'Tern',
        album: 'Coast',
      );
      scan.say(const ScanProgress(filesFound: 1, filesRead: 1, done: true));

      expect(await container.read(songCountProvider.future), 1);
      search = await container.read(searchFunctionProvider.future);
      expect(search('tern').artists, hasLength(1));
      expect(search('salt').songs, hasLength(1));
    },
  );
}
