// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/app/device_platform.dart';
import 'package:neupod/app/neupod_app.dart';
import 'package:neupod/app/scan_service_platform.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';

Future<void> main() async {
  // First, before any override is built: the Android scan service registers
  // its channel handler when it is constructed, and a channel needs the
  // binding. Without this the app dies in main() on Android.
  WidgetsFlutterBinding.ensureInitialized();
  // Automatic retry is off (decision 0006): a provider that fails to open a
  // resource, such as the database, should fail visibly, not retry forever.
  final container = ProviderContainer(
    retry: (_, _) => null,
    // Haptics and the system volume from the neuPod device plugin; on
    // Android, the service that keeps the library scan going (0009).
    overrides: [...devicePlatformOverrides(), ...scanServiceOverrides()],
  );
  // The saved mode and looks are read before the first frame, so the device
  // is never painted in Auto or Harbour and then repainted. A small file; the
  // stores never throw.
  await Future.wait([
    container.read(styleChoiceProvider.notifier).loaded,
    container.read(customLooksProvider.notifier).loaded,
  ]).timeout(const Duration(seconds: 1), onTimeout: () => const []);
  // A stalled read opens in the defaults; the notifiers apply the stored
  // values when they arrive, and never over a change made meanwhile.
  runApp(
    UncontrolledProviderScope(container: container, child: const NeuPodApp()),
  );
}
