// SPDX-License-Identifier: MPL-2.0

// Goldens of the device in Custom mode with see-through parts: Sea glass
// (body 55 %) and Clear case (body 85 %, wheel and centre 60 %) at 393x852,
// Clear case at 375x667 and 440x956 (the anchor), and the wheel's states over
// the blurred board. Compared with the design's look-part-transparent.png,
// look-clear-case.png and wheel-states-see-through.png. The board pictures
// are placeholders, and the screen is a text-free test pattern.

import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/board.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/domain/wheel/wheel_input.dart';
import 'package:neupod/ui/device/board_picture.dart';
import 'package:neupod/ui/device/custom_device_paint.dart';
import 'package:neupod/ui/device/device_view.dart';
import 'package:neupod/ui/device/wheel_painter.dart';

import '../device_test_harness.dart';

final CustomLook _seaGlass = CustomLooks.starters.looks[1];
final CustomLook _clear = CustomLooks.starters.looks[2];

void main() {
  Future<void> device(
    WidgetTester tester,
    String name,
    CustomLook look,
    Phone phone,
  ) async {
    final assets = await PreloadedBoardAssets.load(tester);
    await pumpDevice(
      tester,
      phone: phone,
      screen: const _Pattern(),
      look: look,
      overrides: [boardAssetsProvider.overrideWithValue(assets)],
    );
    await settleBoards(tester);
    await expectLater(find.byType(DeviceView), matchesGoldenFile('$name.png'));
  }

  testWidgets('device_393x852_custom_seaglass', (tester) async {
    await device(
      tester,
      'device_393x852_custom_seaglass',
      _seaGlass,
      designPhones[1],
    );
  });

  for (final phone in designPhones) {
    final name = 'device_${phone.name}_custom_clear';
    testWidgets(name, (tester) => device(tester, name, _clear, phone));
  }

  testWidgets('wheel_states_custom_clear', (tester) async {
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(944, 262);
    addTearDown(tester.view.reset);
    // Clear case over the first board, as the design's sheet draws it.
    const boardId = 'main-top';
    final assets = const BoardAssets();
    late BoardInfo info;
    late ui.Image frost;
    await tester.runAsync(() async {
      info = boardFor(parseBoards(await assets.loadCatalog()), boardId)!;
      frost = await assets.decode(info.frostPath);
    });
    addTearDown(frost.dispose);
    final paint = customDevicePaint(_clear, board: info);
    const diameter = 200.0;
    final unitsPerPixel = boardUnitsPerPixel(
      width: diameter / 0.64,
      wheelDiameter: diameter,
    );
    const states = <(WheelVisual, bool)>[
      (WheelIdle(), false),
      (WheelTouchingRing(62), false),
      (WheelPressing(WheelButton.menu), false),
      (WheelPressing(WheelButton.centre), false),
    ];
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: ColoredBox(
          color: Color(0xFF000000 | info.ground),
          child: DecoratedBox(
            decoration: BoxDecoration(gradient: paint.bodyGradient),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                for (final (v, disabled) in states)
                  CustomPaint(
                    size: const Size.square(diameter),
                    painter: WheelPainter(
                      devicePaint: paint,
                      visual: v,
                      disabled: disabled,
                      frost: frost,
                      frostUnitsPerPixel: unitsPerPixel,
                    ),
                  ),
              ],
            ),
          ),
        ),
      ),
    );
    await expectLater(
      find.byType(Directionality),
      matchesGoldenFile('wheel_states_custom_clear.png'),
    );
  });
}

/// A text-free 600x450 pattern, as in custom_golden_test.dart.
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
