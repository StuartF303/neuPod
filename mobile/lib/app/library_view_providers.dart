// SPDX-License-Identifier: MPL-2.0

/// Wires the browsing screens' library and their playback to the app.
library;

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/art_providers.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/data/art/art_cache.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/library_view/reason_words.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';

/// The phone whose player decides which formats cannot be decoded (greyed
/// with `Opus, can't play`; `reason_words.dart`).
final playerPlatformProvider = Provider<PlayerPlatform>(
  (ref) => defaultTargetPlatform == TargetPlatform.iOS
      ? PlayerPlatform.ios
      : PlayerPlatform.android,
);

/// The library the browsing screens show: the scanned drift database.
///
/// A scan finishing rebuilds it, and with it every provider that read the
/// library (song count, cover previews, the search index, the playing
/// album), so lists never keep what a finished scan has changed. A scan
/// starting does not: what was there stays until the new one is done.
final libraryViewProvider = FutureProvider<LibraryView>((ref) async {
  ref.listen(libraryScanProvider.select((s) => s?.done ?? false), (was, now) {
    if (was == false && now) ref.invalidateSelf();
  });
  final view = DriftLibraryView(
    ref.watch(libraryDatabaseProvider),
    platform: ref.watch(playerPlatformProvider),
  );
  ref.onDispose(view.dispose);
  return view;
});

/// Turns songs into the player's queue items; [start] is the song that will
/// play first.
typedef QueueMapper = Future<List<QueueItem>> Function(
  List<Song> songs, {
  int start,
});

/// How long starting playback waits for the first song's lock-screen art.
const lockScreenArtWait = Duration(seconds: 2);

/// Songs ahead of the one playing whose lock-screen art is made in advance.
const lockScreenArtAhead = 4;

/// Maps songs to queue items: each playable song opens its own file (its
/// [Song.uri] is the URI the audio engine opens), and shows its own art on
/// the lock screen and in the notification: the art cache's full-size file,
/// as a `file://` URI. The first song's file is made before the queue is set
/// (waiting at most [lockScreenArtWait]); [warmLockScreenArt] makes the next
/// ones as the queue plays.
final queueMapperProvider = Provider<QueueMapper>((ref) {
  return (songs, {int start = 0}) async {
    final cache = ref.read(artCacheProvider);
    final items = <QueueItem>[];
    ArtRef? first;
    for (final (i, s) in songs.indexed) {
      if (!s.playable) continue;
      String? art;
      if (s.art.sources.isNotEmpty) {
        final path = await cache.pathFor(s.art, ArtSize.full);
        if (path != null) {
          art = Uri.file(path).toString();
          if (i == start) first = s.art;
        }
      } else if (s.art.path != null) {
        art = Uri.file(s.art.path!).toString();
      }
      items.add(
        QueueItem(
          id: s.id,
          uri: s.uri,
          title: s.title,
          artist: s.artist,
          album: s.album,
          artUri: art,
          duration: s.duration,
        ),
      );
    }
    if (first != null) {
      cache.pin([first]);
      await cache.warm([first]).timeout(lockScreenArtWait, onTimeout: () {});
    }
    return items;
  };
});

/// Makes the lock-screen art of the song playing and the next
/// [lockScreenArtAhead] (the player reads a song's art file when the song
/// starts), ahead of the screens' renders, and keeps those files from being
/// evicted. The songs are looked up in [library] by the queue items' ids, so
/// a queue restored after a restart is served too. Called on every player
/// state change; cheap when nothing moved.
Future<void> warmLockScreenArt(
  ArtCache cache,
  Future<LibraryView> Function() library,
  PlayerState? was,
  PlayerState now,
) async {
  final i = now.index;
  if (i == null) return;
  if (was != null && was.index == i && identical(was.queue, now.queue)) return;
  final arts = <ArtRef>[];
  try {
    final view = await library();
    for (var k = i; k < now.queue.length && k <= i + lockScreenArtAhead; k++) {
      final art = (await view.song(now.queue[k].id))?.art;
      if (art != null && art.sources.isNotEmpty) arts.add(art);
    }
  } on Object {
    return; // no library: nothing to warm
  }
  cache.pin(arts);
  await cache.warm(arts);
}
