// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/neupod_app.dart';

void main() {
  testWidgets('the app starts and shows the Home placeholder', (tester) async {
    await tester.pumpWidget(
      // Retry off in tests (decision 0006), so a failing provider fails at once.
      ProviderScope(retry: (_, _) => null, child: const NeuPodApp()),
    );

    expect(find.text('neuPod'), findsOneWidget);
    expect(find.text('No songs'), findsOneWidget);
  });
}
