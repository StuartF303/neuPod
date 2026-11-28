// SPDX-License-Identifier: MPL-2.0

// The Licences screen is reachable from Settings with the wheel, and lists
// every bundled third-party asset.

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/features/licences/licences_data.dart';
import 'package:neupod/ui/screens/browse/home_screen.dart';
import 'package:neupod/ui/screens/browse/licences_screen.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';

import 'package:neupod/ui/style/neupod_style.dart';

import 'screens_harness.dart';

Finder _rowText(String text) =>
    find.descendant(of: find.byType(NeupodRow), matching: find.text(text));

Future<void> _press(WidgetTester tester, ShellRig rig, WheelButton b) async {
  rig.press(b);
  await settle(tester);
}

void main() {
  // A cached load from an earlier test's zone must not leak into the next.
  setUp(rootBundle.clear);
  for (final family in StyleFamily.values) {
    _navigation(family);
  }
  testWidgets('the packages row opens a lazy list of notices', (tester) async {
    final rig = await pumpShell(tester, home: (_) => const LicencesScreen());
    rig.step(2); // Montserrat, Lucide icons, packages
    await settle(tester);
    await _press(tester, rig, WheelButton.centre);
    await _loaded(tester);
    await settle(tester);
    expect(find.byType(LicenceTextScreen), findsOneWidget);
    expect(find.byType(ListView), findsOneWidget);
  });
}

void _navigation(StyleFamily family) {
  testWidgets(
    '$family: Home > Settings > Licences with the wheel lists Montserrat and '
    'Lucide; Select shows the text; Menu goes back',
    (tester) async {
      final rig = await pumpShell(
        tester,
        family: family,
        dark: family == StyleFamily.modern,
        home: (_) => const HomeScreen(),
      );
      rig.step(2); // Settings
      await settle(tester);
      await _press(tester, rig, WheelButton.centre);
      expect(_rowText('Licences'), findsOneWidget);
      expect(_rowText('Style'), findsOneWidget);

      rig.step(4); // Style, Mode, Custom look, Library, Licences: the last row
      await settle(tester);
      await _press(tester, rig, WheelButton.centre);
      expect(find.byType(LicencesScreen), findsOneWidget);
      expect(_rowText('Montserrat'), findsOneWidget);
      expect(_rowText('Lucide icons'), findsOneWidget);
      expect(_rowText(packagesLicenceTitle), findsOneWidget);
      expect(rig.container.read(shellProvider).pages, hasLength(3));

      // Every bundled asset has a row.
      for (final l in bundledLicences) {
        expect(_rowText(l.name), findsOneWidget);
      }

      // Select opens Montserrat's text.
      await _press(tester, rig, WheelButton.centre);
      await _loaded(tester);
      await settle(tester);
      expect(find.byType(LicenceTextScreen), findsOneWidget);
      expect(find.textContaining('Montserrat 9.000'), findsOneWidget);

      // The wheel scrolls it without leaving; Menu goes back to the list.
      rig.step(3);
      await settle(tester);
      expect(find.byType(LicenceTextScreen), findsOneWidget);
      await _press(tester, rig, WheelButton.menu);
      expect(find.byType(LicencesScreen), findsOneWidget);
      await _press(tester, rig, WheelButton.menu);
      expect(_rowText('Licences'), findsOneWidget);
    },
  );
}

/// Lets the asset load finish (real time), then builds what it produced.
Future<void> _loaded(WidgetTester tester) async {
  for (var i = 0; i < 3; i++) {
    await tester.runAsync(
      () => Future<void>.delayed(const Duration(milliseconds: 50)),
    );
    await settle(tester);
  }
}
