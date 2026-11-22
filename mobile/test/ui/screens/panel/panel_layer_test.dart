// SPDX-License-Identifier: MPL-2.0

// The centred panel layer: Select or Menu dismisses it, a screen below does
// not act on the press that dismisses it, OK can be tapped, the wheel's
// screen-reader text is the panel's while it is up.

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/panel.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/common/screen_frame.dart';
import 'package:neupod/ui/screens/panel/panel_layer.dart';

class _Below extends ConsumerStatefulWidget {
  const _Below(this.log);
  final List<String> log;

  @override
  ConsumerState<_Below> createState() => _BelowState();
}

class _BelowState extends ConsumerState<_Below> {
  @override
  void initState() {
    super.initState();
    ref.read(wheelBusProvider).events.listen((e) {
      // A well-behaved screen under a panel.
      if (ref.read(panelProvider) != null) return;
      if (e is WheelPress) widget.log.add('acted on ${e.button.name}');
    });
  }

  @override
  Widget build(BuildContext context) => const Text('below');
}

Future<ProviderContainer> _pump(WidgetTester tester, List<String> log) async {
  tester.view.physicalSize = const Size(600, 450);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final container = ProviderContainer.test();
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: Directionality(
        textDirection: TextDirection.ltr,
        child: MediaQuery(
          data: const MediaQueryData(size: Size(600, 450)),
          child: ScreenFrame(child: PanelLayer(child: _Below(log))),
        ),
      ),
    ),
  );
  return container;
}

void main() {
  for (final button in [WheelButton.centre, WheelButton.menu]) {
    testWidgets('${button.name} dismisses the panel and the screen below '
        'does not act on that press', (tester) async {
      final log = <String>[];
      final c = await _pump(tester, log);
      var dismissed = 0;
      c
          .read(panelProvider.notifier)
          .show(
            PanelRequest.libraryUpdateStopped(onDismissed: () => dismissed++),
          );
      await tester.pump();
      expect(find.byType(NeupodPanel), findsOneWidget);
      expect(find.text('Library update stopped'), findsOneWidget);
      expect(
        c.read(wheelBusProvider).semantics.value.label,
        'Library update stopped',
      );

      c.read(wheelBusProvider).press(button);
      await tester.pump(); // the microtask clears the request
      await tester.pump();
      expect(find.byType(NeupodPanel), findsNothing);
      expect(dismissed, 1);
      expect(log, isEmpty);
      expect(c.read(wheelBusProvider).semantics.value.label, 'Click wheel');

      c.read(wheelBusProvider).press(button);
      expect(log, ['acted on ${button.name}'], reason: 'the panel is gone');
    });
  }

  testWidgets('other buttons and the wheel do not dismiss it; OK can be '
      'tapped', (tester) async {
    final c = await _pump(tester, []);
    c
        .read(panelProvider.notifier)
        .show(const PanelRequest(title: 'Card is full', message: 'Free some'));
    await tester.pump();
    final bus = c.read(wheelBusProvider);
    bus.press(WheelButton.playPause);
    bus.emit(const WheelStep(3, 1));
    await tester.pump();
    expect(find.byType(NeupodPanel), findsOneWidget);

    await tester.tap(find.text('OK'));
    await tester.pump();
    expect(find.byType(NeupodPanel), findsNothing);
  });

  testWidgets('a tap on the scrim does nothing', (tester) async {
    final c = await _pump(tester, []);
    c
        .read(panelProvider.notifier)
        .show(const PanelRequest(title: 'Lost', message: 'Something'));
    await tester.pump();
    await tester.tapAt(const Offset(10, 10));
    await tester.pump();
    expect(find.byType(NeupodPanel), findsOneWidget);
  });
}
