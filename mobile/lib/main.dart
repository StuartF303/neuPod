// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/device_platform.dart';
import 'package:neupod/app/neupod_app.dart';
import 'package:neupod/app/scan_service_platform.dart';

void main() {
  // First, before any override is built: the Android scan service registers
  // its channel handler when it is constructed, and a channel needs the
  // binding. Without this the app dies in main() on Android.
  WidgetsFlutterBinding.ensureInitialized();
  runApp(
    // Automatic retry is off (decision 0006): a provider that fails to open a
    // resource, such as the database, should fail visibly, not retry forever.
    ProviderScope(
      retry: (_, _) => null,
      // Haptics and the system volume from the neuPod device plugin; on
      // Android, the service that keeps the library scan going (0009).
      overrides: [...devicePlatformOverrides(), ...scanServiceOverrides()],
      child: const NeuPodApp(),
    ),
  );
}
