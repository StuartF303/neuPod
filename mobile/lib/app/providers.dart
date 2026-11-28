// SPDX-License-Identifier: MPL-2.0

/// Infrastructure providers: the one place where the data and audio layers are
/// built and handed to the UI (decision 0006). Tests swap any of them with
/// `ProviderScope(overrides: [...])`.
///
/// Only this folder and `ui/` may import Riverpod; `domain/`, `data/` and
/// `audio/` stay plain Dart (enforced by `test/architecture_test.dart`).
library;

import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/audio/neupod_player.dart';
import 'package:neupod/audio/test_media.dart';
import 'package:neupod/data/db/drift_library_repository.dart';
import 'package:neupod/data/db/drift_track_stats_store.dart';
import 'package:neupod/data/db/stats_database.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/db/open_library_database.dart';
import 'package:neupod/data/platform/storage_folder_access.dart';
import 'package:neupod/data/scan/local_library_scanner.dart';
import 'package:neupod/data/settings/file_settings_store.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/library_summary.dart';
import 'package:neupod/domain/library/play_stats.dart';
import 'package:neupod/domain/playback/player.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/settings/settings_store.dart';
import 'package:neupod_audio/neupod_audio.dart';
import 'package:neupod_storage/neupod_storage.dart';

/// A summary of the music library.
///
/// Scaffold placeholder: it always reports an empty library. Phase 1 replaces
/// it with a provider over the data layer's library repository.
final librarySummaryProvider = Provider<LibrarySummary>(
  (ref) => const LibrarySummary(trackCount: 0),
);

/// The player (decisions 0001, 0002 and 0006). One per app: the native engine
/// and the media session are singletons.
final playerProvider = Provider<Player>((ref) {
  final player = NeupodPlayer(NeupodAudio());
  ref.onDispose(player.dispose);
  return player;
});

/// Every screen's view of the player: the current snapshot, then each change.
final playerStateProvider = StreamProvider<PlayerState>(
  (ref) => ref.watch(playerProvider).states,
);

/// The bundled test tones, for the player debug screen.
final testMediaProvider = Provider<TestMedia>((ref) => const TestMedia());

/// The neupod_storage plugin (decision 0004): the only code that holds folder
/// grants and security-scoped access.
final neupodStorageProvider = Provider<NeupodStorage>((ref) => NeupodStorage());

/// Read-only access to the library folders.
final folderAccessProvider = Provider<FolderAccess>(
  (ref) => StorageFolderAccess(
    ref.watch(neupodStorageProvider),
    // Android's MediaStore; iOS has no shared music collection for apps.
    offersSharedCollection: defaultTargetPlatform == TargetPlatform.android,
  ),
);

/// The library database (decision 0005), opened on first use in a background
/// isolate and closed with the container.
final libraryDatabaseProvider = Provider<LibraryDatabase>((ref) {
  final storage = ref.watch(neupodStorageProvider);
  final db = openLibraryDatabase(storage.appSupportPath);
  ref.onDispose(db.close);
  return db;
});

/// The ratings and play counts database (decision 0011), apart from the
/// library so that phone backups keep it (owner decision 20): in
/// neupod_storage's backed-up directory, opened on first use in a background
/// isolate and closed with the container.
final statsDatabaseProvider = Provider<StatsDatabase>((ref) {
  final storage = ref.watch(neupodStorageProvider);
  final db = openStatsDatabase(storage.backedUpDataPath);
  ref.onDispose(db.close);
  return db;
});

/// The app's settings (Mode, the Custom looks, ...): one JSON file in a
/// `settings/` folder beside the stats database, in the backed-up data
/// directory, so phone backups keep it too (owner decision 20). On iOS that is
/// Application Support, not excluded from backup; on Android `filesDir`,
/// which `backup_rules.xml` and `data_extraction_rules.xml` include.
final settingsStoreProvider = Provider<SettingsStore>((ref) {
  final storage = ref.watch(neupodStorageProvider);
  return FileSettingsStore(() async {
    final stats = await storage.backedUpDataPath();
    return '${Directory(stats).parent.path}/settings';
  });
});

/// Ratings and play counts, with tracks found in the library.
final trackStatsStoreProvider = Provider<TrackStatsStore>(
  (ref) => DriftTrackStatsStore(
    ref.watch(libraryDatabaseProvider),
    ref.watch(statsDatabaseProvider),
  ),
);

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
    // A move or retag seen by a scan carries the file's ratings and play
    // counts to its new key (owner decision 20). Read when it happens, so a
    // scan never opens the stats database for nothing.
    onMoves: (moves) => ref.read(trackStatsStoreProvider).carry(moves),
  ),
);
