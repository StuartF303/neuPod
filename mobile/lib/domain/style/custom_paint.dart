// SPDX-License-Identifier: MPL-2.0

/// The contrast rule of Custom mode (design section 4), a port of `paint()`
/// and `increase_contrast()` in the design's `render.py`. Pure Dart.
library;

import 'dart:math' as math;

import 'package:neupod/domain/style/colour_math.dart';
import 'package:neupod/domain/style/custom_look.dart';

const int _black = 0x000000;
const int _white = 0xffffff;

/// WCAG target for legends.
const double contrastTarget = 4.5;

/// The target with Increase Contrast.
const double contrastTargetHigh = 7;

/// Relative luminance at or above which one flat colour takes black ink.
const double inkSwitchLuminance = 0.179;

/// Step when giving transparency back, per cent.
const int transparencyStep = 5;

/// How far a gradient's ends sit from the part's colour.
double gradientAmplitude(LookPart part) => switch (part) {
  LookPart.body => 0.10,
  LookPart.wheel => 0.06,
  LookPart.centre => 0.06,
};

/// What to draw a part with, once the rule has run.
final class PaintedPart {
  const PaintedPart({
    required this.base,
    required this.stops,
    required this.ink,
    required this.worst,
    required this.transparencyKept,
    required this.flat,
  });

  /// The part's colour from its hue, saturation and brightness.
  final int base;

  /// Gradient colours top to bottom (three equal ones when [flat]), at the
  /// positions 0, 0.45 and 1.
  final List<int> stops;

  /// `0x000000` or `0xffffff`.
  final int ink;

  /// The ink's worst contrast over what is under the legend.
  final double worst;

  /// Transparency kept, per cent (the rule may have given some back).
  final int transparencyKept;

  /// Painted as one colour, no gradient.
  final bool flat;

  /// Ink is black.
  bool get inkIsBlack => ink == _black;
}

/// The contrast rule for one part. [grounds] are the darkest and lightest
/// colour showing through, given for the wheel only; [need] is 4.5, or 7 with
/// Increase Contrast.
PaintedPart paintPart(
  PartPaint p,
  LookPart part, {
  List<int>? grounds,
  double need = contrastTarget,
}) {
  final amp = gradientAmplitude(part);
  final base = hsvToRgb(p.hue, p.saturation, p.brightness);
  var t = math.min(p.transparency, part.maxTransparency);
  var flat = false;

  late List<int> stops;
  late int ink;
  late double worst;
  void run() {
    stops = flat
        ? [base, base, base]
        : [mix(base, _white, amp), base, mix(base, _black, amp)];
    final under = (t > 0 && grounds != null)
        ? [
            for (final g in grounds)
              for (final s in stops) mix(s, g, t / 100),
          ]
        : stops;
    final black = under.map((u) => contrastRatio(_black, u)).reduce(math.min);
    final white = under.map((u) => contrastRatio(_white, u)).reduce(math.min);
    ink = black >= white ? _black : _white;
    worst = math.max(black, white);
  }

  run();
  if (worst < need) {
    flat = true;
    run();
  }
  while (worst < need && t > 0) {
    t -= transparencyStep;
    run();
  }
  return PaintedPart(
    base: base,
    stops: stops,
    ink: ink,
    worst: worst,
    transparencyKept: t,
    flat: flat,
  );
}

/// Increase Contrast: opaque, and brightness moved the least that makes the
/// legend reach 7:1 (down first, then up, at each distance).
PartPaint increaseContrastPaint(PartPaint p, LookPart part) {
  for (var d = 0; d <= 100; d++) {
    for (final v in [p.brightness - d, p.brightness + d]) {
      if (v < 0 || v > 100) continue;
      final q = PartPaint(hue: p.hue, saturation: p.saturation, brightness: v);
      if (paintPart(q, part, need: contrastTargetHigh).worst >=
          contrastTargetHigh) {
        return q;
      }
    }
  }
  return PartPaint(hue: p.hue, saturation: p.saturation, brightness: 0);
}
