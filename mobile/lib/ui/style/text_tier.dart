// SPDX-License-Identifier: MPL-2.0

/// The three text tiers of the device view design (section 8, "Largest text
/// size"). The screen stays 600x450; type and bars are multiplied by [k] and
/// lists show fewer, taller rows.
library;

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

enum TextTier {
  /// Up to Large on iOS (the default), font scale under 1.15 on Android.
  standard(
    k: 1,
    rowsOneLine: 7,
    rowsTwoLine: 5,
    statusBarHeight: 45,
    rowHeight: 56,
    row2Height: 75,
  ),

  /// Extra Large and Extra Extra Large on iOS; 1.15 to under 1.3 on Android.
  large(
    k: 7 / 6,
    rowsOneLine: 6,
    rowsTwoLine: 4,
    statusBarHeight: 52,
    rowHeight: 66,
    row2Height: 99,
  ),

  /// Extra Extra Extra Large and the accessibility sizes on iOS (capped here,
  /// device view Open question 3); 1.3 and above on Android.
  largest(
    k: 7 / 5,
    rowsOneLine: 5,
    rowsTwoLine: 3,
    statusBarHeight: 63,
    rowHeight: 77,
    row2Height: 129,
  );

  const TextTier({
    required this.k,
    required this.rowsOneLine,
    required this.rowsTwoLine,
    required this.statusBarHeight,
    required this.rowHeight,
    required this.row2Height,
  });

  /// The factor applied to type and bars.
  final double k;

  /// Rows on a page of one-line rows and of two-line rows.
  final int rowsOneLine;
  final int rowsTwoLine;

  /// Heights in design px (600x450 space).
  final double statusBarHeight;
  final double rowHeight;
  final double row2Height;

  /// Scales a design length that grows with the text (type, bars, glyphs).
  double s(double designPx) => designPx * k;

  /// The tier for the phone's text scale.
  ///
  /// iOS reports its text sizes as a scale of 17 pt Body: Large 1.0, Extra
  /// Large 19/17 (1.12), Extra Extra Large 21/17 (1.24), Extra Extra Extra
  /// Large 23/17 (1.35); the thresholds sit between them. Android uses the
  /// font scale thresholds of the design's table (1.15 and 1.3).
  static TextTier fromTextScale(double scale, {required bool ios}) {
    if (ios) {
      if (scale >= 1.3) return largest;
      if (scale >= 1.05) return large;
      return standard;
    }
    if (scale >= 1.3) return largest;
    if (scale >= 1.15) return large;
    return standard;
  }

  /// The tier for [context]'s text scaler and the target platform.
  static TextTier of(BuildContext context) {
    final scale = MediaQuery.textScalerOf(context).scale(100) / 100;
    final ios = defaultTargetPlatform == TargetPlatform.iOS;
    return fromTextScale(scale, ios: ios);
  }
}
