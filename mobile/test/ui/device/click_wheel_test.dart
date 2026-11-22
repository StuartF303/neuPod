// SPDX-License-Identifier: MPL-2.0

// Gestures on the click wheel inside the device view, end to end: pointer
// events on the screen to WheelEvents on the bus, the haptics asked for, the
// screen-reader element and the keyboard.

import 'dart:math' as math;

import 'package:flutter/semantics.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod/domain/wheel/firmware_constants.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/device/click_wheel.dart';
import 'package:neupod/ui/device/device_geometry.dart';
import 'package:neupod/ui/device/wheel_bus.dart';

import 'device_test_harness.dart';

class _Rig {
  _Rig(this.tester, this.container, this.clock, this.haptics, this.g) {
    container.read(wheelBusProvider).events.listen(events.add);
  }

  final WidgetTester tester;
  final ProviderContainer container;
  final TestClock clock;
  final RecordingHaptics haptics;
  final DeviceGeometry g;
  final List<WheelEvent> events = [];

  WheelBus get bus => container.read(wheelBusProvider);

  Offset at(double deg, [double r = 0.75]) {
    final t = deg * math.pi / 180;
    return g.wheelCentre +
        Offset(math.sin(t), -math.cos(t)) * (r * g.wheelRadius);
  }

  Future<void> wait(int ms) async {
    clock.ms += ms;
    await tester.pump(Duration(milliseconds: ms));
  }
}

Future<_Rig> _rig(WidgetTester tester) async {
  final clock = TestClock();
  final haptics = RecordingHaptics();
  final phone = designPhones[1];
  final c = await pumpDevice(tester, clock: clock, haptics: haptics);
  final g = DeviceGeometry.compute(
    width: phone.w,
    height: phone.h,
    safeTop: phone.top,
    safeBottom: phone.bottom,
  );
  return _Rig(tester, c, clock, haptics, g);
}

void main() {
  testWidgets('a tap on each button presses it, with the down and up haptics', (
    tester,
  ) async {
    final r = await _rig(tester);
    for (final (deg, b) in [
      (0.0, WheelButton.menu),
      (90.0, WheelButton.next),
      (180.0, WheelButton.playPause),
      (270.0, WheelButton.previous),
    ]) {
      r.events.clear();
      r.haptics.played.clear();
      await tester.tapAt(r.at(deg));
      expect(r.events, [WheelButtonDown(b), WheelButtonUp(b), WheelPress(b)]);
      expect(r.haptics.played, [
        FeedbackKind.buttonDown,
        FeedbackKind.buttonUp,
      ]);
    }
    expect(r.haptics.prepared, 4);
  });

  testWidgets('the centre is Select', (tester) async {
    final r = await _rig(tester);
    await tester.tapAt(r.g.wheelCentre);
    expect(r.events.last, const WheelPress(WheelButton.centre));
  });

  testWidgets('a touch in the dead band does nothing', (tester) async {
    final r = await _rig(tester);
    await tester.tapAt(r.at(0, 0.40));
    expect(r.events, isEmpty);
    expect(r.haptics.played, isEmpty);
  });

  testWidgets('the ring takes touches 6 % beyond its drawn edge', (
    tester,
  ) async {
    final r = await _rig(tester);
    await tester.tapAt(r.at(90, 1.05));
    expect(r.events.last, const WheelPress(WheelButton.next));
    r.events.clear();
    await tester.tapAt(r.at(90, 1.10));
    expect(r.events, isEmpty);
  });

  testWidgets('turning a quarter turn clockwise is 6 steps; the press is '
      'cancelled with no release haptic', (tester) async {
    final r = await _rig(tester);
    final gesture = await tester.startGesture(r.at(0));
    for (var a = 1; a <= 90; a++) {
      await r.wait(20);
      await gesture.moveTo(r.at(a.toDouble()));
    }
    await gesture.up();
    await tester.pump();
    expect(r.events.first, const WheelButtonDown(WheelButton.menu));
    expect(r.events[1], const WheelButtonCancelled(WheelButton.menu));
    final steps = r.events.whereType<WheelStep>();
    expect(steps.fold<int>(0, (s, e) => s + e.steps), 6);
    expect(r.events.whereType<WheelPress>(), isEmpty);
    expect(r.haptics.played.first, FeedbackKind.buttonDown);
    expect(r.haptics.played, isNot(contains(FeedbackKind.buttonUp)));
    // the placeholder screen moved 4 rows (then hit the end): 4 detents
    expect(
      r.haptics.played.where((k) => k == FeedbackKind.detent),
      hasLength(4),
    );
    expect(r.haptics.played.last, FeedbackKind.endReached);
  });

  testWidgets('holding Next for 600 ms starts a scan, with the hold haptic', (
    tester,
  ) async {
    final r = await _rig(tester);
    final gesture = await tester.startGesture(r.at(90));
    await r.wait(uiHoldMs - 1);
    expect(r.events, [const WheelButtonDown(WheelButton.next)]);
    await r.wait(1);
    expect(r.events.last, const WheelHoldStart(WheelButton.next));
    expect(r.haptics.played.last, FeedbackKind.holdTookEffect);
    await gesture.up();
    await tester.pump();
    expect(r.events.last, const WheelHoldEnd(WheelButton.next));
  });

  testWidgets('a second finger on the wheel is ignored', (tester) async {
    final r = await _rig(tester);
    final one = await tester.startGesture(r.at(90), pointer: 1);
    final two = await tester.startGesture(r.at(270), pointer: 2);
    await two.up();
    await one.up();
    await tester.pump();
    expect(r.events.whereType<WheelPress>().toList(), [
      const WheelPress(WheelButton.next),
    ]);
  });

  testWidgets('a disabled wheel ignores touches', (tester) async {
    final r = await _rig(tester);
    r.bus.enabled.value = false;
    await tester.pump();
    await tester.tapAt(r.at(0));
    expect(r.events, isEmpty);
  });

  testWidgets('the wheel is one adjustable element with five named actions', (
    tester,
  ) async {
    final handle = tester.ensureSemantics();
    final r = await _rig(tester);
    final node = tester.getSemantics(find.byType(ClickWheel));
    final data = node.getSemanticsData();
    expect(data.label, 'Click wheel');
    expect(data.value, 'Music, 1 of 5');
    expect(data.increasedValue, 'Playlists, 2 of 5');
    expect(data.hasAction(SemanticsAction.increase), isTrue);
    expect(data.hasAction(SemanticsAction.decrease), isTrue);
    final labels = [
      for (final id in data.customSemanticsActionIds!)
        CustomSemanticsAction.getAction(id)!.label,
    ];
    expect(labels, ['Select', 'Menu', 'Previous track', 'Next track', 'Play']);

    void act(SemanticsAction a, [Object? args]) =>
        tester.binding.performSemanticsAction(
          SemanticsActionEvent(
            type: a,
            viewId: tester.view.viewId,
            nodeId: node.id,
            arguments: args,
          ),
        );
    act(SemanticsAction.increase);
    await tester.pump();
    expect(r.events.last, const WheelStep(1, 1));
    expect(
      tester.getSemantics(find.byType(ClickWheel)).getSemanticsData().value,
      'Playlists, 2 of 5',
    );
    final menuId = data.customSemanticsActionIds![1];
    act(SemanticsAction.customAction, menuId);
    expect(r.events.last, const WheelPress(WheelButton.menu));

    r.bus.semantics.value = const WheelSemantics(
      label: 'Volume',
      value: '62 percent',
      playing: true,
    );
    await tester.pump();
    final np = tester.getSemantics(find.byType(ClickWheel)).getSemanticsData();
    expect(np.label, 'Volume');
    expect(np.value, '62 percent');
    expect(
      CustomSemanticsAction.getAction(np.customSemanticsActionIds!.last)!.label,
      'Pause',
    );
    handle.dispose();
  });

  testWidgets('keyboard: arrows, Return, Escape, Space, left and right', (
    tester,
  ) async {
    final r = await _rig(tester);
    Future<void> key(LogicalKeyboardKey k) async {
      await tester.sendKeyEvent(k);
      await tester.pump();
    }

    await key(LogicalKeyboardKey.arrowDown);
    expect(r.events.last, const WheelStep(1, 1));
    await key(LogicalKeyboardKey.arrowUp);
    expect(r.events.last, const WheelStep(-1, 1));
    for (final (k, b) in [
      (LogicalKeyboardKey.enter, WheelButton.centre),
      (LogicalKeyboardKey.escape, WheelButton.menu),
      (LogicalKeyboardKey.space, WheelButton.playPause),
      (LogicalKeyboardKey.arrowLeft, WheelButton.previous),
      (LogicalKeyboardKey.arrowRight, WheelButton.next),
    ]) {
      await key(k);
      expect(r.events.last, WheelPress(b), reason: '$k');
    }
  });

  testWidgets('the system back gesture is Menu away from Home', (tester) async {
    final r = await _rig(tester);
    r.bus.atRoot.value = false;
    await tester.pump();
    final handled = await tester.binding.handlePopRoute();
    await tester.pump();
    expect(handled, isTrue);
    expect(r.events.last, const WheelPress(WheelButton.menu));
  });
}
