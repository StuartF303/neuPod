// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/device_platform.dart';
import 'package:neupod/app/neupod_app.dart';

void main() {
  runApp(
    // Automatic retry is off (decision 0006): a provider that fails to open a
    // resource, such as the database, should fail visibly, not retry forever.
    ProviderScope(
      retry: (_, _) => null,
      // Haptics and the system volume from the neuPod device plugin.
      overrides: devicePlatformOverrides(),
      child: const NeuPodApp(),
    ),
  );
}
