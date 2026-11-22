// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/neupod_app.dart';
import 'package:neupod/ui/device/device_view.dart';

void main() {
  testWidgets('the app starts as the device, with the placeholder screen', (
    tester,
  ) async {
    await tester.pumpWidget(
      // Retry off in tests (decision 0006), so a failing provider fails at once.
      ProviderScope(retry: (_, _) => null, child: const NeuPodApp()),
    );

    expect(find.byType(DeviceView), findsOneWidget);
    expect(find.text('Music'), findsOneWidget);
    expect(find.text('No songs'), findsOneWidget);
  });
}
