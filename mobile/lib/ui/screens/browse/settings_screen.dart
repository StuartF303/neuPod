// SPDX-License-Identifier: MPL-2.0

/// Settings, as far as the gate needs: Style (Classic or Modern), Mode
/// (Light, Dark, Auto or Custom) and the Custom look row that opens the list
/// of looks (AP-1), the choices the specification puts in Settings
/// ("style is chosen in Settings; mode follows the phone by default"), and
/// Library (the folders: add, remove, rescan), then Licences. Select changes
/// a value in place, or opens Library or Licences. The rest of Settings is
/// phase 6.
///
/// Classic Dark and Modern Light are not designed (UI styles Open question
/// 1): they show the style's designed mode while the body follows the mode.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/custom/looks_screen.dart';
import 'package:neupod/ui/screens/browse/library_screen.dart';
import 'package:neupod/ui/screens/browse/licences_screen.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/domain/style/custom_look.dart';
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
    final lookName =
        ref.watch(customLooksProvider.select((l) => l.inUse?.name)) ?? 'None';
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
                    ModeSetting.auto => 'Auto',
                    ModeSetting.light => 'Light',
                    ModeSetting.dark => 'Dark',
                    ModeSetting.custom => 'Custom',
                  },
                  onActivate: () => notifier.set(
                    StyleChoice(
                      family: choice.family,
                      // The owner's order: Light, Dark, Auto, Custom.
                      mode: switch (choice.mode) {
                        ModeSetting.light => ModeSetting.dark,
                        ModeSetting.dark => ModeSetting.auto,
                        ModeSetting.auto => ModeSetting.custom,
                        ModeSetting.custom => ModeSetting.light,
                      },
                    ),
                  ),
                ),
                // Always shown (owner decision, 28 Nov 2026); it keeps the
                // last look's name when Mode is not Custom.
                NeupodListItem.row(
                  height: h,
                  title: 'Custom look',
                  value: lookName,
                  chevron: true,
                  onActivate: () => pushScreen(
                    ref,
                    'Custom look',
                    (_) => const LooksScreen(),
                  ),
                ),
                NeupodListItem.row(
                  height: h,
                  title: 'Library',
                  chevron: true,
                  onActivate: () =>
                      pushScreen(ref, 'Library', (_) => const LibraryScreen()),
                ),
                // Last, where the specification lists it ("About; Licences").
                NeupodListItem.row(
                  height: h,
                  title: 'Licences',
                  chevron: true,
                  onActivate: () => pushScreen(
                    ref,
                    'Licences',
                    (_) => const LicencesScreen(),
                  ),
                ),
              ],
            );
          },
        ),
      ),
    );
  }
}
