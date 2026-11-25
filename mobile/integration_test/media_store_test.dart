// SPDX-License-Identifier: MPL-2.0

// Android's shared music collection (MediaStore) on the emulator, through the
// real neupod_storage plugin, under instrumentation
// (tool/android_integration_tests.sh):
//
//   1. the instrumentation rule (android/app/src/androidTest,
//      MainActivityTest.java) grants the read permission and puts a WAV in
//      MediaStore under Music/neuPodIT/ before the app starts, when run with
//      `neupodSeedMediaStore=true` (the script passes it for this file only).
//      The permission dialog itself is not driven: with the permission held,
//      turning the collection on asks nothing, as on a phone that granted it;
//   2. the test turns the collection on, finds it among the roots, walks it
//      and finds the seeded file at its MediaStore path, and opens it through
//      ContentResolver;
//   3. the app's own scanner reads it: listed, with the WAV's duration read
//      from the file's bytes, under the `media_store` source;
//   4. turned off, the collection is no longer a root and the next scan
//      removes its songs.
//
// On iOS, which has no shared collection for apps, turning it on is refused
// as `unsupported`.

import 'dart:convert';
import 'dart:io';

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:neupod/data/db/drift_library_repository.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/platform/storage_folder_access.dart';
import 'package:neupod/data/scan/local_library_scanner.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod_storage/neupod_storage.dart';

/// Where MainActivityTest.java puts the seeded file.
const seededPath = 'Music/neuPodIT/neupod_it_tone.wav';

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // As in app_launch_test.dart: under `xcodebuild test` iOS turns
  // accessibility on just after launch; wait for it first (bounded).
  setUpAll(() async {
    if (!Platform.isIOS) return;
    final deadline = DateTime.now().add(const Duration(seconds: 10));
    while (!binding.platformDispatcher.semanticsEnabled &&
        DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
  });

  testWidgets('the shared collection: on, listed, read, scanned, then off', (
    tester,
  ) async {
    final storage = NeupodStorage();
    await tester.runAsync(() async {
      if (!Platform.isAndroid) {
        await expectLater(
          storage.addSharedCollection(),
          throwsA(
            isA<StorageException>().having(
              (e) => e.code,
              'code',
              'unsupported',
            ),
          ),
        );
        return;
      }

      final root = await storage.addSharedCollection();
      expect(root, isNotNull, reason: 'the permission is held: no question');
      expect(root!.id, sharedCollectionRootId);
      expect(root.kind, RootKind.sharedCollection);
      expect(root.available, isTrue);
      expect(
        (await storage.roots()).map((r) => r.id),
        contains(sharedCollectionRootId),
      );

      // MediaStore may take a moment to list a file just added.
      StorageEntry? item;
      final deadline = DateTime.now().add(const Duration(seconds: 30));
      while (item == null) {
        await for (final page in storage.walk(sharedCollectionRootId)) {
          for (final e in page.entries) {
            if (e.relPath == seededPath) item = e;
          }
          if (page.done) expect(page.complete, isTrue);
        }
        if (item != null) break;
        if (DateTime.now().isAfter(deadline)) {
          fail('$seededPath not in the shared collection');
        }
        await Future<void>.delayed(const Duration(milliseconds: 500));
      }
      expect(item.uri, startsWith('content://media/external_primary/'));
      final reader = await storage.openReader(item);
      try {
        expect(ascii.decode(reader.readAt(0, 4)), 'RIFF');
      } finally {
        reader.close();
      }

      final db = LibraryDatabase(NativeDatabase.memory());
      try {
        final scanner = LocalLibraryScanner(
          folders: StorageFolderAccess(storage, offersSharedCollection: true),
          storage: storage,
          db: db,
        );
        await scanner.scan().drain<void>();
        final songs = await DriftLibraryRepository(db).watchSongs().first;
        final t = songs.firstWhere((t) => t.location.relPath == seededPath);
        expect(
          t.source,
          const TrackSource(SourceKind.mediaStore, sharedCollectionRootId),
        );
        expect(t.codec, 'wav');
        expect(t.unreadableReason, isNull);
        // One second of 44.1 kHz mono 16-bit PCM, from the file's own header
        // (wav_parse: the device gives no duration below 44.1 kHz).
        expect(t.durationMs, inInclusiveRange(990, 1010));
        expect(t.displayTitle, 'neupod_it_tone.wav');

        await storage.removeRoot(sharedCollectionRootId);
        expect(
          (await storage.roots()).map((r) => r.id),
          isNot(contains(sharedCollectionRootId)),
        );
        await scanner.scan().drain<void>();
        final after = await DriftLibraryRepository(db).watchSongs().first;
        expect(after.where((t) => t.location.relPath == seededPath), isEmpty);
      } finally {
        await db.close();
      }
    });
  });
}
