// SPDX-License-Identifier: MPL-2.0

/// Type: Montserrat Medium (SIL OFL 1.1, bundled in `assets/fonts/`), at the
/// device's three sizes, 22 / 26 / 38 design px with line heights 24 / 29 / 41
/// (UI styles design; firmware `neupod_font_22/26/38`), times the tier's k.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/style/text_tier.dart';

/// The bundled family name, as declared in `pubspec.yaml`.
const String montserrat = 'Montserrat';

/// A Montserrat Medium style at [size] design px with a [lineHeight] in px.
TextStyle neupodTextStyle(double size, double lineHeight, Color color) =>
    TextStyle(
      fontFamily: montserrat,
      fontWeight: FontWeight.w500,
      fontSize: size,
      height: lineHeight / size,
      leadingDistribution: TextLeadingDistribution.even,
      color: color,
      letterSpacing: 0,
    );

/// The three text sizes of the design at a text tier.
final class NeupodType {
  const NeupodType(this.tier);

  final TextTier tier;

  double get k => tier.k;

  /// Small: 22 px on 24 (secondary lines, times, the status bar's context).
  TextStyle small(Color c) => neupodTextStyle(22 * k, 24 * k, c);

  /// Body: 26 px on 29 (rows, titles).
  TextStyle body(Color c) => neupodTextStyle(26 * k, 29 * k, c);

  /// Large: 38 px on 41 (Now Playing title, album header).
  TextStyle large(Color c) => neupodTextStyle(38 * k, 41 * k, c);

  /// Any other size the design names (the letter tile's 48, seek's 72, the
  /// rail's 17 and 22), scaled by k; line height 1.08 times unless given.
  TextStyle sized(double px, Color c, {double? lineHeight}) =>
      neupodTextStyle(px * k, (lineHeight ?? px * 1.08) * k, c);
}
