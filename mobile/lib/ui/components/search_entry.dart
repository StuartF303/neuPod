// SPDX-License-Identifier: MPL-2.0

/// Search typed with the wheel (owner decision 12; device view design,
/// "Search, by the wheel"): the live result rows, the letter ring, the mode
/// chips and the hint line. The field is [SearchFieldBox].
///
/// Pure views: they draw what they are given. The model (which glyph is
/// current, which chip is active, what the wheel does) is a port of the
/// firmware's `app/include/wheel_input.h`, built separately.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

/// One of the three live results while typing: a 50 px row, the name in Body
/// and what it is at the right in Small (`Artist`, `Album · 1991`).
class SearchResultRow extends StatelessWidget {
  const SearchResultRow({
    super.key,
    required this.title,
    required this.label,
    this.selected = false,
    this.greyed = false,
    this.onTap,
  });

  final String title;
  final String label;
  final bool selected;

  /// A song that cannot play: greyed, with [label] its reason.
  final bool greyed;
  final VoidCallback? onTap;

  static const double height = 50;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final style = theme.style;
    final type = NeupodType(theme.tier);
    final ink = selected
        ? style.selection.text
        : greyed
        ? style.list.disabled
        : style.tokens.ink;
    final ink2 = selected
        ? style.selection.text
        : greyed
        ? style.list.reason
        : style.list.secondary;
    Widget row = SizedBox(
      height: height * theme.tier.k,
      child: Padding(
        padding: EdgeInsets.symmetric(horizontal: style.list.rowPadX),
        child: Row(
          children: [
            Expanded(child: NeupodText(title, type.body(ink))),
            const SizedBox(width: 12),
            NeupodText(label, type.small(ink2)),
          ],
        ),
      ),
    );
    if (selected) {
      row = SkinBox(
        skin: style.selection.bar.copyWith(
          radius: style.selection.radiusOneLine,
        ),
        child: row,
      );
    }
    return MergeSemantics(
      child: Semantics(
        label: '$title, $label',
        button: !greyed,
        enabled: !greyed,
        selected: selected,
        onTap: greyed ? null : onTap,
        child: ExcludeSemantics(
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: onTap,
            child: row,
          ),
        ),
      ),
    );
  }
}

/// The letter ring: nine glyphs centred on the current one, which is drawn at
/// 56 px in the caret colour; its neighbours at 30, 26, 22 and 18 px fading
/// from ink to tertiary. A 64 px band with a hairline above.
class LetterRing extends StatelessWidget {
  const LetterRing({super.key, required this.glyphs});

  /// The nine glyphs on show, the current one at index 4.
  final List<String> glyphs;

  static const List<double> sizes = [18, 22, 26, 30, 56, 30, 26, 22, 18];

  @override
  Widget build(BuildContext context) {
    assert(glyphs.length == 9, 'nine glyphs, the current one in the middle');
    final theme = NeupodTheme.of(context);
    final style = theme.style;
    final k = theme.tier.k;
    final t = style.tokens;
    final colours = [
      t.ink3,
      t.ink2,
      t.ink2,
      t.ink,
      style.searchField.caret,
      t.ink,
      t.ink2,
      t.ink2,
      t.ink3,
    ];
    return Semantics(
      label: 'Letter ${glyphs[4]}',
      child: ExcludeSemantics(
        child: Container(
          height: 64 * k,
          decoration: BoxDecoration(
            border: Border(top: BorderSide(color: style.searchField.ringRule)),
          ),
          child: FittedBox(
            fit: BoxFit.scaleDown,
            child: Row(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.center,
              children: [
                for (var i = 0; i < 9; i++) ...[
                  if (i > 0) const SizedBox(width: 22),
                  Text(
                    glyphs[i],
                    style: neupodTextStyle(sizes[i] * k, 60 * k, colours[i]),
                  ),
                ],
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// The mode chips, in a 40 px band: 36 high, radius 18, the active one filled
/// and the others outlined. The owner chose the firmware's six (`abc`, `ABC`,
/// `123`, `#!?`, `space`, `del`) over the design's five; the row is
/// data-driven either way and shrinks to fit rather than clip.
class ModeChips extends StatelessWidget {
  const ModeChips({
    super.key,
    required this.labels,
    required this.active,
    this.onTap,
  });

  final List<String> labels;
  final int active;
  final ValueChanged<int>? onTap;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = theme.style.searchField;
    final type = NeupodType(theme.tier);
    final k = theme.tier.k;
    return SizedBox(
      height: 40 * k,
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            for (var i = 0; i < labels.length; i++) ...[
              if (i > 0) const SizedBox(width: 10),
              Semantics(
                button: true,
                selected: i == active,
                label: labels[i],
                onTap: onTap == null ? null : () => onTap!(i),
                child: ExcludeSemantics(
                  child: GestureDetector(
                    onTap: onTap == null ? null : () => onTap!(i),
                    child: SizedBox(
                      height: 36 * k,
                      child: SkinBox(
                        skin: (i == active ? skin.chipOn : skin.chipOff)
                            .copyWith(radius: 18 * k),
                        child: Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 14),
                          child: Center(
                            widthFactor: 1,
                            child: Text(
                              labels[i],
                              style: type.small(
                                i == active
                                    ? skin.chipOnText
                                    : skin.chipOffText,
                              ),
                            ),
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

/// The hint under the chips, in Small tertiary
/// (`Select: type · Menu: delete · Play: results`).
class SearchHint extends StatelessWidget {
  const SearchHint(this.text, {super.key});

  final String text;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    return NeupodText(
      text,
      neupodTextStyle(
        22 * theme.tier.k,
        26 * theme.tier.k,
        theme.style.tokens.ink3,
      ),
      align: TextAlign.center,
    );
  }
}
