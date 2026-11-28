// SPDX-License-Identifier: MPL-2.0

/// Settings > Custom look: the saved looks, one row each, then `New look`
/// (design section 2). The look in use carries a tick. Resting the selection
/// bar on a look for 250 ms previews it on the device; Menu leaves with the
/// look in use unchanged; Select opens the look and makes it the look in use
/// (and Mode Custom). `New look` makes a copy of the look in use and opens it;
/// at six it is greyed with `6 of 6`.
///
/// No scroll bar is drawn (owner decision 2). At the Large and Largest text
/// tiers the rows move with the wheel (owner decision, 28 Nov 2026).
library;

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/custom_look_actions.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/look_edit.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/custom/look_screen.dart';
import 'package:neupod/ui/screens/custom/wheel_screen.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// Clears the preview after the frame, so a screen going away never changes
/// a provider while the tree is being torn down.
void clearLookPreview(LookPreviewNotifier notifier) {
  scheduleMicrotask(() {
    try {
      notifier.show(null);
    } on Object {
      // The app is gone.
    }
  });
}

/// Opens [look] (in use, Mode Custom) on its own screen.
void openLook(WidgetRef ref, CustomLook look) {
  ref.read(customLookActionsProvider).open(look.id);
  pushScreen(ref, look.name, (_) => LookDetailScreen(lookId: look.id));
}

/// The list of looks.
class LooksScreen extends ConsumerStatefulWidget {
  /// The list of looks.
  const LooksScreen({super.key});

  @override
  ConsumerState<LooksScreen> createState() => _LooksScreenState();
}

class _LooksScreenState extends ConsumerState<LooksScreen> {
  late final LookPreviewNotifier _preview;
  final RestTimer _rest = RestTimer();
  int _selected = -1;

  @override
  void initState() {
    super.initState();
    _preview = ref.read(lookPreviewProvider.notifier);
  }

  @override
  void dispose() {
    _rest.cancel();
    clearLookPreview(_preview);
    super.dispose();
  }

  void _moved(CustomLooks looks, int index) {
    setState(() => _selected = index);
    _rest.start(previewRest, () {
      if (!mounted) return;
      _preview.show(index < looks.looks.length ? looks.looks[index] : null);
    });
  }

  void _open(CustomLook look) {
    _rest.cancel();
    _preview.show(null);
    openLook(ref, look);
  }

  void _new() {
    _rest.cancel();
    _preview.show(null);
    final made = ref
        .read(customLookActionsProvider)
        .newLook(
          phoneIsLight:
              MediaQuery.platformBrightnessOf(context) == Brightness.light,
        );
    if (made != null) {
      pushScreen(ref, made.name, (_) => LookDetailScreen(lookId: made.id));
    }
  }

  @override
  Widget build(BuildContext context) {
    final looks = ref.watch(customLooksProvider);
    final count = looks.looks.length + 1;
    final shown =
        (_selected < 0
                ? (looks.looks.indexWhere((l) => l.id == looks.lookInUse))
                : _selected)
            .clamp(0, count - 1);
    return ShellFrame(
      title: 'Custom look',
      context: '${shown + 1} of $count',
      body: BrowseListBody(
        child: Builder(
          builder: (context) {
            final h = NeupodTheme.of(context).tier.rowHeight;
            final inUse = looks.looks.indexWhere(
              (l) => l.id == looks.lookInUse,
            );
            return NeupodList(
              indexed: false,
              showScrollBar: false,
              initialSelection: inUse < 0 ? 0 : inUse,
              onSelectionChanged: (i) => _moved(looks, i),
              items: [
                for (final look in looks.looks)
                  NeupodListItem.row(
                    id: look.id,
                    height: h,
                    title: look.name,
                    tick: look.id == looks.lookInUse,
                    chevron: true,
                    onActivate: () => _open(look),
                  ),
                NeupodListItem.row(
                  id: 'new',
                  height: h,
                  title: 'New look',
                  disabledReason: looks.full ? '$maxLooks of $maxLooks' : null,
                  onActivate: _new,
                ),
              ],
            );
          },
        ),
      ),
    );
  }
}
