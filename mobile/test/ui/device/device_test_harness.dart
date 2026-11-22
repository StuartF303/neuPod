// SPDX-License-Identifier: MPL-2.0

// Shared set-up for the device view tests: phone sizes from the design
// (render.py's PHONES), a manual clock, and a recording haptics port.

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_riverpod/misc.dart' show Override;
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/device_view.dart';
import 'package:neupod/ui/device/placeholder_screen.dart';
import 'package:neupod/ui/device/wheel_bus.dart';

/// A phone of the design: width, height, safe top, safe bottom.
typedef Phone = ({String name, double w, double h, double top, double bottom});

/// The three phones the design draws (render.py, PHONES).
const List<Phone> designPhones = [
  (name: '375x667', w: 375, h: 667, top: 20, bottom: 0),
  (name: '393x852', w: 393, h: 852, top: 59, bottom: 34),
  (name: '440x956', w: 440, h: 956, top: 62, bottom: 34),
];

/// A clock the test moves.
class TestClock {
  /// Milliseconds.
  int ms = 1000;
}

/// Records what the device view asked the platform to play.
class RecordingHaptics implements HapticsPort {
  /// What was played, in order.
  final List<FeedbackKind> played = [];

  /// How often prepare() was called.
  int prepared = 0;

  @override
  void prepare() => prepared++;

  @override
  void play(FeedbackKind kind) => played.add(kind);
}

/// Sets the test view to [phone] at 1 physical pixel per point.
void usePhone(WidgetTester tester, Phone phone) {
  tester.view.devicePixelRatio = 1;
  tester.view.physicalSize = Size(phone.w, phone.h);
  final pad = FakeViewPadding(top: phone.top, bottom: phone.bottom);
  tester.view.padding = pad;
  tester.view.viewPadding = pad;
  addTearDown(tester.view.reset);
}

/// Pumps the app's device view with a placeholder screen (or [screen]) and
/// the given overrides; returns the container.
Future<ProviderContainer> pumpDevice(
  WidgetTester tester, {
  Phone? phone,
  Widget? screen,
  TestClock? clock,
  RecordingHaptics? haptics,
  Brightness brightness = Brightness.light,
  List<Override> overrides = const [],
}) async {
  usePhone(tester, phone ?? designPhones[1]);
  final container = ProviderContainer.test(
    retry: (_, _) => null,
    overrides: [
      if (clock != null) wheelClockProvider.overrideWithValue(() => clock.ms),
      if (clock != null)
        feedbackClockProvider.overrideWithValue(() => clock.ms * 1000),
      if (haptics != null) hapticsPortProvider.overrideWithValue(haptics),
      ...overrides,
    ],
  );
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        debugShowCheckedModeBanner: false,
        home: DeviceView(
          screen: screen ?? const PlaceholderScreen(),
          brightness: brightness,
        ),
      ),
    ),
  );
  await tester.pump();
  return container;
}
