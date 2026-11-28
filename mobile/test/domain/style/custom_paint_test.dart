// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/colour_math.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/custom_paint.dart';

/// The placeholder boards' frost extremes, from the design's render.py.
const Map<String, List<int>> _frost = {
  'main-top': [0x143022, 0x34543e],
  'main-bottom': [0x121a2c, 0x303c56],
  'copper': [0x2c1e12, 0x543c26],
};

PartPaint _p(int h, int s, int v, [int t = 0]) =>
    PartPaint(hue: h, saturation: s, brightness: v, transparency: t);

void main() {
  test('hsvToRgb and mix', () {
    expect(hexOf(hsvToRgb(205, 62, 44)), '#2b5370');
    expect(hexOf(hsvToRgb(0, 0, 100)), '#ffffff');
    expect(hexOf(hsvToRgb(0, 100, 100)), '#ff0000');
    expect(hexOf(hsvToRgb(120, 100, 50)), '#008000');
    expect(hexOf(hsvToRgb(240, 100, 100)), '#0000ff');
    expect(mix(0x000000, 0xffffff, 0.5), 0x808080); // 127.5 rounds to even: 128
    expect(mix(0x102030, 0xffffff, 0), 0x102030);
    expect(mix(0x102030, 0xffffff, 1), 0xffffff);
  });

  group('the brief\'s check values', () {
    void check(
      String what,
      PaintedPart r,
      String base,
      int ink,
      double worst,
      int kept,
      bool flat,
    ) {
      expect(hexOf(r.base), base, reason: what);
      expect(r.ink, ink, reason: what);
      expect(r.worst, closeTo(worst, 0.005), reason: what);
      expect(r.transparencyKept, kept, reason: what);
      expect(r.flat, flat, reason: what);
    }

    const black = 0x000000, white = 0xffffff;
    final top = _frost['main-top']!;

    test('Harbour', () {
      check(
        'body',
        paintPart(_p(205, 62, 44), LookPart.body),
        '#2b5370',
        white,
        6.28,
        0,
        false,
      );
      check(
        'wheel',
        paintPart(_p(40, 8, 94), LookPart.wheel, grounds: top),
        '#f0e9dd',
        black,
        15.28,
        0,
        false,
      );
      check(
        'centre',
        paintPart(_p(205, 62, 36), LookPart.centre),
        '#23445c',
        white,
        8.62,
        0,
        false,
      );
    });

    test('Sea glass', () {
      check(
        'body',
        paintPart(_p(165, 35, 70, 55), LookPart.body),
        '#74b2a3',
        black,
        7.04,
        55,
        false,
      );
      check(
        'wheel',
        paintPart(_p(165, 10, 96), LookPart.wheel, grounds: top),
        '#dcf5ef',
        black,
        16.06,
        0,
        false,
      );
      check(
        'centre',
        paintPart(_p(165, 40, 60), LookPart.centre),
        '#5c998a',
        black,
        5.71,
        0,
        false,
      );
    });

    test('Clear case', () {
      check(
        'body',
        paintPart(_p(200, 6, 92, 85), LookPart.body),
        '#dde6eb',
        black,
        13.30,
        85,
        false,
      );
      check(
        'wheel',
        paintPart(_p(200, 4, 96, 60), LookPart.wheel, grounds: top),
        '#ebf2f5',
        black,
        4.86,
        60,
        true,
      );
      check(
        'centre',
        paintPart(_p(200, 6, 90, 60), LookPart.centre),
        '#d8e1e6',
        black,
        13.95,
        60,
        false,
      );
    });

    test('Flare, the awkward case', () {
      check(
        'body',
        paintPart(_p(338, 88, 86), LookPart.body),
        '#db1a61',
        white,
        4.85,
        0,
        true,
      );
      check(
        'wheel',
        paintPart(_p(338, 70, 30), LookPart.wheel, grounds: top),
        '#4c172b',
        white,
        12.16,
        0,
        false,
      );
      check(
        'centre',
        paintPart(_p(20, 90, 95), LookPart.centre),
        '#f26118',
        black,
        5.77,
        0,
        false,
      );
    });
  });

  test('a flat part has three equal stops; a gradient mixes toward white and black', () {
    final flat = paintPart(_p(338, 88, 86), LookPart.body);
    expect(flat.stops, [flat.base, flat.base, flat.base]);
    final grad = paintPart(_p(205, 62, 44), LookPart.body);
    expect(grad.stops[0], mix(grad.base, 0xffffff, 0.10));
    expect(grad.stops[1], grad.base);
    expect(grad.stops[2], mix(grad.base, 0x000000, 0.10));
  });

  test('transparency is capped per part, and given back when needed', () {
    expect(paintPart(_p(0, 0, 90, 99), LookPart.body).transparencyKept, 85);
    expect(paintPart(_p(0, 0, 90, 99), LookPart.wheel).transparencyKept, 60);
    // A mid grey wheel over a mixed ground cannot keep all of its 60 %.
    final r = paintPart(
      _p(0, 0, 50, 60),
      LookPart.wheel,
      grounds: _frost['main-top'],
    );
    expect(r.worst, greaterThanOrEqualTo(contrastTarget));
    expect(r.transparencyKept, lessThanOrEqualTo(60));
  });

  group('the walk', () {
    // Hue in 15s, saturation in 10s, brightness in 5s, every part.
    Iterable<PartPaint> grid([int t = 0]) sync* {
      for (var h = 0; h < 360; h += 15) {
        for (var s = 0; s <= 100; s += 10) {
          for (var v = 0; v <= 100; v += 5) {
            yield _p(h, s, v, t);
          }
        }
      }
    }

    test(
      'every colour reaches 4.5:1, solid, and the wheel at 60% over each frost',
      () {
        for (final p in grid()) {
          for (final part in LookPart.values) {
            final r = paintPart(p, part);
            expect(
              r.worst,
              greaterThanOrEqualTo(contrastTarget),
              reason: '$part $p',
            );
          }
        }
        for (final p in grid(60)) {
          for (final frost in _frost.values) {
            final r = paintPart(p, LookPart.wheel, grounds: frost);
            expect(
              r.worst,
              greaterThanOrEqualTo(contrastTarget),
              reason: 'wheel $p $frost',
            );
          }
          // The body passes no grounds (the rule measures the body's own
          // colours only), so this is not a see-through measurement.
          final b = paintPart(p.copyWith(transparency: 85), LookPart.body);
          expect(
            b.worst,
            greaterThanOrEqualTo(contrastTarget),
            reason: 'body $p',
          );
        }
      },
    );

    test('Increase Contrast reaches 7:1 for every colour', () {
      for (final p in grid()) {
        for (final part in LookPart.values) {
          final q = increaseContrastPaint(p.copyWith(transparency: 40), part);
          expect(q.transparency, 0);
          expect(q.hue, p.hue);
          expect(q.saturation, p.saturation);
          expect(
            paintPart(q, part, need: contrastTargetHigh).worst,
            greaterThanOrEqualTo(contrastTargetHigh),
            reason: '$part $p',
          );
        }
      }
    });
  });

  test('Increase Contrast moves brightness the least, down first', () {
    // Already passes: unchanged.
    expect(increaseContrastPaint(_p(0, 0, 0), LookPart.body), _p(0, 0, 0));
    // Mid grey is the worst case for ink. White ink would need brightness 35
    // or less (15 away), black 59 or more (9 away): the nearer one wins.
    expect(increaseContrastPaint(_p(0, 0, 50), LookPart.body).brightness, 59);
  });
}
