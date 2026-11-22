// SPDX-License-Identifier: MPL-2.0

// Goldens of the device view: the three design phones, fit A and B, silver
// and black, with the placeholder screen; and the wheel's five states in
// both finishes. Text is drawn in the test font, so these check the body,
// frame, screen placement and wheel, not type.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/domain/wheel/wheel_input.dart';
import 'package:neupod/ui/device/device_finish.dart';
import 'package:neupod/ui/device/device_geometry.dart';
import 'package:neupod/ui/device/device_options.dart';
import 'package:neupod/ui/device/device_view.dart';
import 'package:neupod/ui/device/wheel_painter.dart';

import '../device_test_harness.dart';

void main() {
  for (final phone in designPhones) {
    for (final fit in DeviceFit.values) {
      for (final finish in DeviceFinish.values) {
        final f = fit == DeviceFit.fillsPhone ? 'a' : 'b';
        final name = 'device_${phone.name}_fit_${f}_${finish.name}';
        testWidgets(name, (tester) async {
          final c = await pumpDevice(tester, phone: phone);
          c
              .read(deviceViewOptionsProvider.notifier)
              .update(
                (o) => o.copyWith(
                  fit: fit,
                  body: finish == DeviceFinish.silver
                      ? BodySetting.silver
                      : BodySetting.black,
                ),
              );
          await tester.pump();
          await expectLater(
            find.byType(DeviceView),
            matchesGoldenFile('$name.png'),
          );
        });
      }
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
