// SPDX-License-Identifier: MPL-2.0

// Increase Contrast and Reduce Motion in Custom mode (design section 8): the
// device view reads the phone's setting and paints the look's solid,
// 7:1 version; the stored look is never changed.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/custom_paint.dart';
import 'package:neupod/ui/device/custom_device_paint.dart';

import 'device_test_harness.dart';

void main() {
  final harbour = CustomLooks.starters.looks[0];

  Iterable<Gradient> bodies(WidgetTester tester) => tester
      .widgetList<DecoratedBox>(find.byType(DecoratedBox))
      .map((d) => d.decoration)
      .whereType<BoxDecoration>()
      .map((d) => d.gradient)
      .whereType<Gradient>();

  testWidgets('Increase Contrast: the body is the 7:1 solid paint', (
    tester,
  ) async {
    tester.platformDispatcher.accessibilityFeaturesTestValue =
        const FakeAccessibilityFeatures(highContrast: true);
    addTearDown(tester.platformDispatcher.clearAccessibilityFeaturesTestValue);
    await pumpDevice(tester, look: harbour);
    final want = customDevicePaint(harbour, increaseContrast: true);
    final plain = customDevicePaint(harbour);
    expect(want.body, isNot(plain.body));
    expect(bodies(tester), contains(want.bodyGradient));
    expect(bodies(tester), isNot(contains(plain.bodyGradient)));
  });

  testWidgets('without it the body is the look as set', (tester) async {
    await pumpDevice(tester, look: harbour);
    expect(bodies(tester), contains(customDevicePaint(harbour).bodyGradient));
  });

  test('the 7:1 paint leaves the look as stored', () {
    final before = harbour.toJson();
    for (final part in LookPart.values) {
      final p = increaseContrastPaint(harbour.paintOf(part), part);
      expect(p.transparency, 0);
      expect(
        paintPart(p, part, need: contrastTargetHigh).worst,
        greaterThanOrEqualTo(7),
      );
    }
    expect(harbour.toJson(), before);
  });
}
