// SPDX-License-Identifier: MPL-2.0

// Shared set-up for the browsing screens' tests: the gate library (the
// committed copy of the generated test library's manifest), a fake player,
// the shell on a 600x450 canvas, and the wheel.

import 'dart:io';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_riverpod/misc.dart' show Override;
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/play_stats_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/play_stats.dart';

import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/shell/shell_root.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/style/neupod_style.dart';

import '../../fakes/fake_player.dart';
import '../../fakes/fake_track_stats_store.dart';
import '../../support/manifest_library_view.dart';

/// The gate library, from the committed manifest.
final ManifestLibraryView gateLibrary = ManifestLibraryView.parse(
  File('test/support/gate_library/manifest.json').readAsStringSync(),
);

/// A text scale for each tier (Android's thresholds, the test platform's).
const Map<String, double> tierScales = {
  'standard': 1.0,
  'large': 1.2,
  'largest': 1.4,
};

/// Records the haptics.
class RecordedHaptics implements HapticsPort {
  /// What was played.
  final List<FeedbackKind> played = [];

  @override
  void prepare() {}

  @override
  void play(FeedbackKind kind) => played.add(kind);
}

/// What a test holds on to.
class ShellRig {
  ShellRig(this.container, this.player, this.haptics, this.trackStats);

  final ProviderContainer container;
  final FakePlayer player;
  final RecordedHaptics haptics;

  /// The ratings and play counts the screens read and write.
  final TrackStatsStore trackStats;

  WheelBus get bus => container.read(wheelBusProvider);

  /// A wheel step.
  void step(int n) => bus.emit(WheelStep(n, 1));

  /// A complete press.
  void press(WheelButton b) => bus.press(b);
}

/// A player state with [song] loaded, as the mock-ups' Home shows.
PlayerState playingState(Song song, {double progress = 0.37}) {
  const total = Duration(minutes: 4);
  return PlayerState(
    status: PlaybackStatus.ready,
    playWhenReady: true,
    queue: [
      QueueItem(
        id: song.id,
        uri: 'file:///x',
        title: song.title,
        artist: song.artist,
        album: song.album,
      ),
    ],
    index: 0,
    duration: total,
    position: total * progress,
  );
}

/// Pumps [home] in the shell at 600x450 in [family] and [dark], at text
/// [scale]. Returns the rig.
Future<ShellRig> pumpShell(
  WidgetTester tester, {
  required WidgetBuilder home,
  StyleFamily family = StyleFamily.classic,
  bool dark = false,
  double scale = 1.0,
  bool reduceMotion = false,
  PlayerState? playing,
  DeviceStatus? status,
  ShellRegistrySetup? registry,
  List<Override> overrides = const [],
  LibraryView? library,
  TrackStatsStore? trackStats,
}) async {
  tester.view.physicalSize = const Size(600, 450);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final player = FakePlayer(playing ?? PlayerState.empty);
  final haptics = RecordedHaptics();
  final stats = trackStats ?? FakeTrackStatsStore();
  final container = ProviderContainer.test(
    retry: (_, _) => null,
    overrides: [
      libraryViewProvider.overrideWith((ref) => library ?? gateLibrary),
      // The real mapper copies test tones out of the asset bundle; the
      // screens' tests only need the queue's order and metadata.
      queueMapperProvider.overrideWithValue(
        (songs, {int start = 0}) async => [
          for (final s in songs)
            if (s.playable)
              QueueItem(
                id: s.id,
                uri: 'file:///gate/${s.id}',
                title: s.title,
                artist: s.artist,
                album: s.album,
              ),
        ],
      ),
      playerProvider.overrideWithValue(player),
      trackStatsStoreProvider.overrideWithValue(stats),
      hapticsPortProvider.overrideWithValue(haptics),
      // The wheel's clock is the test's own (it moves with pump).
      wheelClockProvider.overrideWithValue(
        () => tester.binding.clock.now().millisecondsSinceEpoch,
      ),
      if (status != null) deviceStatusProvider.overrideWithValue(status),
      ...overrides,
    ],
  );
  container
      .read(styleChoiceProvider.notifier)
      .set(
        StyleChoice(
          family: family,
          mode: dark ? StyleMode.dark : StyleMode.light,
        ),
      );
  registry?.call(container);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      // A new key: a second rig in one test starts from a fresh tree.
      key: UniqueKey(),
      container: container,
      child: MediaQuery(
        data: MediaQueryData(
          size: const Size(600, 450),
          textScaler: TextScaler.linear(scale),
          platformBrightness: dark ? Brightness.dark : Brightness.light,
          disableAnimations: reduceMotion,
        ),
        child: RepaintBoundary(
          key: const ValueKey('device'),
          child: ShellRoot(home: home),
        ),
      ),
    ),
  );
  await settle(tester);
  return ShellRig(container, player, haptics, stats);
}

/// Called with the container before the first frame.
typedef ShellRegistrySetup = void Function(ProviderContainer container);

/// Pumps until the screens have loaded and animations are done.
Future<void> settle(WidgetTester tester) async {
  await tester.pump();
  await tester.pump();
  await tester.pumpAndSettle();
}

/// The mock-ups' device status: headphones, 78 % battery.
const mockStatus = DeviceStatus(
  battery: BatteryState(level: 0.78),
  output: OutputIcon.headphones,
  bluetooth: BluetoothIcon.saved,
);
