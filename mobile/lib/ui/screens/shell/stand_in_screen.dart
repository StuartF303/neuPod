// SPDX-License-Identifier: MPL-2.0

/// A screen that says it is not built yet: one greyed row, as the device
/// shows an item that cannot be used. Stands in for Now Playing and Search
/// until their screens register (`ShellRegistry`), and for Playlists, which
/// are a later phase.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// A stand-in screen titled [title].
class StandInScreen extends StatelessWidget {
  /// A stand-in.
  const StandInScreen(this.title, {super.key});

  final String title;

  @override
  Widget build(BuildContext context) => ShellFrame(
    title: title,
    body: Builder(
      builder: (context) => ListGround(
        child: NeupodList(
          indexed: false,
          items: [
            NeupodListItem.row(
              height: NeupodTheme.of(context).tier.rowHeight,
              title: title,
              disabledReason: 'Not built yet',
            ),
          ],
        ),
      ),
    ),
  );
}
