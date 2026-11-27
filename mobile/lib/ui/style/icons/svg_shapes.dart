// SPDX-License-Identifier: MPL-2.0

/// A small reader for the SVG shapes the icon set uses: `<path d>`, `<rect>`,
/// `<circle>`, `<line>` and `<polyline>`, in a 24x24 view box. Enough for the
/// Lucide subset and the glyphs drawn for the design; not a general SVG parser.
library;

import 'dart:ui';

final _element = RegExp(r'<(path|rect|circle|line|polyline)\b([^>]*?)/?>');
final _attribute = RegExp(r'([a-zA-Z-]+)="([^"]*)"');

/// The shapes of an SVG fragment (the elements inside `<svg>`), as one path.
/// With [fillCurrentColourOnly], only the elements marked
/// `fill="currentColor"` (the lit star, the record dot).
Path parseSvgShapes(String svg, {bool fillCurrentColourOnly = false}) {
  final path = Path();
  for (final m in _element.allMatches(svg)) {
    final attrs = {
      for (final a in _attribute.allMatches(m.group(2)!))
        a.group(1)!: a.group(2)!,
    };
    if (fillCurrentColourOnly && attrs['fill'] != 'currentColor') continue;
    double n(String k, [double d = 0]) =>
        attrs[k] == null ? d : double.parse(attrs[k]!);
    switch (m.group(1)) {
      case 'path':
        _addPathData(path, attrs['d'] ?? '');
      case 'rect':
        final rect = Rect.fromLTWH(n('x'), n('y'), n('width'), n('height'));
        final rx = n('rx', n('ry'));
        final ry = n('ry', rx);
        path.addRRect(RRect.fromRectXY(rect, rx, ry));
      case 'circle':
        path.addOval(
          Rect.fromCircle(center: Offset(n('cx'), n('cy')), radius: n('r')),
        );
      case 'line':
        path
          ..moveTo(n('x1'), n('y1'))
          ..lineTo(n('x2'), n('y2'));
      case 'polyline':
        final v = _numbers(attrs['points'] ?? '');
        for (var i = 0; i + 1 < v.length; i += 2) {
          i == 0 ? path.moveTo(v[i], v[i + 1]) : path.lineTo(v[i], v[i + 1]);
        }
    }
  }
  return path;
}

final _number = RegExp(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?');

List<double> _numbers(String s) =>
    _number.allMatches(s).map((m) => double.parse(m.group(0)!)).toList();

final _token = RegExp(
  r'[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?',
);

/// Appends SVG path data [d] to [path].
void _addPathData(Path path, String d) {
  final tokens = _token.allMatches(d).map((m) => m.group(0)!).toList();
  var i = 0;
  var cmd = '';
  var cx = 0.0, cy = 0.0; // current point
  var sx = 0.0, sy = 0.0; // subpath start
  var lcx = 0.0, lcy = 0.0; // last control point, for S and T
  var lastCubic = false, lastQuad = false;

  bool isCmd(String t) => RegExp(r'^[A-Za-z]$').hasMatch(t);
  double next() => double.parse(tokens[i++]);
  // Arc flags may be packed ("a1 1 0 011 1"); read one digit.
  bool flag() {
    final t = tokens[i];
    if (t.length > 1 && (t[0] == '0' || t[0] == '1')) {
      tokens[i] = t.substring(1);
      return t[0] == '1';
    }
    i++;
    return t == '1';
  }

  while (i < tokens.length) {
    if (isCmd(tokens[i])) cmd = tokens[i++];
    final rel = cmd == cmd.toLowerCase();
    final ox = rel ? cx : 0.0, oy = rel ? cy : 0.0;
    var cubic = false, quad = false;
    switch (cmd.toUpperCase()) {
      case 'M':
        cx = ox + next();
        cy = oy + next();
        path.moveTo(cx, cy);
        sx = cx;
        sy = cy;
        cmd = rel ? 'l' : 'L'; // further pairs are line-tos
      case 'L':
        cx = ox + next();
        cy = oy + next();
        path.lineTo(cx, cy);
      case 'H':
        cx = (rel ? cx : 0) + next();
        path.lineTo(cx, cy);
      case 'V':
        cy = (rel ? cy : 0) + next();
        path.lineTo(cx, cy);
      case 'C':
        final x1 = ox + next(), y1 = oy + next();
        final x2 = ox + next(), y2 = oy + next();
        cx = ox + next();
        cy = oy + next();
        path.cubicTo(x1, y1, x2, y2, cx, cy);
        lcx = x2;
        lcy = y2;
        cubic = true;
      case 'S':
        final x1 = lastCubic ? 2 * cx - lcx : cx;
        final y1 = lastCubic ? 2 * cy - lcy : cy;
        final x2 = ox + next(), y2 = oy + next();
        cx = ox + next();
        cy = oy + next();
        path.cubicTo(x1, y1, x2, y2, cx, cy);
        lcx = x2;
        lcy = y2;
        cubic = true;
      case 'Q':
        final x1 = ox + next(), y1 = oy + next();
        cx = ox + next();
        cy = oy + next();
        path.quadraticBezierTo(x1, y1, cx, cy);
        lcx = x1;
        lcy = y1;
        quad = true;
      case 'T':
        final x1 = lastQuad ? 2 * cx - lcx : cx;
        final y1 = lastQuad ? 2 * cy - lcy : cy;
        cx = ox + next();
        cy = oy + next();
        path.quadraticBezierTo(x1, y1, cx, cy);
        lcx = x1;
        lcy = y1;
        quad = true;
      case 'A':
        final rx = next(), ry = next(), rot = next();
        final large = flag(), sweep = flag();
        cx = ox + next();
        cy = oy + next();
        path.arcToPoint(
          Offset(cx, cy),
          radius: Radius.elliptical(rx, ry),
          rotation: rot,
          largeArc: large,
          clockwise: sweep,
        );
      case 'Z':
        path.close();
        cx = sx;
        cy = sy;
        cmd = ''; // numbers straight after Z are an error
      default:
        throw FormatException('unsupported path command "$cmd" in "$d"');
    }
    lastCubic = cubic;
    lastQuad = quad;
  }
}
