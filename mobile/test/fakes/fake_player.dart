// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:neupod/domain/playback/player.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';

/// A [Player] for widget tests: records commands, and the test sets [state].
class FakePlayer implements Player {
  FakePlayer([PlayerState initial = PlayerState.empty]) : _state = initial;

  final _states = StreamController<PlayerState>.broadcast();
  final calls = <String>[];
  PlayerState _state;
  MediaSessionSnapshot session = const MediaSessionSnapshot();

  @override
  PlayerState get state => _state;

  set state(PlayerState next) {
    _state = next;
    _states.add(next);
  }

  @override
  Stream<PlayerState> get states async* {
    yield _state;
    yield* _states.stream;
  }

  /// A single value, so widget tests do not leave a periodic timer running.
  @override
  Stream<Duration> positions({
    Duration interval = const Duration(milliseconds: 200),
  }) => Stream.value(_state.position);

  @override
  Future<void> setQueue(
    List<QueueItem> items, {
    int startIndex = 0,
    Duration startPosition = Duration.zero,
  }) async {
    calls.add('setQueue(${items.map((i) => i.uri).join(', ')})');
    state = _state.copyWith(queue: items, index: () => startIndex);
  }

  @override
  Future<void> play() async {
    calls.add('play');
    state = _state.copyWith(playWhenReady: true);
  }

  @override
  Future<void> pause() async {
    calls.add('pause');
    state = _state.copyWith(playWhenReady: false);
  }

  @override
  Future<void> seek(Duration position) async =>
      calls.add('seek(${position.inMilliseconds})');

  @override
  Future<void> skipTo(int index) async => calls.add('skipTo($index)');

  @override
  Future<void> skipToNext() async => calls.add('skipToNext');

  @override
  Future<void> skipToPrevious() async => calls.add('skipToPrevious');

  @override
  Future<MediaSessionSnapshot> sessionSnapshot() async => session;

  @override
  Future<void> dispose() => _states.close();
}
