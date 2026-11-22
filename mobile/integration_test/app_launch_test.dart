// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:neupod/main.dart' as app;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('the app launches on a device and shows the Home placeholder', (
    tester,
  ) async {
    app.main();
    await tester.pumpAndSettle();

    expect(find.text('neuPod'), findsOneWidget);
    expect(find.text('No songs'), findsOneWidget);
  });
}
