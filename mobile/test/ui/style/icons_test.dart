// SPDX-License-Identifier: MPL-2.0

import 'dart:io';

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/ui/style/icons/neupod_glyphs.dart';
import 'package:neupod/ui/style/icons/svg_shapes.dart';

final _element = RegExp(r'<(path|rect|circle|line|polyline)\b[^>]*?/?>');

/// The shape elements of an SVG text, whitespace normalised.
List<String> _shapes(String svg) => _element
    .allMatches(svg)
    .map(
      (m) =>
          m.group(0)!.replaceAll(RegExp(r'\s+'), ' ').replaceAll(' />', '/>'),
    )
    .toList();

void main() {
  test('every glyph matches its SVG file in assets/icons', () {
    for (final g in NeupodGlyph.values) {
      final file = File('assets/icons/${g.file}.svg');
      expect(file.existsSync(), isTrue, reason: file.path);
      expect(
        _shapes(g.shapes),
        _shapes(file.readAsStringSync()),
        reason: g.name,
      );
    }
  });

  test('every Lucide file is a glyph and carries its licence line', () {
    final files = Directory('assets/icons/lucide')
        .listSync()
        .whereType<File>()
        .where((f) => f.path.endsWith('.svg'));
    final used = {
      for (final g in NeupodGlyph.values) 'assets/icons/${g.file}.svg',
    };
    for (final f in files) {
      final path = f.path.replaceAll(r'\', '/');
      expect(used, contains(path), reason: 'unused icon $path');
      expect(
        f.readAsStringSync(),
        contains('@license lucide-static v1.72.0 - ISC'),
      );
    }
    expect(File('assets/icons/lucide/LICENSE-Lucide.txt').existsSync(), isTrue);
  });

  test('every glyph parses and stays inside its 24x24 view box', () {
    for (final g in NeupodGlyph.values) {
      final b = g.path.getBounds();
      expect(b.isEmpty, isFalse, reason: g.name);
      const box = Rect.fromLTRB(-0.01, -0.01, 24.01, 24.01);
      expect(
        box.contains(b.topLeft) && box.contains(b.bottomRight),
        isTrue,
        reason: '${g.name} $b',
      );
    }
  });

  test(
    'the path reader handles packed numbers, relative commands and arcs',
    () {
      final p = parseSvgShapes(
        '<path d="M2 2h4v4H2z m10 0l.5.5-.5.5a2 2 0 0 1 2 2" />',
      );
      final b = p.getBounds();
      expect(b.left, closeTo(2, 1e-6));
      expect(b.top, closeTo(2, 1e-6));
      expect(b.right, greaterThan(12));
      expect(
        () => parseSvgShapes('<path d="M0 0z 3 4" />'),
        throwsFormatException,
      );
    },
  );
}
