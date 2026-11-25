// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:neupod/domain/library/play_stats.dart';
import 'package:neupod/domain/playback/player.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';

/// Counts plays as the firmware's `count_plays()` does (`app/runtime/
/// player.c`): a pass every [playCountPass] feeds the player's state to a
/// [PlayTracker], and a track start that becomes a counted play is handed
/// to [onCounted] once.
///
/// - Playing is [PlayerState.isPlaying]: asked to play, ready and not held
///   back (the firmware's `PLAYER_PLAYING && !audio_route_holding()`).
/// - Ended is [PlaybackStatus.ended], the queue's last track ran out
///   (`PLAYER_ENDED`).
/// - The length is [PlayerState.duration], 0 when unknown.
/// - Nothing current (the firmware's "not a file") forgets the track.
/// - A track start (the firmware's `track_id`) is a new queue or another
///   index in it: a skip, a handoff to the next track, a tap on a song.
///   A seek is not one. The engine does not report a restart of the same
///   entry (Previous after three seconds; repeat one in phase 4), so that
///   is not a new start yet: decision 0011 logs it for the audio engineer.
///
/// The passes run only while there is a current track (the firmware's pass
/// does nothing else then). Before the first pass after a pause in the
/// passes, a pass with nothing playing is made, as the firmware's ongoing
/// passes would have been, so the gap adds no heard time.
final class PlayCounter {
  /// A counter on [player], with [nowMs] a monotonic clock in milliseconds.
  PlayCounter({
    required this.player,
    required this.nowMs,
    required this.onCounted,
    this.every = playCountPass,
  });

  /// The player followed.
  final Player player;

  /// A monotonic clock in milliseconds.
  final int Function() nowMs;

  /// Called once for each track start that becomes a counted play.
  final void Function(QueueItem item) onCounted;

  /// The time between passes.
  final Duration every;
  final PlayTracker _tracker = PlayTracker();

  StreamSubscription<PlayerState>? _sub;
  Timer? _timer;
  List<QueueItem>? _queue;
  int? _index;
  int _startId = 0;

  /// Starts following the player.
  void start() {
    _sub ??= player.states.listen(_onState);
  }

  /// Stops; nothing more is counted.
  Future<void> dispose() async {
    _timer?.cancel();
    _timer = null;
    await _sub?.cancel();
    _sub = null;
  }

  void _onState(PlayerState s) {
    if (s.current == null) {
      _timer?.cancel();
      _timer = null;
      _tracker.reset();
      return;
    }
    if (_timer == null) {
      // The passes stopped while nothing was current: carry on from now.
      _noteStart(s);
      _tracker.update(
        nowMs: nowMs(),
        playing: false,
        ended: false,
        trackId: _startId,
        durationMs: s.duration?.inMilliseconds ?? 0,
      );
      _timer = Timer.periodic(every, (_) => pass());
    }
  }

  void _noteStart(PlayerState s) {
    if (!identical(s.queue, _queue) || s.index != _index) {
      _queue = s.queue;
      _index = s.index;
      _startId++;
    }
  }

  /// One pass, as the timer makes it (tests call it directly).
  void pass() {
    final s = player.state;
    final item = s.current;
    if (item == null) {
      _tracker.reset();
      return;
    }
    _noteStart(s);
    final counted = _tracker.update(
      nowMs: nowMs(),
      playing: s.isPlaying,
      ended: s.status == PlaybackStatus.ended,
      trackId: _startId,
      durationMs: s.duration?.inMilliseconds ?? 0,
    );
    if (counted) onCounted(item);
  }
}
