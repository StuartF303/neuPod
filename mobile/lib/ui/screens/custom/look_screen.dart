// SPDX-License-Identifier: MPL-2.0

/// One look: seven rows (design section 2). Body, Wheel and Centre open the
/// part editor on that part; Board opens the board picker; Screen cycles
/// Light, Dark, Auto in place; Name opens the wheel text entry; Reset or
/// delete opens its two rows.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/custom_look_actions.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/domain/style/board_list.dart';
import 'package:neupod/domain/style/colour_math.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/look_edit.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/custom/board_screen.dart';
import 'package:neupod/ui/screens/custom/name_screen.dart';
import 'package:neupod/ui/screens/custom/part_picker_screen.dart';
import 'package:neupod/ui/screens/custom/reset_delete_screen.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// `Solid` or `55 % clear`.
String clearLabel(PartPaint p) =>
    p.transparency == 0 ? 'Solid' : '${p.transparency} % clear';

/// `Light`, `Dark` or `Auto`.
String lookScreenName(LookScreen s) => switch (s) {
  LookScreen.light => 'Light',
  LookScreen.dark => 'Dark',
  LookScreen.auto => 'Auto',
};

/// A colour swatch: the part's colour in a thin grey frame.
class ColourSwatch extends StatelessWidget {
  /// A swatch of [rgb].
  const ColourSwatch(this.rgb, {super.key});

  final int rgb;

  @override
  Widget build(BuildContext context) => DecoratedBox(
    decoration: BoxDecoration(
      color: Color(0xFF000000 | rgb),
      borderRadius: BorderRadius.circular(4),
      border: Border.all(color: const Color(0xCC808080)),
    ),
  );
}

/// A look's seven rows.
class LookDetailScreen extends ConsumerWidget {
  /// The look with [lookId].
  const LookDetailScreen({required this.lookId, super.key});

  final String lookId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final look = ref.watch(customLooksProvider.select((l) => l.byId(lookId)));
    if (look == null) {
      // Deleted while this was showing: the delete pops it.
      return const ShellFrame(title: 'Custom look', body: SizedBox.shrink());
    }
    void part(LookPart p) => pushScreen(
      ref,
      look.name,
      (_) => PartPickerScreen(lookId: lookId, part: p),
    );
    return _SelectionTracker(
      count: 7,
      title: look.name,
      builder: (context, position) => BrowseListBody(
        child: Builder(
          builder: (context) {
            final h = NeupodTheme.of(context).tier.rowHeight;
            NeupodListItem partRow(String title, LookPart p) {
              final paint = look.paintOf(p);
              return NeupodListItem.row(
                height: h,
                title: title,
                leading: ColourSwatch(
                  hsvToRgb(paint.hue, paint.saturation, paint.brightness),
                ),
                value: clearLabel(paint),
                chevron: true,
                onActivate: () => part(p),
              );
            }

            return NeupodList(
              indexed: false,
              showScrollBar: false,
              onSelectionChanged: position,
              items: [
                partRow('Body', LookPart.body),
                partRow('Wheel', LookPart.wheel),
                partRow('Centre', LookPart.centre),
                NeupodListItem.row(
                  height: h,
                  title: 'Board',
                  value: boardById(look.board).name,
                  chevron: true,
                  onActivate: () => pushScreen(
                    ref,
                    'Board',
                    (_) => BoardScreen(lookId: lookId),
                  ),
                ),
                NeupodListItem.row(
                  height: h,
                  title: 'Screen',
                  value: lookScreenName(look.screen),
                  onActivate: () => ref
                      .read(customLookActionsProvider)
                      .edit(
                        lookId,
                        (l) => l.copyWith(
                          screen: switch (l.screen) {
                            LookScreen.light => LookScreen.dark,
                            LookScreen.dark => LookScreen.auto,
                            LookScreen.auto => LookScreen.light,
                          },
                        ),
                      ),
                ),
                NeupodListItem.row(
                  height: h,
                  title: 'Name',
                  value: look.name,
                  chevron: true,
                  onActivate: () => pushScreen(
                    ref,
                    'Name',
                    (_) => NameScreen(lookId: lookId),
                  ),
                ),
                NeupodListItem.row(
                  height: h,
                  title: 'Reset or delete',
                  chevron: true,
                  onActivate: () => pushScreen(
                    ref,
                    'Reset or delete',
                    (_) => ResetDeleteScreen(lookId: lookId),
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

/// A frame whose status bar says `n of count` as the selection moves.
class _SelectionTracker extends StatefulWidget {
  const _SelectionTracker({
    required this.count,
    required this.title,
    required this.builder,
  });

  final int count;
  final String title;
  final Widget Function(BuildContext, ValueChanged<int> position) builder;

  @override
  State<_SelectionTracker> createState() => _SelectionTrackerState();
}

class _SelectionTrackerState extends State<_SelectionTracker> {
  int _at = 0;

  @override
  Widget build(BuildContext context) => ShellFrame(
    title: widget.title,
    context: '${_at + 1} of ${widget.count}',
    body: Builder(
      builder: (context) =>
          widget.builder(context, (i) => setState(() => _at = i)),
    ),
  );
}
