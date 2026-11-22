// SPDX-License-Identifier: MPL-2.0

/// The colours Modern takes from a track's album art: the dominant colour
/// (which becomes the accent) and a second main colour, for the backdrop's two
/// washes (UI styles design, "Modern, Dark: the values").
///
/// Pure Dart: it works on the RGBA bytes of a small copy of the art, which the
/// UI layer decodes once per track (`ui/style/art_colours.dart`).
library;

import 'dart:typed_data';

import 'package:neupod/domain/style/accent.dart';
import 'package:neupod/domain/style/colour_math.dart';

/// The two main colours of a piece of art and what Modern derives from them.
final class ArtPalette {
  const ArtPalette({required this.dominant, required this.secondary});

  /// Derives the palette from RGBA pixels (4 bytes a pixel, row-major), as
  /// returned by `Image.toByteData(format: ImageByteFormat.rawRgba)`.
  ///
  /// The rule (ours; the design names only "the art's main colours"):
  /// pixels are grouped into 512 buckets (3 bits a channel) and each bucket is
  /// scored by its pixel count times (0.5 + its saturation), so a colourful
  /// area beats an equally large grey or near-black one. Pixels that are
  /// nearly transparent, near black (lightness under 0.06) or near white (over
  /// 0.94) are left out unless nothing else is left. The dominant colour is the
  /// mean of the best bucket; the secondary is the mean of the best bucket
  /// that differs from it by at least 30 degrees of hue or 0.2 of lightness,
  /// or, failing that, the dominant colour at half its lightness.
  ///
  /// Returns null when [rgba] holds no opaque pixel.
  static ArtPalette? fromRgba(Uint8List rgba) {
    final buckets = <int, _Bucket>{};
    final fallback = <int, _Bucket>{};
    for (var i = 0; i + 3 < rgba.length; i += 4) {
      if (rgba[i + 3] < 128) continue;
      final r = rgba[i], g = rgba[i + 1], b = rgba[i + 2];
      final key = ((r >> 5) << 6) | ((g >> 5) << 3) | (b >> 5);
      final mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
      final mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
      final l = (mx + mn) / 510.0;
      final target = (l < 0.06 || l > 0.94) ? fallback : buckets;
      (target[key] ??= _Bucket()).add(r, g, b);
    }
    final pool = buckets.isNotEmpty ? buckets : fallback;
    if (pool.isEmpty) return null;

    final ranked = pool.values.toList()
      ..sort((a, b) => b.score.compareTo(a.score));
    final dominant = ranked.first.mean;
    final dh = Hsl.fromRgb(dominant);
    int? secondary;
    for (final bucket in ranked.skip(1)) {
      final h = Hsl.fromRgb(bucket.mean);
      final hueGap = (h.hue - dh.hue).abs();
      final hueDiff = hueGap > 180 ? 360 - hueGap : hueGap;
      final distinctHue = hueDiff >= 30 && h.saturation > 0.1;
      if (distinctHue || (h.lightness - dh.lightness).abs() >= 0.2) {
        secondary = bucket.mean;
        break;
      }
    }
    secondary ??= dh.withLightness(dh.lightness / 2).toRgb();
    return ArtPalette(dominant: dominant, secondary: secondary);
  }

  /// The art's dominant colour, `0xRRGGBB`.
  final int dominant;

  /// The art's second main colour, `0xRRGGBB`.
  final int secondary;

  /// The accent: [dominant] clamped until white text on it reaches 4.5:1.
  int get accent => clampAccentForWhiteText(dominant);

  /// The progress bar fill in Modern.
  int get progressFill => progressTint(dominant);

  /// The colour at the centre of the backdrop's first wash: the dominant
  /// colour itself, as in every Modern mock-up.
  int get washPrimary => dominant;

  /// The colour at the centre of the backdrop's second wash. The mock-ups draw
  /// it about 1.35 times brighter than the art's colour (`#16324f` washes from
  /// `rgb(30,70,110)`, `#5a3e1b` from `rgb(120,82,36)`), so it shows on black.
  int get washSecondary {
    final (r, g, b) = channels(secondary);
    return rgbOf(r * 1.35, g * 1.35, b * 1.35);
  }

  @override
  bool operator ==(Object other) =>
      other is ArtPalette &&
      other.dominant == dominant &&
      other.secondary == secondary;

  @override
  int get hashCode => Object.hash(dominant, secondary);

  @override
  String toString() =>
      'ArtPalette(dominant: ${hexOf(dominant)}, secondary: ${hexOf(secondary)})';
}

final class _Bucket {
  int n = 0;
  int r = 0, g = 0, b = 0;

  void add(int rr, int gg, int bb) {
    n++;
    r += rr;
    g += gg;
    b += bb;
  }

  int get mean => rgbOf(r / n, g / n, b / n);

  double get score => n * (0.5 + Hsl.fromRgb(mean).saturation);
}
