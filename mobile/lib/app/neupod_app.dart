// SPDX-License-Identifier: MPL-2.0

import 'package:material_ui/material_ui.dart';
import 'package:neupod/ui/features/home/home_screen.dart';

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
    );
  }
}
