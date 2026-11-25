// SPDX-License-Identifier: MPL-2.0

// On a device: phone backups keep the ratings and play counts database and
// leave the library database out (owner decision 20).
//
// - iOS: `isExcludedFromBackup` read from the file system: true for
//   Application Support/library (and the library database in it), false for
//   Application Support/stats and the stats database file itself.
// - Android: the library is in noBackupFilesDir, which Android never backs
//   up; the stats database is in filesDir/stats, the one path the app's
//   backup rules include (checked statically by test/app/backup_rules_test.dart).

import 'dart:io';

import 'package:drift/drift.dart' show Value;
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:neupod/data/db/open_library_database.dart';
import 'package:neupod/data/db/stats_database.dart';
import 'package:neupod_storage/neupod_storage.dart';

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // As app_launch_test.dart: on iOS CI, wait for accessibility to come on
  // before the first test.
  setUpAll(() async {
    if (!Platform.isIOS) return;
    final deadline = DateTime.now().add(const Duration(seconds: 10));
    while (!binding.platformDispatcher.semanticsEnabled &&
        DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
  });

  testWidgets('the stats database is backed up and the library is not', (
    tester,
  ) async {
    final storage = NeupodStorage();
    final library = await storage.appSupportPath();
    final stats = await storage.backedUpDataPath();
    expect(stats, isNot(library));

    // Make both database files, as the app does on first use.
    await tester.runAsync(() async {
      final lib = openLibraryDatabase(() async => library);
      await lib.customSelect('SELECT 1').get();
      await lib.close();
      final st = openStatsDatabase(() async => stats);
      await st
          .into(st.trackStats)
          .insertOnConflictUpdate(
            TrackStatsCompanion.insert(
              path: '/backup-check.flac',
              size: 1,
              rating: const Value(1),
            ),
          );
      await st.close();
    });
    expect(File('$library/library.sqlite').existsSync(), isTrue);
    expect(File('$stats/stats.sqlite').existsSync(), isTrue);

    if (Platform.isIOS) {
      expect(await storage.isExcludedFromBackup(library), isTrue);
      expect(await storage.isExcludedFromBackup(stats), isFalse);
      expect(
        await storage.isExcludedFromBackup('$stats/stats.sqlite'),
        isFalse,
      );
      expect(library, endsWith('/Library/Application Support/library'));
      expect(stats, endsWith('/Library/Application Support/stats'));
    } else {
      expect(library, endsWith('/no_backup'));
      expect(stats, endsWith('/files/stats'));
    }
  });
}
