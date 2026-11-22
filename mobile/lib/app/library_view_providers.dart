// SPDX-License-Identifier: MPL-2.0

/// Wires the browsing screens' library and their playback to the app.
library;

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod/ui/features/songs/songs_providers.dart';

/// The library the browsing screens show: the scanned drift database.
///
/// A scan starting or finishing rebuilds it, and with it every provider that
/// read the library (song count, cover previews, the search index, the
/// playing album), so lists never keep what a finished scan has changed.
final libraryViewProvider = FutureProvider<LibraryView>((ref) async {
  ref.watch(scanProvider.select((s) => s?.done ?? false));
  final view = DriftLibraryView(ref.watch(libraryDatabaseProvider));
  ref.onDispose(view.dispose);
  return view;
});

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
