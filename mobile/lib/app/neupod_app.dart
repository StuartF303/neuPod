// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/foundation.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/ui/device/device_view.dart';
import 'package:neupod/ui/device/placeholder_screen.dart';
import 'package:neupod/ui/features/player_debug/player_debug_screen.dart';

/// The root widget. The composition root (this folder) chooses the theme and,
/// later, the router; screens live under `ui/features/`.
///
/// The app is the device (owner decision 9): the root is the device view,
/// whose 4:3 screen shows a placeholder until the real screens land. In debug
/// builds the player debug screen stays reachable from the gate panel (a long
/// press on the device's body).
class NeuPodApp extends StatelessWidget {
  const NeuPodApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      title: 'neuPod',
      debugShowCheckedModeBanner: false,
      home: DeviceView(screen: PlaceholderScreen()),
      // The player debug screen exists only in debug builds.
      routes: {if (kDebugMode) PlayerDebugScreen.routeName: _playerDebug},
    );
  }
}

Widget _playerDebug(BuildContext context) => const PlayerDebugScreen();
