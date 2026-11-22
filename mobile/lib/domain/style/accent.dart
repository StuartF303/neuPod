// SPDX-License-Identifier: MPL-2.0

/// The Modern style's accent rule (app specification, "Now Playing"; UI styles
/// design, "Modern, Dark: the values"):
///
/// > the art's dominant colour with its lightness clamped until white text on
/// > it is at least 4.5:1.
///
/// Implemented so that the colour actually under the text passes: Modern
/// paints a white gloss over the accent (16 % at the top fading to nothing by
/// 55 % of the height), so white text sits on the accent lightened by that
/// gloss. The colour's HSL hue and saturation are kept and its lightness is
/// lowered to the lightest value at which white text has at least
/// [minAccentContrast] on the accent *with the gloss at the top of the text*
/// ([accentTextGloss]). A colour that already passes is returned unchanged.
///
/// The design's worked examples are hand-picked: `#d9734e` -> `#b8502c` and
/// `#e9c46a` -> `#8a5a12`. On the flat colour they are 4.97:1 (not the 7:1 the
/// README states) and 5.9:1; under the gloss at the text's top they are 4.0:1
/// and 4.6:1, so the first fails the specification's 4.5:1. This rule gives
/// `#ac4925` and `#806013` (`#8a5a12` would pass unchanged, but the rule
/// starts from the art colour, not from the drawn one); both are pinned in
/// `test/domain/style/accent_test.dart`.
library;

import 'package:neupod/domain/style/colour_math.dart';

/// White, the text colour on the accent.
const int white = 0xffffff;

/// The contrast white text must reach on the accent.
const double minAccentContrast = 4.5;

/// Modern's gloss: white at [glossTopAlpha] at the top of an accent surface,
/// fading linearly to nothing at [glossEnd] of its height.
const double glossTopAlpha = 0.16;
const double glossEnd = 0.55;

/// The gloss's white alpha at [fraction] of a surface's height (0 = top).
double glossAlphaAt(double fraction) {
  final f = fraction.clamp(0.0, 1.0);
  return f >= glossEnd ? 0 : glossTopAlpha * (1 - f / glossEnd);
}

/// The highest point text reaches on an accent surface, as a fraction of its
/// height: a standard two-line row (75 px, a 29 + 24 px text block centred)
/// starts its text at 11 / 75 = 0.147. One-line rows (0.24), the panel's OK
/// (0.21) and the larger tiers (0.19 and over) start lower, where the gloss
/// is fainter.
const double highestTextTop = 11 / 75;

/// The gloss under the top of the text, the most any text sits on.
final double accentTextGloss = glossAlphaAt(highestTextTop);

/// [rgb] overlaid with white at [alpha], as the gloss composites.
int withWhite(int rgb, double alpha) {
  final (r, g, b) = channels(rgb);
  return rgbOf(
    r + (255 - r) * alpha,
    g + (255 - g) * alpha,
    b + (255 - b) * alpha,
  );
}

/// [rgb] with its lightness lowered until white text on it, under a white
/// overlay of [gloss] (default: Modern's gloss at the top of the text),
/// reaches [minContrast]; unchanged if it already does.
int clampAccentForWhiteText(
  int rgb, {
  double minContrast = minAccentContrast,
  double? gloss,
}) {
  final overlay = gloss ?? accentTextGloss;
  bool passes(int c) =>
      contrastRatio(withWhite(c, overlay), white) >= minContrast;
  final colour = rgb & 0xffffff;
  if (passes(colour)) return colour;
  final hsl = Hsl.fromRgb(colour);
  // Contrast with white rises as lightness falls (for a fixed hue and
  // saturation), so bisect on lightness for the lightest passing value.
  var lo = 0.0; // passes: black
  var hi = hsl.lightness; // fails
  for (var i = 0; i < 40; i++) {
    final mid = (lo + hi) / 2;
    if (passes(hsl.withLightness(mid).toRgb())) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  var out = hsl.withLightness(lo).toRgb();
  // Rounding to 8-bit channels can land a hair under the threshold.
  var l = lo;
  while (!passes(out) && l > 0) {
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
