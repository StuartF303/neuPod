// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/style/board.dart';
import 'package:neupod/domain/style/colour_math.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/custom_paint.dart';
import 'package:neupod/ui/device/device_paint.dart';

/// The device's paint for a custom [look] (design sections 4, 5 and 8): the
/// contrast rule's colours, gradients and ink for each part, and the edges,
/// groove, wordmark and tints in ink.
///
/// Parts are see-through only when [board] is given (its frost pair is what
/// the wheel's legends are measured over) and neither [reduceTransparency]
/// nor [increaseContrast] is on; otherwise every part is painted solid, the
/// cap, badge and board are off, and the stored look is not changed.
/// [increaseContrast] also asks for 7:1 legends and edges at 60 %.
DevicePaint customDevicePaint(
  CustomLook look, {
  BoardInfo? board,
  bool reduceTransparency = false,
  bool increaseContrast = false,
}) {
  final solidOnly = board == null || reduceTransparency || increaseContrast;
  PaintedPart paintOf(LookPart part) {
    var p = look.paintOf(part).clamped(part);
    p = increaseContrast
        ? increaseContrastPaint(p, part)
        : (solidOnly ? p.copyWith(transparency: 0) : p);
    return paintPart(
      p,
      part,
      // The frost pair goes with the wheel only (section 4).
      grounds: (part == LookPart.wheel && !solidOnly) ? board.frostPair : null,
      need: increaseContrast ? contrastTargetHigh : contrastTarget,
    );
  }

  final body = paintOf(LookPart.body);
  final wheel = paintOf(LookPart.wheel);
  final centre = paintOf(LookPart.centre);
  final edge = increaseContrast ? 0.6 : 0.3;

  Color solid(int rgb) => Color(0xFF000000 | rgb);
  Color ink(int rgb, double a) => solid(rgb).withValues(alpha: a);
  // Tints strengthen with the transparency kept (section 5).
  double kept(PaintedPart p) => p.transparencyKept / 100;
  double press(PaintedPart p) => (p.inkIsBlack ? 0.13 : 0.14) + 0.10 * kept(p);
  double touch(PaintedPart p) => (p.inkIsBlack ? 0.07 : 0.08) + 0.06 * kept(p);
  double opacity(PaintedPart p) => 1 - kept(p);

  return DevicePaint(
    body: [for (final c in body.stops) solid(c)],
    bodyStops: const [0, 0.45, 1],
    ring: [solid(wheel.stops.first), solid(wheel.stops.last)],
    ringEdge: ink(wheel.ink, edge),
    groove: ink(wheel.ink, 0.22),
    centre: [solid(centre.stops.first), solid(centre.stops.last)],
    centreEdge: ink(centre.ink, edge),
    glyph: solid(wheel.ink),
    wordmark: solid(body.ink),
    press: ink(wheel.ink, press(wheel)),
    centrePress: ink(centre.ink, press(centre)),
    touch: ink(wheel.ink, touch(wheel)),
    darkStatusText: relativeLuminance(body.stops.first) >= inkSwitchLuminance,
    frameShadow: [BoxShadow(color: ink(body.ink, 0.16), spreadRadius: 1)],
    bodyOpacity: opacity(body),
    ringOpacity: opacity(wheel),
    centreOpacity: opacity(centre),
    hasCap: body.transparencyKept > 0,
    hasBadge: body.transparencyKept > 0,
    hasBoard:
        body.transparencyKept > 0 ||
        wheel.transparencyKept > 0 ||
        centre.transparencyKept > 0,
  );
}
