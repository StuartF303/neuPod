// SPDX-License-Identifier: MPL-2.0

/// Colour arithmetic for the style rules, in pure Dart.
///
/// Colours are opaque sRGB integers `0xRRGGBB` (no alpha), the form the
/// design README writes them in. Contrast follows WCAG 2.x: the relative
/// luminance of linearised sRGB and the ratio `(L1 + 0.05) / (L2 + 0.05)`.
library;

import 'dart:math' as math;

/// Red, green and blue (0 to 255) of [rgb].
(int, int, int) channels(int rgb) =>
    ((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);

/// `0xRRGGBB` from three channels, each clamped to 0..255 and rounded.
int rgbOf(num r, num g, num b) {
  int c(num v) => v.round().clamp(0, 255);
  return (c(r) << 16) | (c(g) << 8) | c(b);
}

/// `#rrggbb`, lower case, as the design writes colours.
String hexOf(int rgb) =>
    '#${(rgb & 0xffffff).toRadixString(16).padLeft(6, '0')}';

double _linear(int channel) {
  final c = channel / 255.0;
  return c <= 0.04045
      ? c / 12.92
      : math.pow((c + 0.055) / 1.055, 2.4) as double;
}

/// WCAG relative luminance of [rgb], 0 (black) to 1 (white).
double relativeLuminance(int rgb) {
  final (r, g, b) = channels(rgb);
  return 0.2126 * _linear(r) + 0.7152 * _linear(g) + 0.0722 * _linear(b);
}

/// WCAG contrast ratio between two opaque colours, 1 to 21.
double contrastRatio(int a, int b) {
  final la = relativeLuminance(a);
  final lb = relativeLuminance(b);
  final hi = math.max(la, lb);
  final lo = math.min(la, lb);
  return (hi + 0.05) / (lo + 0.05);
}

/// Rounds half to even, as Python's `round` does, so the colour arithmetic
/// agrees with the design's `render.py` on exact halves.
int _roundHalfEven(double v) {
  final f = v.floorToDouble();
  final d = v - f;
  if (d < 0.5) return f.toInt();
  if (d > 0.5) return f.toInt() + 1;
  return f.toInt().isEven ? f.toInt() : f.toInt() + 1;
}

/// The colour for hue 0..359, saturation and brightness 0..100 (the usual HSV
/// conversion, `hsv()` in the Custom mode design's `render.py`).
int hsvToRgb(int hue, int saturation, int brightness) {
  final s = saturation / 100, v = brightness / 100;
  int f(int n) {
    final k = (n + hue / 60) % 6;
    final ramp = math.max(0.0, math.min(k, math.min(4 - k, 1.0)));
    return _roundHalfEven(255 * (v - v * s * ramp));
  }

  return (f(5) << 16) | (f(3) << 8) | f(1);
}

/// [a] moved toward [b] by [t] (0 to 1), per channel in sRGB bytes:
/// `round(a + (b - a) * t)`, half to even.
int mix(int a, int b, double t) {
  final (ar, ag, ab) = channels(a);
  final (br, bg, bb) = channels(b);
  int c(int x, int y) => _roundHalfEven(x + (y - x) * t);
  return (c(ar, br) << 16) | (c(ag, bg) << 8) | c(ab, bb);
}

/// A colour in HSL: hue in degrees (0 to 360), saturation and lightness 0 to 1.
final class Hsl {
  const Hsl(this.hue, this.saturation, this.lightness);

  final double hue;
  final double saturation;
  final double lightness;

  factory Hsl.fromRgb(int rgb) {
    final (ri, gi, bi) = channels(rgb);
    final r = ri / 255.0, g = gi / 255.0, b = bi / 255.0;
    final mx = math.max(r, math.max(g, b));
    final mn = math.min(r, math.min(g, b));
    final l = (mx + mn) / 2;
    if (mx == mn) return Hsl(0, 0, l);
    final d = mx - mn;
    final s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    double h;
    if (mx == r) {
      h = (g - b) / d + (g < b ? 6 : 0);
    } else if (mx == g) {
      h = (b - r) / d + 2;
    } else {
      h = (r - g) / d + 4;
    }
    return Hsl(h * 60, s, l);
  }

  /// This colour as `0xRRGGBB`, each channel rounded to the nearest integer.
  int toRgb() {
    final h = (hue % 360) / 360;
    final s = saturation.clamp(0.0, 1.0);
    final l = lightness.clamp(0.0, 1.0);
    if (s == 0) return rgbOf(l * 255, l * 255, l * 255);
    final q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    final p = 2 * l - q;
    double f(double t) {
      var x = t;
      if (x < 0) x += 1;
      if (x > 1) x -= 1;
      if (x < 1 / 6) return p + (q - p) * 6 * x;
      if (x < 1 / 2) return q;
      if (x < 2 / 3) return p + (q - p) * (2 / 3 - x) * 6;
      return p;
    }

    return rgbOf(f(h + 1 / 3) * 255, f(h) * 255, f(h - 1 / 3) * 255);
  }

  Hsl withLightness(double l) => Hsl(hue, saturation, l);

  @override
  String toString() =>
      'Hsl(${hue.toStringAsFixed(1)}, ${saturation.toStringAsFixed(3)}, '
      '${lightness.toStringAsFixed(3)})';
}
