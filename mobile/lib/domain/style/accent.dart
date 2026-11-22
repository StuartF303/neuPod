// SPDX-License-Identifier: MPL-2.0

/// The Modern style's accent rule (app specification, "Now Playing"; UI styles
/// design, "Modern, Dark: the values"):
///
/// > the art's dominant colour with its lightness clamped until white text on
/// > it is at least 4.5:1.
///
/// Implemented literally: the colour's HSL hue and saturation are kept and its
/// lightness is lowered to the lightest value at which white text on it has a
/// contrast of at least [minAccentContrast]. A colour that already passes is
/// returned unchanged.
///
/// The design's worked examples are hand-picked and darker than this minimal
/// clamp (see `test/domain/style/accent_test.dart` and the PR): `#d9734e`
/// clamps to `#c5532a` (4.5:1) where the mock-up drew `#b8502c` (which is
/// 4.97:1, not the 7:1 the README states), and `#e9c46a` clamps to `#967016`
/// (4.5:1) where the mock-up drew `#8a5a12` (5.9:1). Both drawn values pass the
/// rule, so they are fixed points of it.
library;

import 'package:neupod/domain/style/colour_math.dart';

/// White, the text colour on the accent.
const int white = 0xffffff;

/// The contrast white text must reach on the accent.
const double minAccentContrast = 4.5;

/// [rgb] with its lightness lowered until white text on it reaches
/// [minContrast]; unchanged if it already does.
int clampAccentForWhiteText(int rgb, {double minContrast = minAccentContrast}) {
  final colour = rgb & 0xffffff;
  if (contrastRatio(colour, white) >= minContrast) return colour;
  final hsl = Hsl.fromRgb(colour);
  // Contrast with white rises as lightness falls (for a fixed hue and
  // saturation), so bisect on lightness for the lightest passing value.
  var lo = 0.0; // passes: black
  var hi = hsl.lightness; // fails
  for (var i = 0; i < 40; i++) {
    final mid = (lo + hi) / 2;
    if (contrastRatio(hsl.withLightness(mid).toRgb(), white) >= minContrast) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  var out = hsl.withLightness(lo).toRgb();
  // Rounding to 8-bit channels can land a hair under the threshold.
  var l = lo;
  while (contrastRatio(out, white) < minContrast && l > 0) {
    l = (l - 0.001).clamp(0.0, 1.0);
    out = hsl.withLightness(l).toRgb();
  }
  return out;
}

/// The progress bar's fill in Modern: "a light tint of the art colour
/// (`#f2a586` in the mock-up)". The design gives only the example, so this is
/// our rule: hue and saturation kept, lightness moved 37.5 % of the way to
/// white. From `#d9734e` it gives `#e7a790` (the mock-up's lightness, 0.74,
/// with the source's saturation rather than the mock-up's slightly higher one).
int progressTint(int rgb) {
  final hsl = Hsl.fromRgb(rgb & 0xffffff);
  return hsl.withLightness(hsl.lightness + (1 - hsl.lightness) * 0.375).toRgb();
}
