// SPDX-License-Identifier: MPL-2.0

/// The board picker (design section 2): the boards at the left, a picture of
/// the highlighted one at the right, in a split like Home. Resting the bar on
/// a board previews it on the device after 250 ms; Select chooses it and goes
/// back to the look.
///
/// The pictures are the transparency step's (AP-1, 2/3). Until they are in,
/// the pane shows a plain tile in the board's ground colour, labelled
/// `Placeholder picture`.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/custom_look_actions.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/domain/style/board_list.dart';
import 'package:neupod/domain/style/look_edit.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/home_screen.dart'
    show listBesidePaneWidth;
import 'package:neupod/ui/screens/custom/looks_screen.dart'
    show clearLookPreview;
import 'package:neupod/ui/screens/custom/wheel_screen.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';

/// The board picker.
class BoardScreen extends ConsumerStatefulWidget {
  /// The board of the look with [lookId].
  const BoardScreen({required this.lookId, super.key});

  final String lookId;

  @override
  ConsumerState<BoardScreen> createState() => _BoardScreenState();
}

class _BoardScreenState extends ConsumerState<BoardScreen> {
  late final LookPreviewNotifier _preview;
  final RestTimer _rest = RestTimer();
  int? _shown;

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

  void _moved(int i) {
    setState(() => _shown = i);
    _rest.start(previewRest, () {
      final look = ref.read(customLooksProvider).byId(widget.lookId);
      if (!mounted || look == null) return;
      _preview.show(look.copyWith(board: boardList[i].id));
    });
  }

  void _choose(BoardInfo board) {
    _rest.cancel();
    _preview.show(null);
    ref
        .read(customLookActionsProvider)
        .edit(widget.lookId, (l) => l.copyWith(board: board.id));
    ref.read(shellProvider.notifier).pop();
  }

  @override
  Widget build(BuildContext context) {
    final look = ref.watch(customLooksProvider).byId(widget.lookId);
    final current = boardList.indexWhere((b) => b.id == look?.board);
    final shown = (_shown ?? (current < 0 ? 0 : current));
    return ShellFrame(
      title: 'Board',
      context: '${shown + 1} of ${boardList.length}',
      body: Builder(
        builder: (context) {
          final h = NeupodTheme.of(context).tier.rowHeight;
          return Row(
            children: [
              SizedBox(
                width: listBesidePaneWidth,
                child: BrowseListBody(
                  besidePane: true,
                  child: NeupodList(
                    indexed: false,
                    showScrollBar: false,
                    initialSelection: current < 0 ? 0 : current,
                    onSelectionChanged: _moved,
                    items: [
                      for (final b in boardList)
                        NeupodListItem.row(
                          id: b.id,
                          height: h,
                          title: b.name,
                          tick: b.id == look?.board,
                          onActivate: () => _choose(b),
                        ),
                    ],
                  ),
                ),
              ),
              Expanded(child: _BoardPane(boardList[shown])),
            ],
          );
        },
      ),
    );
  }
}

class _BoardPane extends StatelessWidget {
  const _BoardPane(this.board);

  final BoardInfo board;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final type = NeupodType(theme.tier);
    final k = theme.tier.k;
    final ground = Color(0xFF000000 | board.ground);
    final light = Color(0xFF000000 | board.frostLight);
    return PreviewPane(
      child: ExcludeSemantics(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            // A 150 x 195 tile: the picture's shape, until there is one.
            Container(
              width: 150,
              height: 195,
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(6),
                gradient: LinearGradient(
                  begin: Alignment.topCenter,
                  end: Alignment.bottomCenter,
                  colors: [light, ground],
                ),
                boxShadow: const [
                  BoxShadow(
                    color: Color(0x66000000),
                    offset: Offset(0, 2),
                    blurRadius: 5,
                  ),
                ],
              ),
            ),
            SizedBox(height: 14 * k),
            Text(
              'Placeholder picture',
              textAlign: TextAlign.center,
              style: type.small(theme.style.list.secondary),
            ),
          ],
        ),
      ),
    );
  }
}
