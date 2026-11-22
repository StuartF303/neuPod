// SPDX-License-Identifier: MPL-2.0

/// Entry point for the gate 2 simulator shots; NOT the app's `main.dart`, so
/// nothing here reaches a release build. The scene comes from the environment
/// (`SIMCTL_CHILD_<NAME>` on `xcrun simctl launch`), see `capture_sim.sh`:
///
/// - `GATE2_STYLE`: `classic_light` or `modern_dark`;
/// - `GATE2_FIT`: `a` or `b`; `GATE2_BODY`: `auto`, `silver` or `black`;
/// - `GATE2_STATUS`: `1` shows the phone's status bar, `0` hides it;
/// - `GATE2_WORDMARK`: `1` or `0`;
/// - `GATE2_SCENE`: `home` or `nowplaying` (a song is loaded either way).
library;

import 'dart:io';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/neupod_app.dart';
import 'package:neupod/ui/device/device_geometry.dart';
import 'package:neupod/ui/device/device_options.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';

import '../../test/fakes/fake_player.dart';
import 'gate2_app.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  // The scene file (KEY=value lines, written by capture_sim.sh) wins over the
  // environment, which simctl does not always hand to a debug build.
  final env = {...Platform.environment};
  final file = File('/tmp/neupod-gate2-scene.env');
  if (file.existsSync()) {
    for (final line in file.readAsLinesSync()) {
      final i = line.indexOf('=');
      if (i > 0) env[line.substring(0, i)] = line.substring(i + 1);
    }
  }
  final style = env['GATE2_STYLE'] ?? 'classic_light';
  final scene = env['GATE2_SCENE'] ?? 'home';
  final options = DeviceViewOptions(
    fit: env['GATE2_FIT'] == 'b'
        ? DeviceFit.fixedProportions
        : DeviceFit.fillsPhone,
    body: switch (env['GATE2_BODY']) {
      'silver' => BodySetting.silver,
      'black' => BodySetting.black,
      _ => BodySetting.automatic,
    },
    showStatusBar: env['GATE2_STATUS'] != '0',
    showWordmark: env['GATE2_WORDMARK'] != '0',
  );
  final player = FakePlayer();
  final container = gate2Container(
    style: style,
    options: options,
    player: player,
  );
  player.state = gate2Playing(await gate2Song(container));
  runApp(
    UncontrolledProviderScope(container: container, child: const NeuPodApp()),
  );
  if (scene == 'nowplaying') {
    await Future<void>.delayed(const Duration(milliseconds: 800));
    container.read(shellProvider.notifier).open(ShellDestination.nowPlaying);
  }
}
