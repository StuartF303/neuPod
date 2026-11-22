// SPDX-License-Identifier: MPL-2.0

// The device view's layout and switches: where the screen and wheel land,
// the screen hosted at 600x450, fits, finishes, the wordmark, the status bar
// style, direct touch candidates and the gate panel.

import 'package:flutter/semantics.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/ui/device/click_wheel.dart';
import 'package:neupod/ui/device/device_finish.dart';
import 'package:neupod/ui/device/device_geometry.dart';
import 'package:neupod/ui/device/device_options.dart';
import 'package:neupod/ui/device/gate_panel.dart';
import 'package:neupod/ui/device/placeholder_screen.dart';
import 'package:neupod/ui/device/screen_host.dart';
import 'package:neupod/ui/device/wheel_bus.dart';

import 'device_test_harness.dart';

class _Probe extends StatelessWidget {
  const _Probe(this.onTap);

  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    behavior: HitTestBehavior.opaque,
    onTap: onTap,
    child: LayoutBuilder(
      builder: (context, c) => Text(
        '${c.maxWidth.toInt()}x${c.maxHeight.toInt()}',
        textDirection: TextDirection.ltr,
      ),
    ),
  );
}

void main() {
  _zeroSizeTests();
  for (final phone in designPhones) {
    for (final fit in DeviceFit.values) {
      testWidgets('${phone.name} ${fit.name}: screen and wheel where the '
          'geometry puts them', (tester) async {
        final c = await pumpDevice(tester, phone: phone);
        c
            .read(deviceViewOptionsProvider.notifier)
            .update((o) => o.copyWith(fit: fit));
        await tester.pump();
        final g = DeviceGeometry.compute(
          width: phone.w,
          height: phone.h,
          safeTop: phone.top,
          safeBottom: phone.bottom,
          fit: fit,
        );
        final host = tester.getRect(find.byType(DeviceScreenHost));
        expect(host.left, closeTo(g.screen.left, 1e-6));
        expect(host.top, closeTo(g.screen.top, 1e-6));
        expect(host.width, closeTo(g.screen.width, 1e-6));
        expect(host.height, closeTo(g.screen.height, 1e-6));
        final wheel = tester.getRect(find.byType(ClickWheel));
        expect(wheel.center.dx, closeTo(g.wheelCentre.dx, 1e-6));
        expect(wheel.center.dy, closeTo(g.wheelCentre.dy, 1e-6));
        expect(wheel.width, closeTo(g.wheelDiameter * 1.06, 1e-6));
      });
    }
  }

  testWidgets('the screen is laid out at 600x450 and scaled by s', (
    tester,
  ) async {
    var taps = 0;
    await pumpDevice(tester, screen: _Probe(() => taps++));
    expect(find.text('600x450'), findsOneWidget);
    final probe = tester.getRect(find.byType(_Probe));
    expect(probe.width, closeTo(0.88 * 393, 1e-6));
    await tester.tapAt(probe.center);
    expect(taps, 1);
  });

  testWidgets('direct touch candidate 1: the screen takes no touches', (
    tester,
  ) async {
    var taps = 0;
    final c = await pumpDevice(tester, screen: _Probe(() => taps++));
    c
        .read(deviceViewOptionsProvider.notifier)
        .update((o) => o.copyWith(directTouch: DirectTouch.wheelOnly));
    await tester.pump();
    await tester.tapAt(tester.getRect(find.byType(_Probe)).center);
    expect(taps, 0);
  });

  testWidgets('candidate 1 keeps the rows activatable by the screen reader', (
    tester,
  ) async {
    final handle = tester.ensureSemantics();
    final c = await pumpDevice(tester);
    c
        .read(deviceViewOptionsProvider.notifier)
        .update((o) => o.copyWith(directTouch: DirectTouch.wheelOnly));
    await tester.pump();
    final row = find.semantics.byLabel('Settings, 3 of 5');
    expect(row, findsOneWidget);
    tester.semantics.tap(row);
    await tester.pump();
    expect(find.text('open Settings'), findsOneWidget);
    handle.dispose();
  });

  testWidgets('a row tap in candidate 2 selects and opens it', (tester) async {
    await pumpDevice(tester);
    await tester.tap(find.text('Shuffle Songs'));
    await tester.pump();
    expect(find.text('open Shuffle Songs'), findsOneWidget);
  });

  testWidgets('the wordmark follows its switch and the gap rule', (
    tester,
  ) async {
    final c = await pumpDevice(tester);
    Finder mark() => find.descendant(
      of: find.byType(Positioned),
      matching: find.text('neuPod'),
    );
    expect(mark(), findsWidgets);
    final before = tester.widgetList(find.text('neuPod')).length;
    c
        .read(deviceViewOptionsProvider.notifier)
        .update((o) => o.copyWith(showWordmark: false));
    await tester.pump();
    expect(tester.widgetList(find.text('neuPod')).length, before - 1);
  });

  testWidgets('status bar text: dark on silver, light on black and on the '
      'fit B surround', (tester) async {
    SystemUiOverlayStyle style() => tester
        .widget<AnnotatedRegion<SystemUiOverlayStyle>>(
          find.byType(AnnotatedRegion<SystemUiOverlayStyle>).first,
        )
        .value;
    final c = await pumpDevice(tester);
    expect(style().statusBarIconBrightness, Brightness.dark);
    expect(style().statusBarBrightness, Brightness.light);
    c
        .read(deviceViewOptionsProvider.notifier)
        .update((o) => o.copyWith(body: BodySetting.black));
    await tester.pump();
    expect(style().statusBarIconBrightness, Brightness.light);
    c
        .read(deviceViewOptionsProvider.notifier)
        .update(
          (o) => o.copyWith(
            body: BodySetting.silver,
            fit: DeviceFit.fixedProportions,
          ),
        );
    await tester.pump();
    expect(style().statusBarIconBrightness, Brightness.light);
  });

  testWidgets('the body follows the mode unless pinned', (tester) async {
    expect(
      const DeviceViewOptions().finishFor(Brightness.light),
      DeviceFinish.silver,
    );
    expect(
      const DeviceViewOptions().finishFor(Brightness.dark),
      DeviceFinish.black,
    );
    expect(
      const DeviceViewOptions(body: BodySetting.black)
          .finishFor(Brightness.light),
      DeviceFinish.black,
    );
    final c = await pumpDevice(tester, brightness: Brightness.dark);
    final wheel = tester.widget<ClickWheel>(find.byType(ClickWheel));
    expect(wheel.finish, DeviceFinish.black);
    c
        .read(deviceViewOptionsProvider.notifier)
        .update((o) => o.copyWith(body: BodySetting.silver));
    await tester.pump();
    expect(
      tester.widget<ClickWheel>(find.byType(ClickWheel)).finish,
      DeviceFinish.silver,
    );
  });

  testWidgets('a long press on the body opens the gate switches', (
    tester,
  ) async {
    final c = await pumpDevice(tester);
    await tester.longPressAt(const Offset(8, 500));
    await tester.pumpAndSettle();
    expect(find.byType(GatePanel), findsOneWidget);
    await tester.tap(find.text('B: fixed'));
    await tester.pump();
    expect(c.read(deviceViewOptionsProvider).fit, DeviceFit.fixedProportions);
    // The panel scrolls; Now Playing's section made it longer than the phone.
    await tester.ensureVisible(find.text('Done'));
    await tester.pump();
    await tester.tap(find.text('Done'));
    await tester.pump();
    expect(find.byType(GatePanel), findsNothing);
  });

  testWidgets('the placeholder shows the specification\'s Home items', (
    tester,
  ) async {
    await pumpDevice(tester);
    for (final item in PlaceholderScreen.items) {
      expect(find.text(item), findsOneWidget);
    }
    expect(find.text('Podcasts'), findsNothing);
  });

  testWidgets('the wordmark stays 0.036 W at a large text size', (
    tester,
  ) async {
    tester.platformDispatcher.textScaleFactorTestValue = 2;
    addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
    await pumpDevice(tester);
    final mark = find.byWidgetPredicate(
      (w) =>
          w is Text &&
          w.data == 'neuPod' &&
          w.textScaler == TextScaler.noScaling,
    );
    expect(mark, findsOneWidget);
    // The test font's line box is the font size: 0.036 x 393 = 14.1 pt.
    expect(tester.getSize(mark).height, closeTo(0.036 * 393, 0.5));
  });

  testWidgets('the gate panel takes the keys while open', (tester) async {
    final c = await pumpDevice(tester);
    final events = <Object>[];
    c.read(wheelBusProvider).events.listen(events.add);
    await tester.longPressAt(const Offset(8, 500));
    await tester.pumpAndSettle();
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
    await tester.pump();
    expect(events, isEmpty);
  });

  testWidgets('the body\'s long press is not offered to the screen reader', (
    tester,
  ) async {
    final handle = tester.ensureSemantics();
    await pumpDevice(tester);
    expect(
      find.semantics.byPredicate(
        (n) => n.getSemanticsData().hasAction(SemanticsAction.longPress),
      ),
      findsNothing,
    );
    handle.dispose();
  });
}

void _useSize(WidgetTester tester, Size size) {
  tester.view.physicalSize = size;
  tester.view.padding = FakeViewPadding.zero;
  tester.view.viewPadding = FakeViewPadding.zero;
}

void _zeroSizeTests() {
  const tooSmall = <String, Size>{
    'Size.zero': Size.zero,
    '1x1': Size(1, 1),
    '100x100': Size(100, 100),
    'just below the minimum width': Size(343, 852),
    'just below the minimum height': Size(393, 400),
  };
  for (final e in tooSmall.entries) {
    testWidgets('${e.key}: builds an empty body, then lays out at 393x852', (
      tester,
    ) async {
      await pumpDevice(tester);
      _useSize(tester, e.value);
      await tester.pump();
      expect(tester.takeException(), isNull);
      expect(find.byType(DeviceScreenHost), findsNothing);
      expect(find.byType(ClickWheel), findsNothing);

      _useSize(tester, const Size(393, 852));
      await tester.pump();
      expect(tester.takeException(), isNull);
      final g = DeviceGeometry.compute(
        width: 393,
        height: 852,
        safeTop: 0,
        safeBottom: 0,
      );
      final host = tester.getRect(find.byType(DeviceScreenHost));
      expect(host.left, closeTo(g.screen.left, 1e-6));
      expect(host.width, closeTo(g.screen.width, 1e-6));
      expect(find.byType(ClickWheel), findsOneWidget);
    });
  }

  test('geometry never has negative sizes at tiny windows', () {
    for (final s in const [Size.zero, Size(1, 1), Size(100, 100)]) {
      final g = DeviceGeometry.compute(
        width: s.width,
        height: s.height,
        safeTop: 0,
        safeBottom: 0,
      );
      expect(g.canLayOut, isFalse);
      expect(g.screen.width, greaterThanOrEqualTo(0));
      expect(g.screen.height, greaterThanOrEqualTo(0));
      expect(g.wheelDiameter, greaterThanOrEqualTo(0));
    }
  });
}
