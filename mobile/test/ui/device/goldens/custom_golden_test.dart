// SPDX-License-Identifier: MPL-2.0

// Goldens of the device in Custom mode, solid parts: Harbour, Chalk, Flare and
// the two default looks on the 393x852 phone, and the wheel's five states in
// Harbour. Compared with the design's look-opaque.png,
// awkward-pale-classic-light.png, awkward-saturated-modern-dark.png,
// default-light.png and default-dark.png. Like device_view_golden_test.dart,
// the screen is a text-free test pattern.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/domain/wheel/wheel_input.dart';
import 'package:neupod/ui/device/custom_device_paint.dart';
import 'package:neupod/ui/device/device_view.dart';
import 'package:neupod/ui/device/wheel_painter.dart';

import '../device_test_harness.dart';

CustomLook _look(
  String name,
  (int, int, int) body,
  (int, int, int) wheel,
  (int, int, int) centre,
) => CustomLook(
  id: name,
  name: name,
  body: PartPaint(hue: body.$1, saturation: body.$2, brightness: body.$3),
  wheel: PartPaint(hue: wheel.$1, saturation: wheel.$2, brightness: wheel.$3),
  centre: PartPaint(
    hue: centre.$1,
    saturation: centre.$2,
    brightness: centre.$3,
  ),
);

/// The looks of the design's render.py (LOOKS), solid.
final Map<String, CustomLook> _looks = {
  'harbour': CustomLooks.starters.looks.first,
  'chalk': _look('Chalk', (48, 10, 98), (48, 4, 100), (48, 14, 94)),
  'flare': _look('Flare', (338, 88, 86), (338, 70, 30), (20, 90, 95)),
  'default_light': _look('Look 4', (218, 4, 88), (220, 2, 95), (220, 4, 87)),
  'default_dark': _look('Look 4', (225, 20, 8), (223, 12, 14), (228, 19, 6)),
};

void main() {
  for (final MapEntry(key: key, value: look) in _looks.entries) {
    final name = 'device_393x852_custom_$key';
    testWidgets(name, (tester) async {
      await pumpDevice(tester, screen: const _Pattern(), look: look);
      await expectLater(
        find.byType(DeviceView),
        matchesGoldenFile('$name.png'),
      );
    });
  }

  // Increase Contrast: Sea glass painted solid, its legends at 7:1, edges at
  // 60 % (design a11y-increase-contrast.png).
  testWidgets('device_393x852_custom_seaglass_increase_contrast', (
    tester,
  ) async {
    tester.platformDispatcher.accessibilityFeaturesTestValue =
        const FakeAccessibilityFeatures(highContrast: true);
    addTearDown(tester.platformDispatcher.clearAccessibilityFeaturesTestValue);
    await pumpDevice(
      tester,
      screen: const _Pattern(),
      look: CustomLooks.starters.looks[1],
    );
    await expectLater(
      find.byType(DeviceView),
      matchesGoldenFile('device_393x852_custom_seaglass_increase_contrast.png'),
    );
  });

  testWidgets('wheel_states_custom_harbour', (tester) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(1180, 290);
    addTearDown(tester.view.reset);
    final paint = customDevicePaint(_looks['harbour']!);
    const states = <(WheelVisual, bool)>[
      (WheelIdle(), false),
      (WheelTouchingRing(62), false),
      (WheelPressing(WheelButton.menu), false),
      (WheelPressing(WheelButton.centre), false),
      (WheelIdle(), true),
    ];
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: DecoratedBox(
          decoration: BoxDecoration(gradient: paint.bodyGradient),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.spaceEvenly,
            children: [
              for (final (v, disabled) in states)
                CustomPaint(
                  size: const Size.square(200),
                  painter: WheelPainter(
                    devicePaint: paint,
                    visual: v,
                    disabled: disabled,
                  ),
                ),
            ],
          ),
        ),
      ),
    );
    await expectLater(
      find.byType(Directionality),
      matchesGoldenFile('wheel_states_custom_harbour.png'),
    );
  });
}

/// A text-free 600x450 pattern, as in device_view_golden_test.dart.
class _Pattern extends StatelessWidget {
  const _Pattern();

  @override
  Widget build(BuildContext context) {
    Widget bar(double top, double h, Color c, {double w = 336}) => Positioned(
      left: 0,
      top: top,
      width: w,
      height: h,
      child: ColoredBox(color: c),
    );
    return Stack(
      children: [
        const Positioned.fill(child: ColoredBox(color: Color(0xFFFFFFFF))),
        bar(0, 45, const Color(0xFFC6CAD0), w: 600),
        bar(45, 56, const Color(0xFF1858C8)),
        for (var i = 1; i < 7; i++)
          Positioned(
            left: 19,
            top: 45 + 56.0 * i + 18,
            width: 160.0 + 20 * (i % 3),
            height: 20,
            child: const ColoredBox(color: Color(0xFF181818)),
          ),
        const Positioned(
          left: 336,
          top: 45,
          right: 0,
          bottom: 0,
          child: ColoredBox(color: Color(0xFFE9EBEE)),
        ),
      ],
    );
  }
}
