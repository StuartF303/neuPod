// SPDX-License-Identifier: MPL-2.0

/// List rows, one and two lines, and the selection bar (UI styles design;
/// device view section 6 for the pressed tint, section 8 for the tiers).
///
/// A row is placed inside a [ListGround], which supplies the list's own
/// horizontal padding (Modern 6 px, the bar's inset). Heights come from the
/// text tier: 56 / 66 / 77 px for one line and 75 / 99 / 129 for two.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/style/icons/neupod_glyphs.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

/// The bar under a row: none, the selection bar, or the bar confirming an
/// action with a tick ("Added to On-The-Go"; UI styles Open question 3 asks
/// whether Classic flashes pale, as drawn, or blinks).
enum RowSelection { none, selected, flash }

class NeupodRow extends StatelessWidget {
  const NeupodRow({
    super.key,
    required this.title,
    this.subtitle,
    this.number,
    this.value,
    this.chevron = false,
    this.thumbnail,
    this.selection = RowSelection.none,
    this.pressed = false,
    this.disabledReason,
    this.position,
    this.paintsBar = true,
    this.onTap,
  });

  final String title;

  /// The second line; a row with one is a two-line row.
  final String? subtitle;

  /// A track number, right-aligned in a column before the title.
  final String? number;

  /// A value at the right, in Small (`On`, `Auto`).
  final String? value;

  /// A chevron at the right: the row opens another screen.
  final bool chevron;

  /// A cover thumbnail before the text (an artist's albums).
  final Widget? thumbnail;
  final RowSelection selection;

  /// Under a finger (device view candidate 2): a tint from touch-down.
  final bool pressed;

  /// When set, the row is greyed and this reason shows at its right, visible
  /// before the press (`AAC, can't play`).
  final String? disabledReason;

  /// For the screen reader: `4 of 7`.
  final String? position;

  /// False when the list draws one sliding [SelectionBar] under its rows; the
  /// row then only switches its text to the bar's colours.
  final bool paintsBar;
  final VoidCallback? onTap;

  bool get twoLine => subtitle != null;
  bool get greyed => disabledReason != null;

  String get semanticsLabel => [
    title,
    if (greyed) disabledReason! else ?(subtitle ?? value),
    if (!greyed) ?position,
  ].join(', ');

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final style = theme.style;
    final type = NeupodType(theme.tier);
    final k = theme.tier.k;
    final list = style.list;
    final sel = style.selection;
    final tier = theme.tier;
    final height = twoLine ? tier.row2Height : tier.rowHeight;
    final onBar = selection != RowSelection.none && !greyed;
    final flash = selection == RowSelection.flash && !greyed;

    final Color ink;
    final Color ink2;
    final Color numberInk;
    final Color chevronInk;
    if (greyed) {
      ink = ink2 = numberInk = chevronInk = list.disabled;
    } else if (flash) {
      ink = ink2 = numberInk = chevronInk = sel.flashText;
    } else if (onBar) {
      ink = ink2 = numberInk = chevronInk = sel.text;
    } else {
      ink = style.tokens.ink;
      ink2 = list.secondary;
      numberInk = list.number;
      chevronInk = list.chevron;
    }
    final shadows = onBar && !flash ? sel.textShadows : const <Shadow>[];
    TextStyle withShadow(TextStyle s) =>
        shadows.isEmpty ? s : s.copyWith(shadows: shadows);

    final text = twoLine
        ? Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              NeupodText(title, withShadow(type.body(ink))),
              NeupodText(subtitle!, withShadow(type.small(ink2))),
            ],
          )
        : NeupodText(title, withShadow(type.body(ink)));

    final children = <Widget>[
      if (number != null) ...[
        SizedBox(
          width: 30 * k,
          child: NeupodText(
            number!,
            withShadow(type.small(numberInk)),
            align: TextAlign.right,
          ),
        ),
        SizedBox(width: 16 * k),
      ],
      if (thumbnail != null) ...[
        SizedBox.square(dimension: 59 * k, child: thumbnail),
        const SizedBox(width: 14),
      ],
      Expanded(child: text),
      if (greyed) ...[
        const SizedBox(width: 12),
        // The reason must stay readable: it and the title share the row,
        // each ending in an ellipsis only if both are long.
        // The reason stays readable: the title gives way first.
        _trailing(NeupodText(disabledReason!, type.small(list.reason))),
      ] else if (flash) ...[
        const SizedBox(width: 12),
        NeupodIcon(
          NeupodGlyph.check,
          size: 30 * k,
          color: sel.flashText,
          strokeWidth: 3,
        ),
      ] else if (value != null) ...[
        const SizedBox(width: 12),
        _trailing(
          NeupodText(
            value!,
            withShadow(type.small(onBar ? sel.text : list.secondary)),
          ),
        ),
      ] else if (chevron) ...[
        const SizedBox(width: 12),
        NeupodIcon(
          NeupodGlyph.chevronRight,
          size: 22 * k,
          color: chevronInk,
          strokeWidth: 3,
        ),
      ],
    ];

    final padLeft = thumbnail != null ? 8.0 : list.rowPadX;
    Widget row = SizedBox(
      height: height,
      child: Padding(
        padding: EdgeInsets.only(left: padLeft, right: list.rowPadX),
        child: LayoutBuilder(
          builder: (context, c) => _TrailingLimit(
            maxWidth: c.maxWidth * 0.6,
            child: Row(children: children),
          ),
        ),
      ),
    );

    final radius = twoLine ? sel.radiusTwoLine : sel.radiusOneLine;
    if (onBar && paintsBar) {
      row = SkinBox(
        skin: (flash ? sel.flash : sel.bar).copyWith(radius: radius),
        child: row,
      );
    } else if (pressed && !greyed) {
      row = DecoratedBox(
        decoration: BoxDecoration(
          color: sel.pressedTint,
          borderRadius: radius > 0 ? BorderRadius.circular(radius) : null,
        ),
        child: row,
      );
    }

    return MergeSemantics(
      child: Semantics(
        label: semanticsLabel,
        selected: selection != RowSelection.none,
        enabled: !greyed,
        button: !greyed,
        onTap: greyed ? null : onTap,
        child: ExcludeSemantics(
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: greyed ? null : onTap,
            child: row,
          ),
        ),
      ),
    );
  }
}

/// A row's right-hand text keeps its own width, up to 60 % of the row.
Widget _trailing(Widget text) => Builder(
  builder: (context) => ConstrainedBox(
    constraints: BoxConstraints(maxWidth: _TrailingLimit.of(context)),
    child: text,
  ),
);

class _TrailingLimit extends InheritedWidget {
  const _TrailingLimit({required this.maxWidth, required super.child});

  final double maxWidth;

  static double of(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<_TrailingLimit>()!.maxWidth;

  @override
  bool updateShouldNotify(_TrailingLimit old) => old.maxWidth != maxWidth;
}

/// The selection bar on its own, for a list that slides one bar between rows
/// (80 ms to an adjacent row; device view section 5). Size it to the row.
class SelectionBar extends StatelessWidget {
  const SelectionBar({super.key, this.twoLine = false, this.flash = false});

  final bool twoLine;
  final bool flash;

  @override
  Widget build(BuildContext context) {
    final sel = NeupodTheme.of(context).style.selection;
    return SkinBox(
      skin: (flash ? sel.flash : sel.bar).copyWith(
        radius: twoLine ? sel.radiusTwoLine : sel.radiusOneLine,
      ),
    );
  }
}
