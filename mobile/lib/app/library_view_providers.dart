// SPDX-License-Identifier: MPL-2.0

/// Wires the browsing screens' library and their playback to the app.
library;

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/playback/queue_item.dart';

/// The library the browsing screens show: the scanned drift database.
final libraryViewProvider = FutureProvider<LibraryView>(
  (ref) async => DriftLibraryView(ref.watch(libraryDatabaseProvider)),
);

/// Turns songs into the player's queue items.
typedef QueueMapper = Future<List<QueueItem>> Function(List<Song> songs);

/// Maps songs to queue items: each playable song opens its own file (its
/// [Song.uri] is the URI the audio engine opens).
final queueMapperProvider = Provider<QueueMapper>((ref) {
  return (songs) async => [
    for (final s in songs)
      if (s.playable)
        QueueItem(
          id: s.id,
          uri: s.uri,
          title: s.title,
          artist: s.artist,
          album: s.album,
          duration: s.duration,
        ),
  ];
});
