// SPDX-License-Identifier: MPL-2.0

/// A look's last row: `Reset to default` and `Delete look`, each behind the
/// centred panel because something is about to be lost (design section 7).
/// Select on the panel does it; Menu keeps things as they were.
library;

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/custom_look_actions.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/domain/style/look_edit.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// Reset or delete.
class ResetDeleteScreen extends ConsumerWidget {
  /// The two rows for the look with [lookId].
  const ResetDeleteScreen({required this.lookId, super.key});

  final String lookId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final look = ref.watch(customLooksProvider.select((l) => l.byId(lookId)));
    final name = look?.name ?? '';
    final shell = ref.read(shellProvider.notifier);
    final phoneIsLight =
        MediaQuery.platformBrightnessOf(context) == Brightness.light;
    return ShellFrame(
      title: 'Reset or delete',
      context: name,
      body: BrowseListBody(
        child: Builder(
          builder: (context) {
            final h = NeupodTheme.of(context).tier.rowHeight;
            return NeupodList(
              indexed: false,
              showScrollBar: false,
              items: [
                NeupodListItem.row(
                  height: h,
                  title: 'Reset to default',
                  onActivate: () => unawaited(
                    shell.showPanel(
                      title: 'Reset $name?',
                      message:
                          'Its colours go back to the default finish. '
                          'Menu keeps them.',
                      okLabel: 'Reset',
                      onConfirm: () {
                        ref
                            .read(customLookActionsProvider)
                            .reset(lookId, phoneIsLight: phoneIsLight);
                        shell.pop();
                      },
                    ),
                  ),
                ),
                NeupodListItem.row(
                  height: h,
                  title: 'Delete look',
                  onActivate: () => unawaited(
                    shell.showPanel(
                      title: 'Delete $name?',
                      message: 'It cannot be got back. Menu keeps it.',
                      okLabel: 'Delete',
                      onConfirm: () {
                        ref.read(customLookActionsProvider).removeLook(lookId);
                        // This screen and the look's, back to the list.
                        shell
                          ..pop()
                          ..pop();
                      },
                    ),
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
