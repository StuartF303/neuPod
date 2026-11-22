// SPDX-License-Identifier: MPL-2.0

/// Infrastructure providers: the one place where the data and audio layers are
/// built and handed to the UI (decision 0006). Tests swap any of them with
/// `ProviderScope(overrides: [...])`.
///
/// Only this folder and `ui/` may import Riverpod; `domain/`, `data/` and
/// `audio/` stay plain Dart (enforced by `test/architecture_test.dart`).
library;

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/audio/neupod_player.dart';
import 'package:neupod/audio/test_media.dart';
import 'package:neupod/data/platform/storage_folder_access.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library/library_summary.dart';
import 'package:neupod/domain/playback/player.dart';
import 'package:neupod/domain/playback/player_state.dart';
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
  (ref) => StorageFolderAccess(ref.watch(neupodStorageProvider)),
);
