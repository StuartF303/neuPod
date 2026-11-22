// SPDX-License-Identifier: MPL-2.0

import 'package:material_ui/material_ui.dart';
import 'package:neupod/ui/device/device_view.dart';
import 'package:neupod/ui/device/placeholder_screen.dart';

/// The root widget. The composition root (this folder) chooses the theme and,
/// later, the router; screens live under `ui/features/`.
///
/// The app is the device (owner decision 9): the root is the device view,
/// whose 4:3 screen shows a placeholder until the real screens land.
class NeuPodApp extends StatelessWidget {
  const NeuPodApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      title: 'neuPod',
      debugShowCheckedModeBanner: false,
      home: DeviceView(screen: PlaceholderScreen()),
    );
  }
}
