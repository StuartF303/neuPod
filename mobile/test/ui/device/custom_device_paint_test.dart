// SPDX-License-Identifier: MPL-2.0

import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/ui/device/custom_device_paint.dart';
import 'package:neupod/ui/device/device_finish.dart';

void main() {
  test('silver and black map onto DevicePaint with their table values', () {
    final s = DeviceFinish.silver.paint;
    expect(s.body, DeviceFinish.silver.body);
    expect(s.ring, DeviceFinish.silver.ring);
    expect(s.centre, DeviceFinish.silver.centre);
    expect(s.ringEdge, const Color(0xFFA9AEB6));
    expect(s.centreEdge, s.ringEdge, reason: 'one edge colour in the finishes');
    expect(s.press, const Color(0x21000000));
    expect(s.centrePress, s.press);
    expect(s.darkStatusText, isTrue);
    expect(s.hasBoard || s.hasCap || s.hasBadge, isFalse);
    expect(s.bodyOpacity, 1);
    expect(s.frameShadow.single.color, const Color(0x59FFFFFF));
    expect(DeviceFinish.black.paint.darkStatusText, isFalse);
    expect(DeviceFinish.black.paint, DeviceFinish.black.paint);
    expect(DeviceFinish.black.paint, isNot(DeviceFinish.silver.paint));
  });

  group('Harbour', () {
    final p = customDevicePaint(CustomLooks.starters.looks.first);

    test('body, wheel and centre in their colours with gradients', () {
      expect(p.body.length, 3);
      expect(p.body[1], const Color(0xFF2B5370));
      expect(p.centre.first.a, 1);
      expect(p.ring.length, 2);
      expect(p.bodyStops, [0, 0.45, 1]);
    });

    test('legends, edges, groove and tints are in ink', () {
      // Wheel #f0e9dd takes black ink; body #2b5370 takes white.
      expect(p.glyph, const Color(0xFF000000));
      expect(p.wordmark, const Color(0xFFFFFFFF));
      expect(p.ringEdge, const Color(0xFF000000).withValues(alpha: 0.3));
      expect(p.groove, const Color(0xFF000000).withValues(alpha: 0.22));
      expect(p.press, const Color(0xFF000000).withValues(alpha: 0.13));
      expect(p.touch, const Color(0xFF000000).withValues(alpha: 0.07));
      // Centre #23445c takes white: its edge and press are white.
      expect(p.centreEdge, const Color(0xFFFFFFFF).withValues(alpha: 0.3));
      expect(p.centrePress, const Color(0xFFFFFFFF).withValues(alpha: 0.14));
      expect(
        p.frameShadow.single.color,
        const Color(0xFFFFFFFF).withValues(alpha: 0.16),
      );
      expect(p.frameShadow.single.spreadRadius, 1);
    });

    test('status bar ink follows the body\'s top colour', () {
      expect(p.darkStatusText, isFalse, reason: 'a dark body: light icons');
      final chalk = customDevicePaint(
        CustomLooks.starters.looks.first.copyWith(
          body: const PartPaint(hue: 48, saturation: 10, brightness: 98),
        ),
      );
      expect(chalk.darkStatusText, isTrue);
    });
  });

  test('Flare\'s body is painted flat', () {
    final p = customDevicePaint(
      CustomLooks.starters.looks.first.copyWith(
        body: const PartPaint(hue: 338, saturation: 88, brightness: 86),
      ),
    );
    expect(p.body[0], p.body[1]);
    expect(p.body[1], p.body[2]);
  });

  test(
    'transparency is ignored while parts are solid; the look is untouched',
    () {
      final clear = CustomLooks.starters.looks[2];
      final p = customDevicePaint(clear);
      expect(p.bodyOpacity, 1);
      expect(p.hasBoard, isFalse);
      expect(clear.body.transparency, 85);
    },
  );

  test('Increase Contrast: edges at 60 % and 7:1 colours', () {
    final p = customDevicePaint(
      CustomLooks.starters.looks.first,
      increaseContrast: true,
    );
    expect(p.ringEdge.a, closeTo(0.6, 0.01));
  });
}
