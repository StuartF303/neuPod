// SPDX-License-Identifier: MPL-2.0

/// The part editor's colour wheel (design section 2): hue round the circle,
/// red at the top and clockwise; saturation from the centre (none) to the rim
/// (full); the whole disc darkened by the brightness; a 26 px ring marks the
/// colour. Touching or dragging it sets hue and saturation together.
///
/// For the screen reader it is one element, `Colour, hue 205 degrees,
/// saturation 62 percent`, not adjustable (the click wheel is the adjustable
/// element).
library;

import 'dart:math' as math;

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/style/colour_math.dart';

/// Called with a hue (0 to 359) and a saturation (0 to 100).
typedef ColourPicked = void Function(int hue, int saturation);

/// The disc, [size] across.
class ColourWheel extends StatelessWidget {
  /// A wheel showing [hue], [saturation] and [brightness].
  const ColourWheel({
    required this.hue,
    required this.saturation,
    required this.brightness,
    required this.size,
    this.onPicked,
    this.line = const Color(0xFF9DA2A9),
    super.key,
  });

  final int hue;
  final int saturation;
  final int brightness;
  final double size;
  final ColourPicked? onPicked;

  /// The disc's 1 px outline.
  final Color line;

  /// The hue and saturation at [p] on a disc of [size], clamped to the rim.
  static (int, int) pick(Offset p, double size) {
    final c = size / 2;
    final dx = p.dx - c;
    final dy = p.dy - c;
    var deg = math.atan2(dx, -dy) * 180 / math.pi;
    if (deg < 0) deg += 360;
    final h = deg.round() % 360;
    final s = (math.sqrt(dx * dx + dy * dy) / c * 100).round().clamp(0, 100);
    return (h, s);
  }

  void _at(Offset p) {
    final (h, s) = pick(p, size);
    onPicked?.call(h, s);
  }

  @override
  Widget build(BuildContext context) => Semantics(
    label: 'Colour, hue $hue degrees, saturation $saturation percent',
    child: ExcludeSemantics(
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onPanDown: (d) => _at(d.localPosition),
        onPanUpdate: (d) => _at(d.localPosition),
        child: CustomPaint(
          size: Size.square(size),
          painter: _WheelPainter(hue, saturation, brightness, line),
        ),
      ),
    ),
  );
}

class _WheelPainter extends CustomPainter {
  _WheelPainter(this.hue, this.saturation, this.brightness, this.line);

  final int hue;
  final int saturation;
  final int brightness;
  final Color line;

  static const _rainbow = [
    Color(0xFFFF0000),
    Color(0xFFFFFF00),
    Color(0xFF00FF00),
    Color(0xFF00FFFF),
    Color(0xFF0000FF),
    Color(0xFFFF00FF),
    Color(0xFFFF0000),
  ];

  @override
  void paint(Canvas canvas, Size size) {
    final c = size.center(Offset.zero);
    final r = size.width / 2;
    final rect = Rect.fromCircle(center: c, radius: r);
    final clip = Path()..addOval(rect);
    canvas.save();
    canvas.clipPath(clip);
    // Hue, red at the top and clockwise.
    canvas.drawRect(
      rect,
      Paint()
        ..shader = SweepGradient(
          colors: _rainbow,
          transform: const GradientRotation(-math.pi / 2),
        ).createShader(rect),
    );
    // Saturation: white at the centre, nothing at the rim.
    canvas.drawRect(
      rect,
      Paint()
        ..shader = const RadialGradient(
          colors: [Color(0xFFFFFFFF), Color(0x00FFFFFF)],
        ).createShader(rect),
    );
    // Brightness darkens the whole disc.
    canvas.drawRect(
      rect,
      Paint()
        ..color = Color.fromARGB(
          ((1 - brightness / 100) * 255).round(),
          0,
          0,
          0,
        ),
    );
    canvas.restore();
    canvas.drawCircle(
      c,
      r - 0.5,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = line,
    );
    // The ring on the colour.
    final a = hue * math.pi / 180;
    final rad = saturation / 100 * r;
    final at = Offset(c.dx + math.sin(a) * rad, c.dy - math.cos(a) * rad);
    final fill = Color(0xFF000000 | hsvToRgb(hue, saturation, brightness));
    canvas.drawCircle(at, 13, Paint()..color = fill);
    canvas.drawCircle(
      at,
      11.5,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 3
        ..color = const Color(0xFFFFFFFF),
    );
    for (final rr in [13.0, 10.0]) {
      canvas.drawCircle(
        at,
        rr,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.5
          ..color = const Color(0xFF000000),
      );
    }
  }

  @override
  bool shouldRepaint(_WheelPainter old) =>
      old.hue != hue ||
      old.saturation != saturation ||
      old.brightness != brightness ||
      old.line != line;
}
