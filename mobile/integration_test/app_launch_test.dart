// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:neupod/main.dart' as app;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('the app launches on a device as the device view', (
    tester,
  ) async {
    app.main();
    await tester.pumpAndSettle();

    // The device view's placeholder screen (the title and the wordmark both
    // read neuPod, so look for a Home item instead).
    expect(find.text('Music'), findsOneWidget);
    expect(find.text('No songs'), findsOneWidget);
  });
}
