// SPDX-License-Identifier: MPL-2.0

/// Settings, as far as the gate needs: Style (Classic or Modern) and Mode
/// (Auto, Light or Dark), the choices the specification puts in Settings
/// ("style is chosen in Settings; mode follows the phone by default"), and
/// Library (the folders: add, remove, rescan). Select changes a value in
/// place, or opens Library. The rest of Settings is phase 6.
///
/// Classic Dark and Modern Light are not designed (UI styles Open question
/// 1): they show the style's designed mode while the body follows the mode.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/library_screen.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// Settings.
class SettingsScreen extends ConsumerWidget {
  /// Settings.
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final choice = ref.watch(styleChoiceProvider);
    final notifier = ref.read(styleChoiceProvider.notifier);
    return ShellFrame(
      title: 'Settings',
      body: BrowseListBody(
        child: Builder(
          builder: (context) {
            final h = NeupodTheme.of(context).tier.rowHeight;
            return NeupodList(
              indexed: false,
              items: [
                NeupodListItem.row(
                  height: h,
                  title: 'Style',
                  value: choice.family == StyleFamily.classic
                      ? 'Classic'
                      : 'Modern',
                  onActivate: () => notifier.set(
                    StyleChoice(
                      family: choice.family == StyleFamily.classic
                          ? StyleFamily.modern
                          : StyleFamily.classic,
                      mode: choice.mode,
                    ),
                  ),
                ),
                NeupodListItem.row(
                  height: h,
                  title: 'Mode',
                  value: switch (choice.mode) {
                    null => 'Auto',
                    StyleMode.light => 'Light',
                    StyleMode.dark => 'Dark',
                  },
                  onActivate: () => notifier.set(
                    StyleChoice(
                      family: choice.family,
                      mode: switch (choice.mode) {
                        null => StyleMode.light,
                        StyleMode.light => StyleMode.dark,
                        StyleMode.dark => null,
                      },
                    ),
                  ),
                ),
                NeupodListItem.row(
                  height: h,
                  title: 'Library',
                  chevron: true,
                  onActivate: () =>
                      pushScreen(ref, 'Library', (_) => const LibraryScreen()),
                ),
              ],
            );
          },
        ),
      ),
    );
  }
}
