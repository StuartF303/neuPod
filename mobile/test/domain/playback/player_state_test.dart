// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';

void main() {
  const a = QueueItem(id: 'a', uri: 'file:///a.flac');
  const b = QueueItem(id: 'b', uri: 'file:///b.mp3');

  test('the empty state has nothing to play', () {
    expect(PlayerState.empty.current, isNull);
    expect(PlayerState.empty.isPlaying, isFalse);
    expect(PlayerState.empty.hasNext, isFalse);
  });

  test('current and hasNext follow the index', () {
    const s = PlayerState(queue: [a, b], index: 0);
    expect(s.current, a);
    expect(s.hasNext, isTrue);
    expect(s.copyWith(index: () => 1).hasNext, isFalse);
    expect(s.copyWith(index: () => 5).current, isNull);
  });

  test('playing needs both the intent and a ready engine', () {
    const s = PlayerState(queue: [a], index: 0, playWhenReady: true);
    expect(s.isPlaying, isFalse);
    expect(s.copyWith(status: PlaybackStatus.ready).isPlaying, isTrue);
    expect(
      s.copyWith(status: PlaybackStatus.ready, playWhenReady: false).isPlaying,
      isFalse,
    );
  });

  test('copyWith can clear nullable fields', () {
    const s = PlayerState(
      queue: [a],
      index: 0,
      duration: Duration(seconds: 1),
      format: StreamFormat(codec: 'flac'),
      error: 'x',
    );
    final cleared = s.copyWith(
      index: () => null,
      duration: () => null,
      format: () => null,
      error: () => null,
    );
    expect(cleared.index, isNull);
    expect(cleared.duration, isNull);
    expect(cleared.format, isNull);
    expect(cleared.error, isNull);
    expect(cleared.queue, [a]);
  });

  test('equality is by value', () {
    expect(
      const PlayerState(queue: [a, b], index: 1),
      PlayerState(queue: [a, b].toList(), index: 1),
    );
    expect(
      const PlayerState(queue: [a, b]),
      isNot(const PlayerState(queue: [b, a])),
    );
    expect(
      const PlayerState(queue: [a]).hashCode,
      PlayerState(queue: [a].toList()).hashCode,
    );
  });
}
