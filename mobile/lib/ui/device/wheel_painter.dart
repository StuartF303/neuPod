// SPDX-License-Identifier: MPL-2.0

import 'dart:math' as math;

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/domain/wheel/wheel_input.dart';
import 'package:neupod/ui/device/device_paint.dart';

/// Paints the click wheel (design section 4) in its five states. Drawn in a
/// 200-unit square scaled to the wheel's diameter, with the same shapes as
/// the design's `render.py` (`wheel_svg`): ring of radius 99 with a 1 unit
/// edge, groove to 40, centre button of radius 36 (0.36 D across), glyphs on
/// a circle of 0.70 R.
///
/// The glyphs: Lucide's `skip-back` and `skip-forward` shapes (ISC), and the
/// play/pause and `MENU` of the design. Drawn fresh; nothing traced.
class WheelPainter extends CustomPainter {
  /// A painter for [devicePaint] showing [visual]; [disabled] dims the glyphs.
  WheelPainter({
    required this.devicePaint,
    required this.visual,
    this.disabled = false,
    this.menuFont = 'Montserrat',
  });

  /// What to paint with.
  final DevicePaint devicePaint;

  /// What is touched.
  final WheelVisual visual;

  /// Glyphs and `MENU` at 35 %, while the wheel cannot act (design section 4).
  final bool disabled;

  /// The family for `MENU`.
  final String menuFont;

  static const double _c = 36; // centre radius, of 100
  static const double _groove = _c + 4;

  @override
  void paint(Canvas canvas, Size size) {
    final k = size.width / 200;
    canvas.save();
    canvas.scale(k);
    const centre = Offset(100, 100);

    // Drop shadow under the wheel: 0 1px 1px rgba(0,0,0,.25).
    canvas.drawCircle(
      centre.translate(0, 1 / k),
      99.5,
      Paint()
        ..color = const Color(0x40000000)
        ..maskFilter = MaskFilter.blur(BlurStyle.normal, 0.5 / k),
    );

    final ringRect = Rect.fromCircle(center: centre, radius: 99);
    canvas.drawCircle(
      centre,
      99,
      Paint()..shader = _vertical(devicePaint.ring).createShader(ringRect),
    );
    canvas.drawCircle(
      centre,
      99,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = devicePaint.ringEdge,
    );

    switch (visual) {
      case WheelTouchingRing(:final angleDegrees):
        _wedge(canvas, angleDegrees - 25, angleDegrees + 25, devicePaint.touch);
      case WheelPressing(:final button) when button != WheelButton.centre:
        final mid = switch (button) {
          WheelButton.menu => 0.0,
          WheelButton.next => 90.0,
          WheelButton.playPause => 180.0,
          _ => 270.0,
        };
        _wedge(canvas, mid - 45, mid + 45, devicePaint.press);
      case _:
        break;
    }

    canvas.drawCircle(centre, _groove, Paint()..color = devicePaint.groove);
    final centreRect = Rect.fromCircle(center: centre, radius: _c);
    canvas.drawCircle(
      centre,
      _c,
      Paint()..shader = _vertical(devicePaint.centre).createShader(centreRect),
    );
    canvas.drawCircle(
      centre,
      _c,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 0.8
        ..color = devicePaint.centreEdge,
    );
    if (visual case WheelPressing(button: WheelButton.centre)) {
      canvas.drawCircle(centre, _c, Paint()..color = devicePaint.centrePress);
    }

    final glyph = Paint()
      ..color = devicePaint.glyph.withValues(alpha: disabled ? 0.35 : 1);
    _menu(canvas, glyph.color);
    // Next: translate(160 91) scale(.75), Lucide skip-forward.
    _glyph(canvas, const Offset(160, 91), 0.75, glyph, (p) {
      p.addPolygon(const [Offset(5, 4), Offset(15, 12), Offset(5, 20)], true);
      p.addRRect(_bar(17.5, 4, 3, 16));
    });
    // Previous: translate(22 91) scale(.75), Lucide skip-back.
    _glyph(canvas, const Offset(22, 91), 0.75, glyph, (p) {
      p.addPolygon(const [Offset(19, 20), Offset(9, 12), Offset(19, 4)], true);
      p.addRRect(_bar(3.5, 4, 3, 16));
    });
    // Play/pause: translate(88 166) scale(.72), the design's own glyph.
    _glyph(canvas, const Offset(88, 166), 0.72, glyph, (p) {
      p.addPolygon(const [Offset(2, 4), Offset(14, 12), Offset(2, 20)], true);
      p.addRRect(_bar(18, 4, 4, 16));
      p.addRRect(_bar(25, 4, 4, 16));
    });
    canvas.restore();
  }

  static RRect _bar(double x, double y, double w, double h) =>
      RRect.fromLTRBR(x, y, x + w, y + h, const Radius.circular(1));

  static LinearGradient _vertical(List<Color> c) => LinearGradient(
    begin: Alignment.topCenter,
    end: Alignment.bottomCenter,
    colors: c,
  );

  void _glyph(
    Canvas canvas,
    Offset at,
    double scale,
    Paint paint,
    void Function(Path) build,
  ) {
    final p = Path();
    build(p);
    canvas.save();
    canvas.translate(at.dx, at.dy);
    canvas.scale(scale);
    canvas.drawPath(p, paint);
    canvas.restore();
  }

  void _menu(Canvas canvas, Color color) {
    final tp = TextPainter(
      text: TextSpan(
        text: 'MENU',
        style: TextStyle(
          fontFamily: menuFont,
          fontWeight: FontWeight.w500,
          fontSize: 11.5,
          letterSpacing: 1.2,
          color: color,
          height: 1,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    // SVG: text-anchor middle at x 100, baseline at y 27.
    final baseline = tp.computeDistanceToActualBaseline(
      TextBaseline.alphabetic,
    );
    tp.paint(canvas, Offset(100 - tp.width / 2, 27 - baseline));
    tp.dispose();
  }

  /// The ring between the groove and the edge, from [a0] to [a1] degrees
  /// clockwise from the top.
  void _wedge(Canvas canvas, double a0, double a1, Color color) {
    Rect circle(double r) =>
        Rect.fromCircle(center: const Offset(100, 100), radius: r);
    double rad(double a) => (a - 90) * math.pi / 180;
    final sweep = (a1 - a0) * math.pi / 180;
    final p = Path()
      ..arcTo(circle(99), rad(a0), sweep, true)
      ..arcTo(circle(_groove), rad(a1), -sweep, false)
      ..close();
    canvas.drawPath(p, Paint()..color = color);
  }

  @override
  bool shouldRepaint(WheelPainter old) =>
      old.devicePaint != devicePaint ||
      old.disabled != disabled ||
      !_sameVisual(old.visual, visual);

  static bool _sameVisual(WheelVisual a, WheelVisual b) => switch ((a, b)) {
    (WheelIdle(), WheelIdle()) => true,
    (
      WheelTouchingRing(angleDegrees: final x),
      WheelTouchingRing(angleDegrees: final y),
    ) =>
      x == y,
    (WheelPressing(button: final x), WheelPressing(button: final y)) => x == y,
    _ => false,
  };
}
