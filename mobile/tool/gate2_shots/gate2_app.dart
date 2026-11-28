// SPDX-License-Identifier: MPL-2.0

/// Shared set-up for the gate 2 screenshot tools: the real app (`NeuPodApp`,
/// so the device view, shell and every gate screen) over the gate library
/// and a fake player, with the style and the device view's switches preset.
/// Used by the simulator entry point (`gate2_sim_main.dart`) and the exact
/// renders (`gate2_renders_test.dart`). Nothing here is part of the app.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_riverpod/misc.dart' show Override;
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/neupod_app.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/device/device_options.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/style/neupod_style.dart';

import '../../test/fakes/fake_player.dart';

/// The two designed styles.
const Map<String, (StyleFamily, bool)> gate2Styles = {
  'classic_light': (StyleFamily.classic, false),
  'modern_dark': (StyleFamily.modern, true),
};

/// The mock-ups' device status: headphones, 78 % battery.
const gate2Status = DeviceStatus(
  battery: BatteryState(level: 0.78),
  output: OutputIcon.headphones,
  bluetooth: BluetoothIcon.saved,
);

/// A player state with [song] loaded, as the mock-ups' Home shows.
PlayerState gate2Playing(Song song, {double progress = 0.37}) {
  const total = Duration(minutes: 4, seconds: 1);
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
    format: const StreamFormat(
      codec: 'FLAC',
      sampleRate: 44100,
      channelCount: 2,
      bitsPerSample: 16,
    ),
  );
}

/// A container holding the real app's state: [style] ('classic_light' or
/// 'modern_dark'), the device view [options], a [player] and the gate
/// library.
ProviderContainer gate2Container({
  required String style,
  required FakePlayer player,
  DeviceViewOptions options = const DeviceViewOptions(),
  List<Override> overrides = const [],
}) {
  final (family, dark) = gate2Styles[style]!;
  final container = ProviderContainer(
    retry: (_, _) => null,
    overrides: [
      playerProvider.overrideWithValue(player),
      deviceStatusProvider.overrideWithValue(gate2Status),
      // Playing a song is not what the shots are about; the queue's order and
      // metadata are enough.
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
      ...overrides,
    ],
  );
  container
      .read(styleChoiceProvider.notifier)
      .set(
        StyleChoice(
          family: family,
          mode: dark ? ModeSetting.dark : ModeSetting.light,
        ),
      );
  container.read(deviceViewOptionsProvider.notifier).set(options);
  return container;
}

/// The song the shots play.
Future<Song> gate2Song(ProviderContainer c) async {
  final library = await c.read(libraryViewProvider.future);
  return (await library.songs()).firstWhere((s) => s.title == 'Salt Road');
}

/// The app inside [container], with [boundary] around it for capture.
Widget gate2Widget(ProviderContainer container, Key boundary) =>
    UncontrolledProviderScope(
      container: container,
      child: RepaintBoundary(key: boundary, child: const NeuPodApp()),
    );

/// Opens a screen on top of Home, as a press on its item would.
void gate2Push(ProviderContainer c, String name, WidgetBuilder builder) => c
    .read(shellProvider.notifier)
    .push(ShellPage(name: name, builder: builder));
