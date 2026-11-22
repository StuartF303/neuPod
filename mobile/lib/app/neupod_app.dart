// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/ui/device/device_view.dart';
import 'package:neupod/ui/screens/browse/browse_root.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/features/player_debug/player_debug_screen.dart';

/// The root widget. The composition root (this folder) chooses the theme and,
/// later, the router; screens live under `ui/features/`.
///
/// The app is the device (owner decision 9): the root is the device view,
/// whose 4:3 screen shows the browsing screens (`BrowseRoot`), a stack of
/// screens from Home. The body follows the mode the owner pinned in Settings,
/// or the phone's. In debug
/// builds the player debug screen stays reachable from the gate panel (a long
/// press on the device's body).
class NeuPodApp extends ConsumerWidget {
  const NeuPodApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final mode = ref.watch(styleChoiceProvider.select((c) => c.mode));
    return MaterialApp(
      title: 'neuPod',
      debugShowCheckedModeBanner: false,
      home: DeviceView(
        screen: const BrowseRoot(),
        brightness: switch (mode) {
          null => null,
          StyleMode.light => Brightness.light,
          StyleMode.dark => Brightness.dark,
        },
      ),
      // The player debug screen exists only in debug builds.
      routes: {if (kDebugMode) PlayerDebugScreen.routeName: _playerDebug},
    );
  }
}

Widget _playerDebug(BuildContext context) => const PlayerDebugScreen();
