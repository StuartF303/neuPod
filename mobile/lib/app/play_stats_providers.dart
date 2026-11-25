// SPDX-License-Identifier: MPL-2.0

/// Ratings and play counts (decision 0011): the store, each track's
/// numbers, and the play counter that runs for the life of the app.
library;

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/play_counter.dart';
import 'package:neupod/domain/library/play_stats.dart';

export 'package:neupod/app/providers.dart'
    show statsDatabaseProvider, trackStatsStoreProvider;

/// One track's numbers by its library id, kept current while watched.
final trackStatsProvider = StreamProvider.autoDispose.family<PlayStats, String>(
  (ref, trackId) => ref.watch(trackStatsStoreProvider).watch(trackId),
);

final _clock = Stopwatch()..start();

/// The play counter: started by the app at launch, it counts plays into the
/// store as the firmware counts them.
final playCounterProvider = Provider<PlayCounter>((ref) {
  final counter = PlayCounter(
    player: ref.watch(playerProvider),
    nowMs: () => _clock.elapsedMilliseconds,
    onCounted: (item) => unawaited(() async {
      try {
        await ref.read(trackStatsStoreProvider).countPlay(item.id);
      } on Object catch (e) {
        // A play that cannot be stored is lost, as on the device when its
        // inbox is full; playback goes on.
        debugPrint('neupod: play of ${item.id} not counted: $e');
      }
    }()),
  )..start();
  ref.onDispose(counter.dispose);
  return counter;
});
