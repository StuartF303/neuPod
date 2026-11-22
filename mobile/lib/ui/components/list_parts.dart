// SPDX-License-Identifier: MPL-2.0

/// The parts around list rows: the list ground, the scroll bar, disc and
/// section headings, and the right-hand preview pane.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

/// The ground under a list. Classic: none. Modern: a dark veil over the
/// backdrop (`rgba(0,0,0,.42)`, or `.38` beside a preview pane) and the 6 px
/// padding that insets the selection bar.
class ListGround extends StatelessWidget {
  const ListGround({
    super.key,
    required this.child,
    this.besidePane = false,
    this.topRule = false,
  });

  final Widget child;

  /// The list on Home and the Music menu, beside the preview pane.
  final bool besidePane;

  /// A hairline above the list (Modern, under the album header).
  final bool topRule;

  @override
  Widget build(BuildContext context) {
    final list = NeupodTheme.of(context).style.list;
    final ground = besidePane ? list.paneGround : list.ground;
    Widget out = Padding(
      padding: EdgeInsets.symmetric(horizontal: list.padX),
      child: child,
    );
    if (topRule && list.divider != null) {
      out = DecoratedBox(
        decoration: BoxDecoration(
          border: Border(top: BorderSide(color: list.divider!)),
        ),
        child: out,
      );
    }
    if (ground != null) out = SkinBox(skin: ground, child: out);
    return out;
  }
}

/// The scroll bar at a list's right edge. [visible] is the fraction of the
/// list on screen, [position] how far it is scrolled (0 to 1).
class NeupodScrollBar extends StatelessWidget {
  const NeupodScrollBar({
    super.key,
    required this.visible,
    required this.position,
  });

  final double visible;
  final double position;

  /// The thumb is never shorter than this (firmware `sb_min_h` at 600x450).
  static const double minThumb = 26;

  @override
  Widget build(BuildContext context) {
    final skin = NeupodTheme.of(context).style.scrollBar;
    Widget bar = Padding(
      padding: skin.padding,
      child: LayoutBuilder(
        builder: (context, c) {
          final h = (c.maxHeight * visible.clamp(0.0, 1.0)).clamp(
            minThumb,
            c.maxHeight,
          );
          final top = (c.maxHeight - h) * position.clamp(0.0, 1.0);
          return Stack(
            children: [
              Positioned(
                left: 0,
                right: 0,
                top: top,
                height: h,
                child: SkinBox(skin: skin.thumb),
              ),
            ],
          );
        },
      ),
    );
    if (skin.gutter != null) bar = SkinBox(skin: skin.gutter!, child: bar);
    return ExcludeSemantics(
      child: SizedBox(width: skin.width, child: bar),
    );
  }
}

/// A disc heading in an album (`Disc 1`; device view section 7): a 34 px band
/// in Small text. Every disc gets one; track numbers restart under each.
class DiscHeading extends StatelessWidget {
  const DiscHeading(this.label, {super.key});

  final String label;

  @override
  Widget build(BuildContext context) =>
      _SectionBand(label: label, height: 34, fontPx: 22, lineHeight: 24);
}

/// A section heading in Search's results (`Artists`, `Albums`, `Songs`): a
/// 30 px band in 20 px text.
class SectionHeading extends StatelessWidget {
  const SectionHeading(this.label, {super.key});

  final String label;

  @override
  Widget build(BuildContext context) =>
      _SectionBand(label: label, height: 30, fontPx: 20, lineHeight: 22);
}

class _SectionBand extends StatelessWidget {
  const _SectionBand({
    required this.label,
    required this.height,
    required this.fontPx,
    required this.lineHeight,
  });

  final String label;
  final double height;
  final double fontPx;
  final double lineHeight;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = theme.style.section;
    final k = theme.tier.k;
    return Semantics(
      header: true,
      child: Container(
        height: height * k,
        color: skin.ground,
        alignment: Alignment.centerLeft,
        padding: EdgeInsets.symmetric(
          horizontal: theme.style.list.rowPadX + skin.extraPadX,
        ),
        child: NeupodText(
          label,
          neupodTextStyle(fontPx * k, lineHeight * k, skin.text),
        ),
      ),
    );
  }
}

/// The right-hand preview pane of Home and the Music menu.
class PreviewPane extends StatelessWidget {
  const PreviewPane({super.key, required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) => SkinBox(
    skin: NeupodTheme.of(context).style.pane,
    child: Padding(
      padding: const EdgeInsets.fromLTRB(22, 22, 22, 0),
      child: child,
    ),
  );
}
