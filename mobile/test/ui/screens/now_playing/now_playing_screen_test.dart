// SPDX-License-Identifier: MPL-2.0

// Now Playing wired to the wheel, the player and the system volume: the
// volume bar and its 1.2 s return, the failure that must be seen (owner
// decision 10), seek mode, scan, skip, page 2 and the screen-reader value.

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_riverpod/misc.dart' show Override;
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod/domain/wheel/system_volume_port.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/bars.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/system_volume.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_options.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_screen.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_view.dart';
import 'package:neupod/ui/screens/panel/panel_layer.dart';

import '../../../fakes/fake_player.dart';
import '../../device/device_test_harness.dart';
import '../screens_test_support.dart';

const _item = QueueItem(
  id: 't1',
  uri: 'file:///a.flac',
  title: 'Harbour Lights',
  artist: 'Aurora Lane',
  album: 'First Light',
);

PlayerState _playing({Duration? duration = const Duration(seconds: 240)}) =>
    PlayerState(
      status: PlaybackStatus.ready,
      playWhenReady: true,
      queue: const [_item, _item],
      index: 0,
      position: const Duration(seconds: 90),
      duration: duration,
      format: const StreamFormat(
        codec: 'flac',
        sampleRate: 44100,
        bitsPerSample: 16,
      ),
    );

class _Rig {
  _Rig(this.tester, this.player, this.volume, this.clock, this.haptics);

  final WidgetTester tester;
  final FakePlayer player;
  final RecordingVolume volume;
  final TestClock clock;
  final RecordingHaptics haptics;
  late ProviderContainer container;
  int backs = 0;
  int queues = 0;

  WheelBus get bus => container.read(wheelBusProvider);

  Future<void> advance(int ms) async {
    clock.ms += ms;
    await tester.pump(Duration(milliseconds: ms));
  }

  /// A full press, as the wheel produces it.
  Future<void> press(WheelButton b, {int holdMs = 0}) async {
    bus.emit(WheelButtonDown(b));
    await tester.pump();
    if (holdMs > 0) {
      await advance(holdMs);
      bus.emit(WheelHoldStart(b));
      bus.emit(WheelButtonUp(b));
      bus.emit(WheelHoldEnd(b));
    } else {
      bus.emit(WheelButtonUp(b));
      bus.emit(WheelPress(b));
    }
    await tester.pump();
  }

  Future<void> turn(int steps) async {
    bus.emit(WheelStep(steps, 1));
    await tester.pump();
  }
}

Future<_Rig> _pump(
  WidgetTester tester, {
  PlayerState? state,
  SystemVolumeStatus? failWith,
  bool throwOnRead = false,
}) async {
  tester.view.physicalSize = const Size(600, 450);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final player = FakePlayer(state ?? _playing());
  final volume = RecordingVolume(failWith: failWith, throwOnRead: throwOnRead);
  final clock = TestClock();
  final haptics = RecordingHaptics();
  final rig = _Rig(tester, player, volume, clock, haptics);
  final List<Override> overrides = [
    playerProvider.overrideWithValue(player),
    systemVolumeProvider.overrideWithValue(volume),
    wheelClockProvider.overrideWithValue(() => clock.ms),
    hapticsPortProvider.overrideWithValue(haptics),
  ];
  rig.container = ProviderContainer.test(overrides: overrides);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: rig.container,
      child: Directionality(
        textDirection: TextDirection.ltr,
        child: MediaQuery(
          data: const MediaQueryData(size: Size(600, 450)),
          child: NowPlayingScreen(
            onBack: () => rig.backs++,
            onOpenQueue: () => rig.queues++,
          ),
        ),
      ),
    ),
  );
  await tester.pump();
  return rig;
}

NowPlayingView _view(WidgetTester tester) =>
    tester.widget<NowPlayingView>(find.byType(NowPlayingView));

void main() {
  testWidgets('at rest: the title, the format line and progress', (
    tester,
  ) async {
    await _pump(tester);
    expect(find.text('Harbour Lights'), findsOneWidget);
    expect(find.text('FLAC 16/44.1'), findsOneWidget);
    expect(find.text('1:30'), findsOneWidget);
    expect(find.text('-2:30'), findsOneWidget);
    expect(find.text('1 of 2'), findsOneWidget);
    expect(find.byType(NeupodProgressBar), findsOneWidget);
    expect(find.byType(NeupodVolumeBar), findsNothing);
  });

  testWidgets('the wheel sets the system volume, one step each, with a detent '
      'per step, and the bar goes back 1.2 s after rest', (tester) async {
    final rig = await _pump(tester);
    await rig.turn(2);
    await tester.pump();
    expect(rig.volume.asked, [1, 1]);
    expect(rig.volume.current, 10);
    expect(rig.haptics.played, isNotEmpty, reason: 'a detent for the steps');
    expect(find.byType(NeupodVolumeBar), findsOneWidget);
    expect(find.byType(NeupodProgressBar), findsNothing);
    expect(rig.bus.semantics.value.label, 'Volume');
    expect(rig.bus.semantics.value.value, '63 percent');

    await tester.pump(const Duration(milliseconds: 1100));
    expect(find.byType(NeupodVolumeBar), findsOneWidget);
    await tester.pump(const Duration(milliseconds: 150));
    expect(find.byType(NeupodVolumeBar), findsNothing);
    expect(find.byType(NeupodProgressBar), findsOneWidget);

    await rig.turn(-1);
    await tester.pump();
    expect(rig.volume.asked.last, -1);
  });

  testWidgets('a volume step that did not move shows the read-back level, '
      'no detent, and makes the failure visible', (tester) async {
    final rig = await _pump(tester, failWith: SystemVolumeStatus.noSlider);
    await rig.turn(1);
    await tester.pump();
    // The bar shows the level the system reports (8 of 16), as in a store
    // build, where nothing else says the step failed.
    expect(find.byType(NeupodVolumeBar), findsOneWidget);
    expect(find.byType(NeupodProgressBar), findsNothing);
    expect(rig.bus.semantics.value.label, 'Volume');
    expect(rig.bus.semantics.value.value, '50 percent');
    final failure = rig.container.read(volumeFailureProvider);
    expect(failure?.status, SystemVolumeStatus.noSlider);
    expect(failure?.count, 1);
    // Debug and gate builds say so in the status bar.
    expect(find.text("Can't set volume: noSlider"), findsOneWidget);
    expect(rig.haptics.played, isEmpty, reason: 'no detent when nothing moved');
    await tester.pump(messageHold + const Duration(milliseconds: 10));
    expect(find.text("Can't set volume: noSlider"), findsNothing);
    expect(find.byType(NeupodProgressBar), findsOneWidget);
  });

  testWidgets('a volume port that throws is recorded as an error, not as '
      'a step that did not move', (tester) async {
    final rig = await _pump(tester, throwOnRead: true);
    final failure = rig.container.read(volumeFailureProvider);
    expect(failure, isNotNull);
    expect(failure?.status, isNull);
    expect(failure?.error, isA<StateError>());
    expect(failure.toString(), contains('error'));
  });

  testWidgets('a cancelled centre press does nothing; a cancelled Next '
      'drops its scan', (tester) async {
    final rig = await _pump(tester);
    rig.bus.emit(const WheelButtonDown(WheelButton.centre));
    await rig.advance(100);
    rig.bus.emit(const WheelButtonCancelled(WheelButton.centre));
    await rig.advance(600);
    expect(_view(tester).mode, NowPlayingMode.rest, reason: 'no page 2');

    rig.bus.emit(const WheelButtonDown(WheelButton.next));
    await rig.advance(700); // scanning
    await rig.advance(500);
    rig.bus.emit(const WheelButtonCancelled(WheelButton.next));
    await rig.advance(600);
    expect(rig.player.calls, isEmpty, reason: 'nothing skipped or applied');
    expect(_view(tester).data.position, const Duration(seconds: 90));
  });

  testWidgets('a panel on top stops the wheel reaching Now Playing', (
    tester,
  ) async {
    final rig = await _pump(tester);
    rig.container
        .read(panelProvider.notifier)
        .show(const PanelRequest(title: 'Lost', message: 'Gone'));
    await rig.turn(2);
    await rig.press(WheelButton.next);
    await rig.press(WheelButton.playPause);
    expect(rig.volume.asked, isEmpty);
    expect(rig.player.calls, isEmpty);
    rig.container.read(panelProvider.notifier).dismiss();
    await rig.turn(1);
    expect(rig.volume.asked, [1]);
  });

  testWidgets('the tick runs only while there is something to tick', (
    tester,
  ) async {
    final rig = await _pump(tester);
    // Idle: no timer pending beyond the volume's.
    expect(tester.binding.transientCallbackCount, 0);
    await rig.press(WheelButton.centre, holdMs: 520);
    await rig.advance(60);
    expect(_view(tester).mode, NowPlayingMode.seek);
    await rig.press(WheelButton.menu); // leaves seek mode
    await rig.advance(100);
    // Nothing left to tick: advancing 10 s does not throw or change a thing.
    await rig.advance(10000);
    expect(_view(tester).mode, NowPlayingMode.rest);
  });

  testWidgets('going to the background leaves seek mode with nothing applied', (
    tester,
  ) async {
    final rig = await _pump(tester);
    await rig.press(WheelButton.centre, holdMs: 520);
    await rig.advance(60);
    await rig.turn(1);
    expect(_view(tester).mode, NowPlayingMode.seek);
    for (final st in [
      AppLifecycleState.inactive,
      AppLifecycleState.hidden,
      AppLifecycleState.paused,
    ]) {
      tester.binding.handleAppLifecycleStateChanged(st);
    }
    // Frames stop while paused: come back to see what it left.
    tester.binding.handleAppLifecycleStateChanged(AppLifecycleState.resumed);
    await tester.pump();
    expect(_view(tester).mode, NowPlayingMode.rest);
    expect(rig.player.calls.where((c) => c.startsWith('seek')), isEmpty);
  });

  testWidgets('holding the centre 500 ms enters seek mode; the wheel moves '
      'the target by 1/60 of the track; Select applies it', (tester) async {
    final rig = await _pump(tester);
    expect(rig.bus.holdMs.keys, contains(WheelButton.centre));
    await rig.press(WheelButton.centre, holdMs: 520);
    await rig.advance(60);
    expect(_view(tester).mode, NowPlayingMode.seek);
    expect(find.byType(SeekBar), findsOneWidget);
    expect(rig.bus.holdMs.keys, isNot(contains(WheelButton.centre)));
    expect(rig.bus.semantics.value.label, 'Seek');

    await rig.turn(2); // 240 s / 60 = 4 s a step
    expect(rig.volume.asked, isEmpty, reason: 'seek mode: not the volume');
    expect(_view(tester).seekTarget, const Duration(seconds: 98));
    expect(find.text('1:38'), findsOneWidget);

    await rig.press(WheelButton.centre); // go
    await rig.advance(60);
    expect(rig.player.calls, contains('seek(98000)'));
    expect(_view(tester).mode, NowPlayingMode.rest);
  });

  testWidgets('Menu in seek mode cancels it and does not leave', (
    tester,
  ) async {
    final rig = await _pump(tester);
    await rig.press(WheelButton.centre, holdMs: 520);
    await rig.advance(60);
    await rig.turn(1);
    await rig.press(WheelButton.menu);
    expect(_view(tester).mode, NowPlayingMode.rest);
    expect(rig.backs, 0);
    expect(rig.player.calls.where((c) => c.startsWith('seek')), isEmpty);
    await rig.press(WheelButton.menu);
    expect(rig.backs, 1);
  });

  testWidgets('seek mode left alone for 10 s is cancelled', (tester) async {
    final rig = await _pump(tester);
    await rig.press(WheelButton.centre, holdMs: 520);
    await rig.advance(60);
    expect(_view(tester).mode, NowPlayingMode.seek);
    await rig.advance(10100);
    expect(_view(tester).mode, NowPlayingMode.rest);
    expect(rig.player.calls.where((c) => c.startsWith('seek')), isEmpty);
  });

  testWidgets('a track that cannot seek answers the hold with a message', (
    tester,
  ) async {
    final rig = await _pump(tester, state: _playing(duration: null));
    await rig.press(WheelButton.centre, holdMs: 520);
    await rig.advance(60);
    expect(find.text('Seek not available'), findsOneWidget);
    expect(_view(tester).mode, NowPlayingMode.rest);
  });

  testWidgets('Previous and Next: a press skips, a hold scans and applies '
      'after release', (tester) async {
    final rig = await _pump(tester);
    await rig.press(WheelButton.next);
    expect(rig.player.calls, contains('skipToNext'));
    await rig.press(WheelButton.previous);
    expect(rig.player.calls, contains('skipToPrevious'));

    rig.player.calls.clear();
    rig.bus.emit(const WheelButtonDown(WheelButton.next));
    await rig.advance(700); // past 600 ms: scanning at 10x
    await rig.advance(1000);
    expect(
      _view(tester).data.position,
      greaterThan(const Duration(seconds: 95)),
    );
    rig.bus.emit(const WheelHoldStart(WheelButton.next));
    rig.bus.emit(const WheelButtonUp(WheelButton.next));
    rig.bus.emit(const WheelHoldEnd(WheelButton.next));
    await rig.advance(60);
    expect(rig.player.calls.where((c) => c == 'skipToNext'), isEmpty);
    expect(rig.player.calls.any((c) => c.startsWith('seek(')), isTrue);
  });

  testWidgets('a short centre press opens page 2 and its release is not '
      'taken twice', (tester) async {
    final rig = await _pump(tester);
    await rig.press(WheelButton.centre);
    expect(_view(tester).mode, NowPlayingMode.page2);
    expect(_view(tester).ratingEditing, isFalse);
    expect(find.text('Shuffle'), findsOneWidget);
    expect(find.text('Queue'), findsOneWidget);

    // Shuffle, Repeat and the Queue route.
    await rig.turn(1);
    await rig.press(WheelButton.centre);
    expect(rig.container.read(playbackChoicesProvider).shuffle, isTrue);
    await rig.turn(1);
    await rig.press(WheelButton.centre);
    expect(rig.container.read(playbackChoicesProvider).repeat, RepeatState.all);
    await rig.turn(1);
    await rig.press(WheelButton.centre);
    expect(rig.queues, 1);

    await rig.press(WheelButton.menu); // back to page 1
    expect(_view(tester).mode, NowPlayingMode.rest);
    expect(rig.backs, 0);
  });

  testWidgets('page 2: Select on Rating lets the wheel set the stars', (
    tester,
  ) async {
    final rig = await _pump(tester);
    await rig.press(WheelButton.centre);
    await rig.press(WheelButton.centre); // edit
    expect(_view(tester).ratingEditing, isTrue);
    await rig.turn(3);
    expect(rig.container.read(playbackChoicesProvider).ratings['t1'], 3);
    expect(_view(tester).data.rating, 3);
    await rig.press(WheelButton.menu); // done, still on page 2
    expect(_view(tester).ratingEditing, isFalse);
    expect(_view(tester).mode, NowPlayingMode.page2);
  });

  testWidgets('Play/Pause toggles the player', (tester) async {
    final rig = await _pump(tester);
    await rig.press(WheelButton.playPause);
    expect(rig.player.calls, ['pause']);
  });

  testWidgets('the screen holds its holds and gives the hold haptic itself, '
      'and puts everything back when it goes', (tester) async {
    final rig = await _pump(tester);
    final bus = rig.bus;
    expect(
      bus.screenGivesHoldHaptic,
      containsAll(<WheelButton>[
        WheelButton.previous,
        WheelButton.next,
        WheelButton.centre,
      ]),
    );
    await tester.pumpWidget(const SizedBox());
    expect(bus.holdMs, isEmpty);
    expect(bus.screenGivesHoldHaptic, isEmpty);
  });
}
