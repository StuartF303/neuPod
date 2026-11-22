// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/ui/device/device_view.dart';
import 'package:neupod/ui/features/licences/licences_data.dart';
import 'package:neupod/ui/screens/browse/browse_root.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_screen.dart';
import 'package:neupod/ui/screens/search/search_screen.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
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
///
/// The library is scanned at launch and each time the app comes back to the
/// front, when there is any folder (on iOS the Documents folder always is),
/// so music copied in with Finder or the Files app appears by itself. The
/// scanner reads only new or changed files. A scan the user cancelled is not
/// started again on return to the front.
///
/// A scan that fails, however it was started, says why in a panel (the
/// firmware shows `library_status_text()` for a failed build, in its pane or
/// as a toast); one the user cancelled says nothing, as on the firmware.
class NeuPodApp extends ConsumerStatefulWidget {
  const NeuPodApp({super.key});

  @override
  ConsumerState<NeuPodApp> createState() => _NeuPodAppState();
}

class _NeuPodAppState extends ConsumerState<NeuPodApp> {
  late final AppLifecycleListener _lifecycle;

  @override
  void initState() {
    super.initState();
    unawaited(ref.read(libraryScanProvider.notifier).startIfSources());
    _lifecycle = AppLifecycleListener(
      onResume: () => unawaited(
        ref.read(libraryScanProvider.notifier).startIfSources(resume: true),
      ),
    );
  }

  @override
  void dispose() {
    _lifecycle.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    ref.listen(libraryScanProvider, (was, now) {
      final ended = was != null && !was.done && now != null && now.done;
      final error = now?.error;
      if (ended && error != null && error != scanCancelled) {
        // The raw error is for the log, never the panel.
        debugPrint('neupod: library scan failed: $error');
        unawaited(
          ref
              .read(shellProvider.notifier)
              .showPanel(
                title: scanFailureText(error),
                message: scanFailureHint(error),
              ),
        );
      }
    });
    final mode = ref.watch(styleChoiceProvider.select((c) => c.mode));
    // Now Playing and Search plug into the shell (idempotent).
    registerGateScreens(ref.read(shellRegistryProvider));
    // The bundled fonts' and icons' licences join Flutter's (idempotent).
    registerBundledLicences();
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

/// Registers Now Playing and Search with the shell's registry. Each screen
/// leaves through the shell (Menu pops; the queue route is not built yet and
/// stays a stand-in).
void registerGateScreens(ShellRegistry registry) {
  registry
    ..register(ShellDestination.nowPlaying, (_) => const NowPlayingScreen())
    ..register(ShellDestination.search, (_) => const SearchScreen());
}
