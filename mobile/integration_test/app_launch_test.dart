// SPDX-License-Identifier: MPL-2.0

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:neupod/main.dart' as app;

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // On iOS CI the tests run under `xcodebuild test` (ios/RunnerTests), so they start the moment the app launches,
  // and iOS turns accessibility on a few tens of milliseconds later. The binding then opens a SemanticsHandle; if that
  // lands inside a test, the test fails with "A SemanticsHandle was active at the end of the test". Wait for it
  // first (bounded, as a real device without VoiceOver never turns it on).
  setUpAll(() async {
    if (!Platform.isIOS) return;
    final deadline = DateTime.now().add(const Duration(seconds: 10));
    while (!binding.platformDispatcher.semanticsEnabled &&
        DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
  });

  testWidgets('the app launches on a device and shows the Home placeholder', (
    tester,
  ) async {
    app.main();
    await tester.pumpAndSettle();

    expect(find.text('neuPod'), findsOneWidget);
    expect(find.text('DELIBERATE FAILURE: not on screen'), findsOneWidget);
  });
}
