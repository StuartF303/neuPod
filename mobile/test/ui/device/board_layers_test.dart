// SPDX-License-Identifier: MPL-2.0

// The see-through layers of Custom mode (design section 5): the board picture
// anchored on the wheel's centre, the cap, the badge, the frost under the
// wheel; and that no picture is decoded unless a part needs one.

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/accessibility_port.dart';
import 'package:neupod/domain/style/board.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/ui/device/board_picture.dart';
import 'package:neupod/ui/device/click_wheel.dart';
import 'package:neupod/app/accessibility_providers.dart';
import 'package:neupod/ui/device/device_accessibility.dart';
import 'package:neupod/ui/device/device_options.dart';

import 'device_test_harness.dart';

final CustomLook _harbour = CustomLooks.starters.looks[0];
final CustomLook _seaGlass = CustomLooks.starters.looks[1];
final CustomLook _clear = CustomLooks.starters.looks[2];

class _ReduceTransparencyOn implements AccessibilityPort {
  @override
  Future<bool> reduceTransparency() async => true;

  @override
  Stream<bool> reduceTransparencyChanges() => const Stream.empty();
}

void main() {
  group('the picture is anchored on the wheel\'s centre', () {
    for (final phone in designPhones) {
      testWidgets(phone.name, (tester) async {
        final assets = await PreloadedBoardAssets.load(tester);
        await pumpDevice(
          tester,
          phone: phone,
          look: _clear,
          overrides: [boardAssetsProvider.overrideWithValue(assets)],
        );
        await settleBoards(tester);
        final picture = tester.getRect(find.byType(RawImage));
        final k = phone.w / boardPictureWidth;
        expect(picture.width, closeTo(boardPictureWidth * k, 1e-6));
        expect(picture.height, closeTo(boardPictureHeight * k, 1e-6));
        final anchor =
            picture.topLeft + Offset(boardAnchorX * k, boardAnchorY * k);
        final wheel = tester.getRect(find.byType(ClickWheel)).center;
        expect(anchor.dx, closeTo(wheel.dx, 1e-6));
        expect(anchor.dy, closeTo(wheel.dy, 1e-6));
      });
    }
  });

  testWidgets('a look with every part solid decodes no picture', (
    tester,
  ) async {
    final assets = await PreloadedBoardAssets.load(tester);
    await pumpDevice(
      tester,
      look: _harbour,
      overrides: [boardAssetsProvider.overrideWithValue(assets)],
    );
    await settleBoards(tester);
    expect(assets.decoded, isEmpty);
    expect(find.byType(RawImage), findsNothing);
  });

  testWidgets('a see-through body decodes the sharp picture only', (
    tester,
  ) async {
    final assets = await PreloadedBoardAssets.load(tester);
    await pumpDevice(
      tester,
      look: _seaGlass, // body 55 %, wheel and centre solid
      overrides: [boardAssetsProvider.overrideWithValue(assets)],
    );
    await settleBoards(tester);
    expect(assets.decoded, ['assets/boards/main-top.webp']);
  });

  testWidgets('a see-through wheel decodes the frost copy only, once', (
    tester,
  ) async {
    final assets = await PreloadedBoardAssets.load(tester);
    final look = _harbour.copyWith(
      wheel: _harbour.wheel.copyWith(transparency: 30),
    );
    final container = await pumpDevice(
      tester,
      look: look,
      overrides: [boardAssetsProvider.overrideWithValue(assets)],
    );
    await settleBoards(tester);
    expect(assets.decoded, ['assets/boards/main-top.frost.webp']);
    final wheel = tester.widget<ClickWheel>(find.byType(ClickWheel));
    expect(wheel.paint.ringOpacity, 0.7);
    expect(wheel.paint.centreOpacity, 1);
    expect(wheel.paint.hasBoard, isTrue);
    expect(wheel.paint.hasCap, isFalse, reason: 'the body is solid');
    // Another build does not decode again.
    container
        .read(deviceViewOptionsProvider.notifier)
        .update((o) => o.copyWith(haptics: false));
    await tester.pump();
    expect(assets.decoded, hasLength(1));
  });

  testWidgets('Clear case: board, cap and badge', (tester) async {
    final assets = await PreloadedBoardAssets.load(tester);
    await pumpDevice(
      tester,
      look: _clear,
      overrides: [boardAssetsProvider.overrideWithValue(assets)],
    );
    await settleBoards(tester);
    final wheel = tester.widget<ClickWheel>(find.byType(ClickWheel));
    expect(wheel.paint.hasBoard, isTrue);
    expect(wheel.paint.hasCap, isTrue);
    expect(wheel.paint.hasBadge, isTrue);
    expect(wheel.paint.bodyOpacity, closeTo(0.15, 1e-9));
    expect(wheel.paint.ringOpacity, lessThan(1));
    expect(wheel.paint.ringOpacity, greaterThanOrEqualTo(0.4));
    expect(wheel.frost, isNotNull);
  });

  testWidgets('Reduce Transparency: solid, no picture is loaded', (
    tester,
  ) async {
    final assets = await PreloadedBoardAssets.load(tester);
    await pumpDevice(
      tester,
      look: _clear,
      overrides: [
        boardAssetsProvider.overrideWithValue(assets),
        accessibilityPortProvider.overrideWithValue(_ReduceTransparencyOn()),
      ],
    );
    await settleBoards(tester);
    expect(assets.decoded, isEmpty);
    expect(find.byType(RawImage), findsNothing);
    final wheel = tester.widget<ClickWheel>(find.byType(ClickWheel));
    expect(wheel.paint.bodyOpacity, 1);
    expect(wheel.paint.ringOpacity, 1);
    expect(wheel.paint.centreOpacity, 1);
    expect(wheel.paint.hasBoard || wheel.paint.hasCap, isFalse);
    expect(wheel.paint.hasBadge, isFalse);
    expect(wheel.frost, isNull);
    expect(_clear.body.transparency, 85, reason: 'the look is not changed');
  });

  testWidgets('Reduce Transparency changing at run time repaints', (
    tester,
  ) async {
    final changes = StreamController<bool>.broadcast();
    addTearDown(changes.close);
    final assets = await PreloadedBoardAssets.load(tester);
    final container = await pumpDevice(
      tester,
      look: _seaGlass,
      overrides: [
        boardAssetsProvider.overrideWithValue(assets),
        accessibilityPortProvider.overrideWithValue(_Changing(changes.stream)),
      ],
    );
    await settleBoards(tester);
    expect(
      tester.widget<ClickWheel>(find.byType(ClickWheel)).paint.bodyOpacity,
      closeTo(0.45, 1e-9),
    );
    changes.add(true);
    await tester.pump();
    await tester.pump();
    expect(container.read(reduceTransparencyProvider), isTrue);
    expect(
      tester.widget<ClickWheel>(find.byType(ClickWheel)).paint.bodyOpacity,
      1,
    );
  });

  testWidgets('Increase Contrast: solid, no picture is loaded', (tester) async {
    tester.platformDispatcher.accessibilityFeaturesTestValue =
        const FakeAccessibilityFeatures(highContrast: true);
    addTearDown(tester.platformDispatcher.clearAccessibilityFeaturesTestValue);
    final assets = await PreloadedBoardAssets.load(tester);
    await pumpDevice(
      tester,
      look: _clear,
      overrides: [boardAssetsProvider.overrideWithValue(assets)],
    );
    await settleBoards(tester);
    expect(assets.decoded, isEmpty);
    final wheel = tester.widget<ClickWheel>(find.byType(ClickWheel));
    expect(wheel.paint.bodyOpacity, 1);
    expect(wheel.paint.ringEdge.a, closeTo(0.6, 0.01));
  });

  testWidgets('Reduce Transparency has a reason for the editor', (
    tester,
  ) async {
    expect(reduceTransparencyReason, 'Reduce Transparency is on');
  });
}

class _Changing implements AccessibilityPort {
  _Changing(this.changes);

  final Stream<bool> changes;

  @override
  Future<bool> reduceTransparency() async => false;

  @override
  Stream<bool> reduceTransparencyChanges() => changes;
}
