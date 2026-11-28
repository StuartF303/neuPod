// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/style/colour_math.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/custom_paint.dart';
import 'package:neupod/ui/device/device_paint.dart';

/// The device's paint for a custom [look] (design sections 4, 5 and 8): the
/// contrast rule's colours, gradients and ink for each part, and the edges,
/// groove, wordmark and tints in ink.
///
/// Solid only for now: every part is painted opaque whatever its
/// transparency says. Transparency, the board and the cap come with the board
/// step; [increaseContrast] is the accessibility flag it will share (opaque,
/// 7:1, edges at 60 %). The stored look is never changed.
DevicePaint customDevicePaint(
  CustomLook look, {
  bool increaseContrast = false,
}) {
  PaintedPart paintOf(LookPart part) {
    var p = look.paintOf(part).clamped(part);
    p = increaseContrast
        ? increaseContrastPaint(p, part)
        : p.copyWith(transparency: 0);
    return paintPart(
      p,
      part,
      need: increaseContrast ? contrastTargetHigh : contrastTarget,
    );
  }

  final body = paintOf(LookPart.body);
  final wheel = paintOf(LookPart.wheel);
  final centre = paintOf(LookPart.centre);
  final edge = increaseContrast ? 0.6 : 0.3;

  Color solid(int rgb) => Color(0xFF000000 | rgb);
  Color ink(int rgb, double a) => solid(rgb).withValues(alpha: a);
  // Tints with the transparency kept at 0 (solid): section 4.
  double press(PaintedPart p) => p.inkIsBlack ? 0.13 : 0.14;
  double touch(PaintedPart p) => p.inkIsBlack ? 0.07 : 0.08;

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
  );
}
