// SPDX-License-Identifier: MPL-2.0

// The firmware marks a row that opens a screen with a chevron
// (app/ui/ui_settings.c: about_get sets it on Licences, settings_get on the
// groups that open a list) and leaves a row that changes a value in place
// (Click, Gapless, Shuffle ...: a detail, no chevron) without. Settings here
// follows the same rule, row by row.

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/screens/browse/settings_screen.dart';
import 'package:neupod/ui/style/neupod_style.dart';

import 'screens_harness.dart';

void main() {
  for (final family in StyleFamily.values) {
    testWidgets('$family: only the rows that open a screen have a chevron', (
      tester,
    ) async {
      await pumpShell(
        tester,
        family: family,
        home: (_) => const SettingsScreen(),
      );
      final rows = tester.widgetList<NeupodRow>(find.byType(NeupodRow));
      final chevrons = {for (final r in rows) r.title: r.chevron};
      expect(chevrons, {
        'Style': false, // changes in place
        'Mode': false, // changes in place
        'Custom look': true, // opens the list of looks
        'Library': true, // opens Library
        'Licences': true, // opens Licences
      });
    });
  }
}
