// SPDX-License-Identifier: MPL-2.0

// Goldens of the device view: the three design phones, fit A only, silver
// and black; and the wheel's five states in both finishes. They check the
// body, frame, screen placement and wheel. The screen inside is a text-free
// test pattern: scaled text rasterises differently on macOS (the CI
// `goldens` job's image, where the goldens are made) and on the Linux CI
// runner, and the screens have their own goldens.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/domain/wheel/wheel_input.dart';
import 'package:neupod/ui/device/device_finish.dart';
import 'package:neupod/ui/device/device_view.dart';
import 'package:neupod/ui/device/wheel_painter.dart';

import '../device_test_harness.dart';

void main() {
  for (final phone in designPhones) {
    for (final finish in DeviceFinish.values) {
      final name = 'device_${phone.name}_fit_a_${finish.name}';
      testWidgets(name, (tester) async {
        await pumpDevice(
          tester,
          phone: phone,
          screen: const _TestPattern(),
          brightness: finish == DeviceFinish.silver
              ? Brightness.light
              : Brightness.dark,
        );
        await expectLater(
          find.byType(DeviceView),
          matchesGoldenFile('$name.png'),
        );
      });
    }
  }

  for (final finish in DeviceFinish.values) {
    testWidgets('wheel_states_${finish.name}', (tester) async {
      tester.view.devicePixelRatio = 1;
      tester.view.physicalSize = const Size(1180, 290);
      addTearDown(tester.view.reset);
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
            decoration: BoxDecoration(gradient: finish.bodyGradient),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                for (final (v, disabled) in states)
                  CustomPaint(
                    size: const Size.square(200),
                    painter: WheelPainter(
                      finish: finish,
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
        matchesGoldenFile('wheel_states_${finish.name}.png'),
      );
    });
  }
}

/// A 600x450 pattern without text: a header band, a selection bar, rows and
/// a right-hand pane, so the screen's placement and scale show.
class _TestPattern extends StatelessWidget {
  const _TestPattern();

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
