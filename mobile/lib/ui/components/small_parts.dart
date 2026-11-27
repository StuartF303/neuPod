// SPDX-License-Identifier: MPL-2.0

/// Smaller parts: the fast-scroll letter tile and index rail, rating stars,
/// and Search's field.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/style/icons/neupod_glyphs.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

/// The letter tile and index rail shown over an indexed list while the wheel
/// spins fast (device view section 7): an 82 px tile, radius 15, 52 px from the
/// right edge and 21 px below the status bar, the letter at 48 px; the rail 26
/// px wide at the right edge, every second letter at 17 px, the current one at
/// 22 px. Place it in a Stack over the list area (below the status bar).
///
/// Neither scales with the text tier: the rail must fit the list's height at
/// every tier, and the tile's letter is already large. Both are visual only;
/// the screen reader hears the focused row.
class FastScrollIndex extends StatelessWidget {
  const FastScrollIndex({super.key, required this.letter, required this.rail});

  /// The tile's letter (`H`).
  final String letter;

  /// The rail's letters (`ACEGHKMOQSUWY`); the one equal to [letter] is shown
  /// larger and brighter.
  final String rail;

  @override
  Widget build(BuildContext context) {
    final skin = NeupodTheme.of(context).style.index;
    return ExcludeSemantics(
      child: Stack(
        children: [
          Positioned(
            right: 52,
            top: 21,
            width: 82,
            height: 82,
            child: DecoratedBox(
              decoration: BoxDecoration(
                color: skin.tile,
                borderRadius: BorderRadius.circular(15),
              ),
              child: Center(
                child: Text(
                  letter,
                  style: neupodTextStyle(48, 52, skin.tileLetter),
                ),
              ),
            ),
          ),
          Positioned(
            right: 8,
            top: 9,
            width: 26,
            child: Column(
              children: [
                for (final c in rail.split(''))
                  SizedBox(
                    height: 28,
                    child: Center(
                      child: Text(
                        c,
                        style: c == letter
                            ? neupodTextStyle(22, 28, skin.railCurrent)
                            : neupodTextStyle(17, 28, skin.rail),
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// Rating stars for Now Playing (specification, "Now Playing"). Not in the
/// mock-ups: drawn here in each style's inks, Lucide `star`, filled for the
/// rating and outlined for the rest; where they sit is chosen at the design
/// gate.
class RatingStars extends StatelessWidget {
  const RatingStars({super.key, required this.rating, this.size = 22});

  /// 0 (not rated) to 5.
  final int rating;

  /// Each star in design px, before the tier's k.
  final double size;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = theme.style.rating;
    final s = size * theme.tier.k;
    return Semantics(
      label: rating == 0 ? 'Not rated' : 'Rating, $rating of 5 stars',
      child: ExcludeSemantics(
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            for (var i = 0; i < 5; i++) ...[
              if (i > 0) SizedBox(width: s * 0.2),
              NeupodIcon(
                i < rating ? NeupodGlyph.starFilled : NeupodGlyph.star,
                size: s,
                color: i < rating ? skin.filled : skin.empty,
              ),
            ],
          ],
        ),
      ),
    );
  }
}

/// Search's field (device view section 7): a 44 px field with a magnifier,
/// the text and a caret, in a 60 px band. Visual only: the phone's keyboard
/// does the typing.
class SearchFieldBox extends StatelessWidget {
  const SearchFieldBox({super.key, required this.text, this.caret = true});

  final String text;
  final bool caret;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = theme.style.searchField;
    final k = theme.tier.k;
    final pad = theme.style.list.rowPadX + theme.style.section.extraPadX;
    return SizedBox(
      height: 60 * k,
      child: Padding(
        padding: EdgeInsets.symmetric(horizontal: pad, vertical: 8 * k),
        child: SkinBox(
          skin: skin.box,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Row(
              children: [
                NeupodIcon(
                  NeupodGlyph.search,
                  size: 24 * k,
                  color: skin.glyph,
                  strokeWidth: 2.5,
                ),
                const SizedBox(width: 10),
                Flexible(
                  child: NeupodText(
                    text,
                    NeupodType(theme.tier).body(skin.text),
                  ),
                ),
                if (caret) ...[
                  const SizedBox(width: 4),
                  Container(width: 2, height: 28 * k, color: skin.caret),
                ],
              ],
            ),
          ),
        ),
      ),
    );
  }
}
