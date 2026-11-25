// SPDX-License-Identifier: MPL-2.0

/// The icon set: a subset of Lucide 1.72.0 (ISC; some icons derive from
/// Feather, MIT) and the few glyphs drawn for the UI styles design.
///
/// Each glyph carries the shapes of its SVG file in `assets/icons/`, copied
/// here so icons draw synchronously (no asset load before the first frame).
/// `test/ui/style/icons_test.dart` fails if a copy and its file disagree. The
/// licence texts ship as assets and are listed on the Licences screen.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/style/icons/svg_shapes.dart';

/// How a glyph is painted.
enum GlyphPaint {
  /// Stroked outline (every Lucide icon).
  stroke,

  /// Filled shape (the Classic play and pause glyphs).
  fill,

  /// Filled and stroked with round joins (the Modern play glyph).
  fillAndStroke,
}

enum NeupodGlyph {
  chevronRight('lucide/chevron-right', '<path d="m9 18 6-6-6-6" />'),
  check('lucide/check', '<path d="M20 6 9 17l-5-5" />'),
  headphones(
    'lucide/headphones',
    '<path d="M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-7a9 9 0 '
        '0 1 18 0v7a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3" />',
  ),
  bluetooth('lucide/bluetooth', '<path d="m7 7 10 10-5 5V2l5 5L7 17" />'),
  bluetoothConnected(
    'lucide/bluetooth-connected',
    '<path d="m7 7 10 10-5 5V2l5 5L7 17" />'
        '<path d="M18 12h3" />'
        '<path d="M3 12h3" />',
  ),
  speaker(
    'lucide/speaker',
    '<rect width="16" height="20" x="4" y="2" rx="2" />'
        '<path d="M12 6h.01" />'
        '<circle cx="12" cy="14" r="4" />'
        '<path d="M12 14h.01" />',
  ),
  shuffle(
    'lucide/shuffle',
    '<path d="m18 14 4 4-4 4" />'
        '<path d="m18 2 4 4-4 4" />'
        '<path d="M2 18h1.973a4 4 0 0 0 3.3-1.7l5.454-8.6a4 4 0 0 1 3.3-1.7H22" />'
        '<path d="M2 6h1.972a4 4 0 0 1 3.6 2.2" />'
        '<path d="M22 18h-6.041a4 4 0 0 1-3.3-1.8l-.359-.45" />',
  ),
  repeat(
    'lucide/repeat',
    '<path d="m17 2 4 4-4 4" />'
        '<path d="M3 11v-1a4 4 0 0 1 4-4h14" />'
        '<path d="m7 22-4-4 4-4" />'
        '<path d="M21 13v1a4 4 0 0 1-4 4H3" />',
  ),
  repeatOne(
    'lucide/repeat-1',
    '<path d="m17 2 4 4-4 4" />'
        '<path d="M3 11v-1a4 4 0 0 1 4-4h14" />'
        '<path d="m7 22-4-4 4-4" />'
        '<path d="M21 13v1a4 4 0 0 1-4 4H3" />'
        '<path d="M11 10h1v4" />',
  ),
  volume(
    'lucide/volume',
    '<path d="M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 '
        '5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 '
        '3.384A.705.705 0 0 0 11 19.298z" />',
  ),
  volumeLoud(
    'lucide/volume-2',
    '<path d="M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 '
        '5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 '
        '3.384A.705.705 0 0 0 11 19.298z" />'
        '<path d="M16 9a5 5 0 0 1 0 6" />'
        '<path d="M19.364 18.364a9 9 0 0 0 0-12.728" />',
  ),
  lock(
    'lucide/lock',
    '<rect width="18" height="11" x="3" y="11" rx="2" ry="2" />'
        '<path d="M7 11V7a5 5 0 0 1 10 0v4" />',
  ),
  star(
    'lucide/star',
    '<path d="M11.525 2.295a.53.53 0 0 1 .95 0l2.31 4.679a2.123 2.123 0 0 0 '
        '1.595 1.16l5.166.756a.53.53 0 0 1 .294.904l-3.736 3.638a2.123 2.123 0 0 '
        '0-.611 1.878l.882 5.14a.53.53 0 0 1-.771.56l-4.618-2.428a2.122 2.122 0 0 '
        '0-1.973 0L6.396 21.01a.53.53 0 0 1-.77-.56l.881-5.139a2.122 2.122 0 0 '
        '0-.611-1.879L2.16 9.795a.53.53 0 0 1 .294-.906l5.165-.755a2.122 2.122 0 0 '
        '0 1.597-1.16z" />',
  ),
  search(
    'lucide/search',
    '<path d="m21 21-4.34-4.34" />'
        '<circle cx="11" cy="11" r="8" />',
  ),
  jackPlug(
    'neupod/jack-plug',
    '<path d="M10 13V4.5a2 2 0 0 1 4 0V13" />'
        '<path d="M10 7.5h4" />'
        '<path d="M10 10.5h4" />'
        '<rect x="8" y="13" width="8" height="7" rx="1.5" />'
        '<path d="M12 20v2.5" />',
  ),
  play('neupod/play', '<path d="M6 3l14 9-14 9z" />'),
  playRounded('neupod/play-rounded', '<path d="M7 4l12 8-12 8z" />'),
  pause(
    'neupod/pause',
    '<rect x="5" y="3" width="5" height="18" rx="1" />'
        '<rect x="14" y="3" width="5" height="18" rx="1" />',
  ),
  pauseRounded(
    'neupod/pause-rounded',
    '<rect x="5" y="3" width="5" height="18" rx="1.5" />'
        '<rect x="14" y="3" width="5" height="18" rx="1.5" />',
  );

  const NeupodGlyph(this.file, this.shapes);

  /// The SVG file under `assets/icons/`, without `.svg`.
  final String file;

  /// The SVG elements of the glyph, as in [file].
  final String shapes;

  static final Map<NeupodGlyph, Path> _paths = {};

  /// The glyph's outline in its 24x24 view box (parsed once).
  Path get path => _paths[this] ??= parseSvgShapes(shapes);
}

/// Draws a [NeupodGlyph] at [size] design px. [strokeWidth] is in the glyph's
/// 24-unit view box, as the mock-ups' `stroke-width` attributes give it.
class NeupodIcon extends StatelessWidget {
  const NeupodIcon(
    this.glyph, {
    super.key,
    required this.size,
    required this.color,
    this.strokeWidth = 2,
    this.paint = GlyphPaint.stroke,
    this.fillColor,
  });

  final NeupodGlyph glyph;
  final double size;
  final Color color;
  final double strokeWidth;
  final GlyphPaint paint;

  /// The fill for [GlyphPaint.fillAndStroke] (the Classic volume glyph fills the
  /// speaker body); defaults to [color].
  final Color? fillColor;

  @override
  Widget build(BuildContext context) => ExcludeSemantics(
    child: CustomPaint(
      size: Size.square(size),
      painter: _GlyphPainter(glyph, color, strokeWidth, paint, fillColor),
    ),
  );
}

class _GlyphPainter extends CustomPainter {
  _GlyphPainter(this.glyph, this.color, this.strokeWidth, this.mode, this.fill);

  final NeupodGlyph glyph;
  final Color color;
  final double strokeWidth;
  final GlyphPaint mode;
  final Color? fill;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.save();
    canvas.scale(size.width / 24, size.height / 24);
    final path = glyph.path;
    if (mode != GlyphPaint.stroke) {
      canvas.drawPath(
        path,
        Paint()
          ..color = fill ?? color
          ..style = PaintingStyle.fill
          ..isAntiAlias = true,
      );
    }
    if (mode != GlyphPaint.fill) {
      canvas.drawPath(
        path,
        Paint()
          ..color = color
          ..style = PaintingStyle.stroke
          ..strokeWidth = strokeWidth
          ..strokeCap = StrokeCap.round
          ..strokeJoin = StrokeJoin.round
          ..isAntiAlias = true,
      );
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(_GlyphPainter old) =>
      old.glyph != glyph ||
      old.color != color ||
      old.strokeWidth != strokeWidth ||
      old.mode != mode ||
      old.fill != fill;
}
