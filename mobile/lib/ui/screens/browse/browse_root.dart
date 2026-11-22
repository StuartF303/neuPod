// SPDX-License-Identifier: MPL-2.0

/// What the device shows: the shell rooted at Home. `NeuPodApp` hands this to
/// `DeviceView(screen: ...)`.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/screens/browse/home_screen.dart';
import 'package:neupod/ui/screens/shell/shell_root.dart';

/// The device's screens, from Home.
class BrowseRoot extends StatelessWidget {
  /// The browsing screens.
  const BrowseRoot({super.key});

  @override
  Widget build(BuildContext context) =>
      ShellRoot(home: (_) => const HomeScreen());
}
