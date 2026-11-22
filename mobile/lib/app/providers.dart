// SPDX-License-Identifier: MPL-2.0

/// Infrastructure providers: the one place where the data and audio layers are
/// built and handed to the UI (decision 0006). Tests swap any of them with
/// `ProviderScope(overrides: [...])`.
///
/// Only this folder and `ui/` may import Riverpod; `domain/`, `data/` and
/// `audio/` stay plain Dart (enforced by `test/architecture_test.dart`).
library;

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/data/db/drift_library_repository.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/db/open_library_database.dart';
import 'package:neupod/data/platform/storage_folder_access.dart';
import 'package:neupod/data/scan/local_library_scanner.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/library_summary.dart';
import 'package:neupod_storage/neupod_storage.dart';

/// A summary of the music library.
///
/// Scaffold placeholder: it always reports an empty library. Phase 1 replaces
/// it with a provider over the data layer's library repository.
final librarySummaryProvider = Provider<LibrarySummary>(
  (ref) => const LibrarySummary(trackCount: 0),
);

/// The neupod_storage plugin (decision 0004): the only code that holds folder
/// grants and security-scoped access.
final neupodStorageProvider = Provider<NeupodStorage>((ref) => NeupodStorage());

/// Read-only access to the library folders.
final folderAccessProvider = Provider<FolderAccess>(
  (ref) => StorageFolderAccess(ref.watch(neupodStorageProvider)),
);

/// The library database (decision 0005), opened on first use in a background
/// isolate and closed with the container.
final libraryDatabaseProvider = Provider<LibraryDatabase>((ref) {
  final storage = ref.watch(neupodStorageProvider);
  final db = openLibraryDatabase(storage.appSupportPath);
  ref.onDispose(db.close);
  return db;
});

/// The library as the UI reads it.
final libraryRepositoryProvider = Provider<LibraryRepository>(
  (ref) => DriftLibraryRepository(ref.watch(libraryDatabaseProvider)),
);

/// Builds the library from the folders.
final libraryScannerProvider = Provider<LibraryScanner>(
  (ref) => LocalLibraryScanner(
    folders: ref.watch(folderAccessProvider),
    storage: ref.watch(neupodStorageProvider),
    db: ref.watch(libraryDatabaseProvider),
  ),
);
