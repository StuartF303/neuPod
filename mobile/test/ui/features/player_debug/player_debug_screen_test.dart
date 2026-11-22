// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod/ui/features/player_debug/player_debug_screen.dart';

import '../../../fakes/fake_player.dart';

void main() {
  const item = QueueItem(id: 'a', uri: 'file:///a.flac', title: 'Tone');

  Future<FakePlayer> pumpScreen(WidgetTester tester, PlayerState state) async {
    final player = FakePlayer(state)
      ..session = const MediaSessionSnapshot(title: 'Tone', hasArtwork: true);
    await tester.pumpWidget(
      ProviderScope(
        retry: (_, _) => null,
        overrides: [playerProvider.overrideWithValue(player)],
        child: const MaterialApp(home: PlayerDebugScreen()),
      ),
    );
    await tester.pumpAndSettle();
    return player;
  }

  testWidgets('shows the player state and the media session', (tester) async {
    await pumpScreen(
      tester,
      const PlayerState(
        status: PlaybackStatus.ready,
        queue: [item],
        index: 0,
        position: Duration(milliseconds: 1500),
        duration: Duration(seconds: 3),
        format: StreamFormat(codec: 'flac', sampleRate: 44100),
      ),
    );

    expect(find.text('Status: ready'), findsOneWidget);
    expect(find.text('Item: 1 of 1: Tone'), findsOneWidget);
    expect(find.text('0:01.5 / 0:03.0'), findsOneWidget);
    expect(find.textContaining('StreamFormat(flac, 44100 Hz'), findsOneWidget);
    expect(find.textContaining('MediaSessionSnapshot(Tone'), findsOneWidget);
    expect(find.text('Play'), findsOneWidget);
  });

  testWidgets('play, pause and skip reach the player', (tester) async {
    final player = await pumpScreen(
      tester,
      const PlayerState(status: PlaybackStatus.ready, queue: [item], index: 0),
    );

    await tester.tap(find.text('Play'));
    await tester.pumpAndSettle();
    expect(find.text('Pause'), findsOneWidget);
    await tester.tap(find.text('Pause'));
    await tester.tap(find.text('Next'));
    await tester.tap(find.text('Previous'));
    await tester.pumpAndSettle();

    expect(player.calls, ['play', 'pause', 'skipToNext', 'skipToPrevious']);
  });

  testWidgets('plays the URIs typed in, as a queue', (tester) async {
    final player = await pumpScreen(tester, PlayerState.empty);

    await tester.enterText(
      find.byKey(const Key('uris')),
      'content://media/external/audio/media/12\nfile:///b.mp3',
    );
    await tester.tap(find.text('Play URIs'));
    await tester.pumpAndSettle();

    expect(player.calls, [
      'setQueue(content://media/external/audio/media/12, file:///b.mp3)',
      'play',
    ]);
  });
}
