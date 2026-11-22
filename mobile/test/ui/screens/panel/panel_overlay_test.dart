// SPDX-License-Identifier: MPL-2.0

// The centred panel in the shell: Select or Menu dismiss it (nothing else
// does), a tap on OK does, a tap on the scrim does not; its text is the
// wheel's screen-reader text while it is up and the screen's comes back;
// onDismissed runs once; the shell is not at the root while it is up.

import 'dart:async';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/panel.dart';
import 'package:neupod/ui/screens/browse/home_screen.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';

import '../screens_harness.dart';

void main() {
  for (final button in [WheelButton.centre, WheelButton.menu]) {
    testWidgets('${button.name} dismisses the panel: onDismissed once, the '
        'future completes, the screen below does not act, its wheel text '
        'comes back', (tester) async {
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      final before = rig.bus.semantics.value;
      var dismissed = 0;
      var done = false;
      unawaited(
        rig.container
            .read(shellProvider.notifier)
            .showPanelRequest(
              PanelRequest.libraryUpdateStopped(onDismissed: () => dismissed++),
            )
            .then((_) => done = true),
      );
      await settle(tester);
      expect(find.byType(NeupodPanel), findsOneWidget);
      expect(find.text('Library update stopped'), findsOneWidget);
      expect(rig.bus.semantics.value.label, 'Library update stopped');
      expect(rig.bus.semantics.value.value, contains('Some songs'));
      expect(rig.bus.atRoot.value, isFalse);

      // Nothing else gets through.
      rig.step(2);
      rig.press(WheelButton.next);
      rig.press(WheelButton.playPause);
      await settle(tester);
      expect(find.byType(NeupodPanel), findsOneWidget);
      expect(rig.player.calls, isEmpty);

      rig.press(button);
      await settle(tester);
      expect(find.byType(NeupodPanel), findsNothing);
      expect(dismissed, 1);
      expect(done, isTrue);
      expect(rig.bus.atRoot.value, isTrue);
      expect(rig.bus.semantics.value, before);
      // The press that dismissed it did not open Music.
      expect(find.text('Music'), findsWidgets);
      expect(rig.container.read(shellProvider).pages.length, 1);
    });
  }

  testWidgets('a tap on OK dismisses it; a tap on the scrim does not', (
    tester,
  ) async {
    final rig = await pumpShell(tester, home: (_) => const HomeScreen());
    unawaited(
      rig.container
          .read(shellProvider.notifier)
          .showPanel(title: 'Card is full', message: 'Free some space.'),
    );
    await settle(tester);
    await tester.tapAt(const Offset(10, 10));
    await settle(tester);
    expect(find.byType(NeupodPanel), findsOneWidget);
    await tester.tap(find.text('OK'));
    await settle(tester);
    expect(find.byType(NeupodPanel), findsNothing);
  });

  testWidgets('a second panel replaces the first; the first one\'s future '
      'completes', (tester) async {
    final rig = await pumpShell(tester, home: (_) => const HomeScreen());
    final shell = rig.container.read(shellProvider.notifier);
    var firstDone = false;
    unawaited(
      shell
          .showPanel(title: 'First', message: 'a')
          .then((_) => firstDone = true),
    );
    await settle(tester);
    unawaited(shell.showPanel(title: 'Second', message: 'b'));
    await settle(tester);
    expect(find.text('First'), findsNothing);
    expect(find.text('Second'), findsOneWidget);
    expect(firstDone, isTrue);
    expect(rig.bus.semantics.value.label, 'Second');
  });
}
