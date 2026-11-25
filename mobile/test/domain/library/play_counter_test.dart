// SPDX-License-Identifier: MPL-2.0

// The play counter: the firmware's count_plays() (app/runtime/player.c) on
// the app's player. The rule itself is checked against the firmware in
// play_stats_vectors_test.dart; these check what the counter feeds it.

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/library/play_counter.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';

import '../../fakes/fake_player.dart';

const _a = QueueItem(id: '1', uri: 'file:///a.flac');
const _b = QueueItem(id: '2', uri: 'file:///b.flac');

PlayerState _playing(
  List<QueueItem> queue, {
  int index = 0,
  int seconds = 10,
  bool playWhenReady = true,
  PlaybackStatus status = PlaybackStatus.ready,
}) => PlayerState(
  status: status,
  playWhenReady: playWhenReady,
  queue: queue,
  index: index,
  duration: Duration(seconds: seconds),
);

void main() {
  late FakePlayer player;
  late int now;
  late List<String> counted;
  late PlayCounter counter;

  setUp(() {
    player = FakePlayer();
    now = 0;
    counted = [];
    counter = PlayCounter(
      player: player,
      nowMs: () => now,
      onCounted: (item) => counted.add(item.id),
    );
  });

  /// [n] passes 100 ms apart, as the firmware's monitor task makes them.
  void passes(int n) {
    for (var i = 0; i < n; i++) {
      now += 100;
      counter.pass();
    }
  }

  test('half of the track heard counts it, once', () {
    player.state = _playing(const [_a]);
    counter.pass();
    passes(49);
    expect(counted, isEmpty);
    passes(1); // 5 s of 10 s
    expect(counted, ['1']);
    passes(100);
    expect(counted, ['1']);
  });

  test('paused or held back time does not count', () {
    player.state = _playing(const [_a], playWhenReady: false);
    counter.pass();
    passes(200);
    player.state = _playing(const [_a]).copyWith(suppressed: true);
    passes(200);
    expect(counted, isEmpty);
    player.state = _playing(const [_a]);
    passes(50);
    expect(counted, ['1']);
  });

  test('a seek is not a new start; a skip, another queue or a tap on the '
      'same song in a new list is', () {
    final queue = const [_a, _b];
    player.state = _playing(queue);
    counter.pass();
    passes(30);
    player.state = player.state.copyWith(position: const Duration(seconds: 8));
    passes(20); // the same start: 5 s heard in all
    expect(counted, ['1']);

    player.state = _playing(
      queue,
      index: 1,
    ); // skip: its first pass hears nothing yet
    passes(50);
    expect(counted, ['1']);
    passes(1);
    expect(counted, ['1', '2']);

    player.state = _playing(List.of(queue), index: 1); // a new queue
    passes(51);
    expect(counted, ['1', '2', '2']);
  });

  test('a track that ended by itself counts after 3 s heard', () {
    player.state = _playing(const [_a], seconds: 600);
    counter.pass();
    passes(30);
    player.state = _playing(
      const [_a],
      seconds: 600,
      status: PlaybackStatus.ended,
      playWhenReady: false,
    );
    passes(1);
    expect(counted, ['1']);
  });

  test('a length not known: 30 s heard', () {
    player.state = PlayerState(
      status: PlaybackStatus.ready,
      playWhenReady: true,
      queue: const [_a],
      index: 0,
    );
    counter.pass();
    passes(299);
    expect(counted, isEmpty);
    passes(1);
    expect(counted, ['1']);
  });

  test('nothing current forgets the track', () {
    player.state = _playing(const [_a]);
    counter.pass();
    passes(40);
    player.state = PlayerState.empty;
    passes(5);
    player.state = _playing(const [_a]);
    passes(40);
    expect(counted, isEmpty);
  });

  test('started, it passes on a timer while a track is current', () async {
    final real = Stopwatch()..start();
    final c = PlayCounter(
      player: player,
      nowMs: () => real.elapsedMilliseconds,
      onCounted: (item) => counted.add(item.id),
      every: const Duration(milliseconds: 5),
    )..start();
    addTearDown(c.dispose);
    player.state = _playing(const [
      _a,
    ], seconds: 0).copyWith(duration: () => const Duration(milliseconds: 100));
    await Future<void>.delayed(const Duration(milliseconds: 400));
    expect(counted, ['1']);
  });
}
