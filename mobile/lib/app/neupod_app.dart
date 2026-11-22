// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/foundation.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/ui/features/home/home_screen.dart';
import 'package:neupod/ui/features/player_debug/player_debug_screen.dart';

/// The root widget. The composition root (this folder) chooses the theme and,
/// later, the router; screens live under `ui/features/`.
class NeuPodApp extends StatelessWidget {
  const NeuPodApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      title: 'neuPod',
      debugShowCheckedModeBanner: false,
      home: HomeScreen(),
      // The player debug screen exists only in debug builds.
      routes: {if (kDebugMode) PlayerDebugScreen.routeName: _playerDebug},
    );
  }
}

Widget _playerDebug(BuildContext context) => const PlayerDebugScreen();
