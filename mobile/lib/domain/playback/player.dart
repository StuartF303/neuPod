// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';

/// The player port (decision 0006): implemented by the audio layer, faked in
/// tests, read by the UI through `playerStateProvider`.
///
/// Shuffle and repeat are domain rules applied to the queue before it is set
/// (decision 0001); the player plays the queue in the order given.
abstract interface class Player {
  /// The latest snapshot.
  PlayerState get state;

  /// The current snapshot first, then every change.
  Stream<PlayerState> get states;

  /// The playback position, extrapolated while playing, every [interval].
  Stream<Duration> positions({
    Duration interval = const Duration(milliseconds: 200),
  });

  /// Replaces the queue and loads [startIndex] at [startPosition]. Playback
  /// continues if it was playing; otherwise call [play].
  Future<void> setQueue(
    List<QueueItem> items, {
    int startIndex = 0,
    Duration startPosition = Duration.zero,
  });

  Future<void> play();

  Future<void> pause();

  Future<void> seek(Duration position);

  /// Jumps to the start of queue entry [index].
  Future<void> skipTo(int index);

  Future<void> skipToNext();

  /// Restarts the track after its first three seconds, otherwise goes to the
  /// previous track (the spec's skip-back rule).
  Future<void> skipToPrevious();

  /// What the lock screen and notification show now.
  Future<MediaSessionSnapshot> sessionSnapshot();

  Future<void> dispose();
}
