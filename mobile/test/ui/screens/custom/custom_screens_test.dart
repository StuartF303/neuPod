// SPDX-License-Identifier: MPL-2.0

// Custom mode's settings rows, the looks list, a look, the name entry, the
// reset and delete panels, the board picker and the part editor, driven by the
// wheel bus as the click wheel drives them (AP-1, 3/3).

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/accessibility_providers.dart';
import 'package:neupod/app/custom_look_actions.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/style/board_list.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/custom_paint.dart';
import 'package:neupod/domain/style/look_edit.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/device/edge_pulse.dart';
import 'package:neupod/ui/screens/browse/settings_screen.dart';
import 'package:neupod/ui/screens/custom/board_screen.dart';
import 'package:neupod/ui/screens/custom/colour_wheel.dart';
import 'package:neupod/ui/screens/custom/look_screen.dart';
import 'package:neupod/ui/screens/custom/looks_screen.dart';
import 'package:neupod/ui/screens/custom/name_screen.dart';
import 'package:neupod/ui/screens/custom/part_picker_screen.dart';
import 'package:neupod/ui/screens/custom/reset_delete_screen.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';

import '../../../fakes/memory_settings_store.dart';
import '../screens_harness.dart';

final _defaultLight = defaultPaints(LookScreen.light, phoneIsLight: true);

/// A look at the default light finish, as New look makes one on a phone.
CustomLook _plain({String id = 'look-9', String name = 'Plain'}) => CustomLook(
  id: id,
  name: name,
  body: _defaultLight.body,
  wheel: _defaultLight.wheel,
  centre: _defaultLight.centre,
);

Future<ShellRig> _pump(
  WidgetTester tester,
  WidgetBuilder home, {
  CustomLooks? looks,
  double scale = 1.0,
  bool reduceMotion = false,
  bool reduceTransparency = false,
}) async {
  final store = MemorySettingsStore();
  final rig = await pumpShell(
    tester,
    home: home,
    scale: scale,
    reduceMotion: reduceMotion,
    overrides: [
      settingsStoreProvider.overrideWithValue(store),
      if (reduceTransparency)
        reduceTransparencyProvider.overrideWithValue(true),
    ],
    registry: (c) {
      if (looks != null) c.read(customLooksProvider.notifier).set(looks);
    },
  );
  return rig;
}

PartPaint _paint(ShellRig rig, String id, LookPart part) =>
    rig.container.read(customLooksProvider).byId(id)!.paintOf(part);

/// The status bar's title.
String _statusTitle(WidgetTester tester) => tester
    .widgetList<Text>(
      find.descendant(of: find.byType(StatusBar), matching: find.byType(Text)),
    )
    .map((t) => t.data!)
    .firstWhere((t) => const {'Body', 'Wheel', 'Centre'}.contains(t));

List<String> _titles(WidgetTester tester) => [
  for (final r in tester.widgetList<NeupodRow>(find.byType(NeupodRow))) r.title,
];

void main() {
  group('Settings', () {
    testWidgets('Mode cycles Light, Dark, Auto, Custom', (tester) async {
      final rig = await _pump(tester, (_) => const SettingsScreen());
      rig.step(1); // Mode
      final seen = <ModeSetting>[rig.container.read(styleChoiceProvider).mode];
      for (var i = 0; i < 4; i++) {
        rig.press(WheelButton.centre);
        await settle(tester);
        seen.add(rig.container.read(styleChoiceProvider).mode);
      }
      expect(seen, [
        ModeSetting.light,
        ModeSetting.dark,
        ModeSetting.auto,
        ModeSetting.custom,
        ModeSetting.light,
      ]);
    });

    testWidgets('the Custom look row is always shown, under Mode', (
      tester,
    ) async {
      await _pump(tester, (_) => const SettingsScreen());
      expect(_titles(tester), [
        'Style',
        'Mode',
        'Custom look',
        'Library',
        'Licences',
      ]);
      final row = tester
          .widgetList<NeupodRow>(find.byType(NeupodRow))
          .firstWhere((r) => r.title == 'Custom look');
      expect(row.value, 'Harbour');
      expect(row.chevron, isTrue);
    });

    testWidgets('Select on the row opens the list of looks', (tester) async {
      final rig = await _pump(tester, (_) => const SettingsScreen());
      rig.step(2);
      rig.press(WheelButton.centre);
      await settle(tester);
      expect(find.byType(LooksScreen), findsOneWidget);
    });
  });

  group('the looks list', () {
    testWidgets('a row a look, the tick on the look in use, then New look', (
      tester,
    ) async {
      await _pump(tester, (_) => const LooksScreen());
      final rows = tester.widgetList<NeupodRow>(find.byType(NeupodRow));
      expect(
        [for (final r in rows) r.title],
        ['Harbour', 'Sea glass', 'Clear case', 'New look'],
      );
      expect([for (final r in rows) r.tick], [true, false, false, false]);
      expect(find.bySemanticsLabel('Harbour, in use, 1 of 4'), findsOneWidget);
      expect(find.text('1 of 4'), findsOneWidget);
    });

    testWidgets('Select on a look opens it, makes it in use and Mode Custom', (
      tester,
    ) async {
      final rig = await _pump(tester, (_) => const LooksScreen());
      expect(rig.container.read(styleChoiceProvider).mode, ModeSetting.light);
      rig.step(1);
      rig.press(WheelButton.centre);
      await settle(tester);
      expect(rig.container.read(customLooksProvider).lookInUse, 'look-2');
      expect(rig.container.read(styleChoiceProvider).mode, ModeSetting.custom);
      expect(find.byType(LookDetailScreen), findsOneWidget);
    });

    testWidgets('resting on a look previews it after 250 ms; Menu clears it', (
      tester,
    ) async {
      final rig = await _pump(tester, (_) => const LooksScreen());
      rig.step(1);
      await tester.pump(const Duration(milliseconds: 200));
      expect(rig.container.read(lookPreviewProvider), isNull);
      await tester.pump(const Duration(milliseconds: 100));
      expect(rig.container.read(lookPreviewProvider)?.name, 'Sea glass');
      expect(rig.container.read(activeLookProvider)?.name, 'Sea glass');
      // The look in use did not change.
      expect(rig.container.read(customLooksProvider).lookInUse, 'look-1');
    });

    testWidgets('a step before the rest is over restarts the wait', (
      tester,
    ) async {
      final rig = await _pump(tester, (_) => const LooksScreen());
      rig.step(1);
      await tester.pump(const Duration(milliseconds: 200));
      rig.step(1);
      await tester.pump(const Duration(milliseconds: 200));
      expect(rig.container.read(lookPreviewProvider), isNull);
      await tester.pump(const Duration(milliseconds: 100));
      expect(rig.container.read(lookPreviewProvider)?.name, 'Clear case');
    });

    testWidgets('New look copies the look in use and opens it', (tester) async {
      final rig = await _pump(tester, (_) => const LooksScreen());
      rig.step(3);
      rig.press(WheelButton.centre);
      await settle(tester);
      final looks = rig.container.read(customLooksProvider);
      expect(looks.looks.length, 4);
      final made = looks.looks.last;
      expect(made.id, 'look-4');
      expect(made.name, 'Look 1');
      expect(made.body, looks.looks.first.body);
      expect(looks.lookInUse, 'look-4');
      expect(find.byType(LookDetailScreen), findsOneWidget);
    });

    testWidgets('at six New look is greyed with 6 of 6 and does nothing', (
      tester,
    ) async {
      var looks = CustomLooks.starters;
      for (var i = 4; i <= 6; i++) {
        looks = looks.added(_plain(id: 'look-$i', name: 'Look $i'));
      }
      final rig = await _pump(tester, (_) => const LooksScreen(), looks: looks);
      final row = tester
          .widgetList<NeupodRow>(find.byType(NeupodRow))
          .firstWhere((r) => r.title == 'New look');
      expect(row.disabledReason, '6 of 6');
      expect(find.text('6 of 6'), findsOneWidget);
      rig.step(6);
      rig.press(WheelButton.centre);
      await settle(tester);
      expect(rig.container.read(customLooksProvider).looks.length, 6);
      expect(find.byType(LookDetailScreen), findsNothing);
    });

    testWidgets('a look id is never reused after a delete', (tester) async {
      final rig = await _pump(tester, (_) => const LooksScreen());
      final actions = rig.container.read(customLookActionsProvider);
      expect(actions.newLook(phoneIsLight: true)!.id, 'look-4');
      actions.removeLook('look-4');
      expect(actions.newLook(phoneIsLight: true)!.id, 'look-5');
      // And the number is kept in the settings file.
      expect(rig.container.read(lookSequenceProvider), 5);
    });

    testWidgets('no scroll bar is drawn when the rows do not fit', (
      tester,
    ) async {
      var looks = CustomLooks.starters;
      for (var i = 4; i <= 6; i++) {
        looks = looks.added(_plain(id: 'look-$i', name: 'Look $i'));
      }
      final rig = await _pump(
        tester,
        (_) => const LooksScreen(),
        looks: looks,
        scale: 1.4,
      );
      // Seven rows at the largest tier overflow the room: the wheel moves
      // them, and no scroll bar is drawn.
      rig.step(6);
      await settle(tester);
      expect(find.byType(NeupodScrollBar), findsNothing);
      expect(find.text('New look'), findsOneWidget);
    });
  });

  group('a look', () {
    testWidgets('seven rows, Screen cycles in place', (tester) async {
      final rig = await _pump(
        tester,
        (_) => const LookDetailScreen(lookId: 'look-1'),
      );
      expect(_titles(tester), [
        'Body',
        'Wheel',
        'Centre',
        'Board',
        'Screen',
        'Name',
        'Reset or delete',
      ]);
      final rows = tester.widgetList<NeupodRow>(find.byType(NeupodRow));
      expect(rows.firstWhere((r) => r.title == 'Body').value, 'Solid');
      expect(
        rows.firstWhere((r) => r.title == 'Board').value,
        'Main board, top',
      );
      expect(find.text('1 of 7'), findsOneWidget);
      rig.step(4); // Screen
      final seen = <LookScreen>[];
      for (var i = 0; i < 3; i++) {
        rig.press(WheelButton.centre);
        await settle(tester);
        seen.add(
          rig.container.read(customLooksProvider).byId('look-1')!.screen,
        );
      }
      expect(seen, [LookScreen.light, LookScreen.dark, LookScreen.auto]);
      expect(find.byType(LookDetailScreen), findsOneWidget);
    });

    testWidgets('a see-through part says how clear', (tester) async {
      await _pump(tester, (_) => const LookDetailScreen(lookId: 'look-2'));
      final body = tester
          .widgetList<NeupodRow>(find.byType(NeupodRow))
          .firstWhere((r) => r.title == 'Body');
      expect(body.value, '55 % clear');
    });
  });

  group('the name', () {
    testWidgets('typed with the wheel, kept with Play', (tester) async {
      final rig = await _pump(
        tester,
        (_) => const NameScreen(lookId: 'look-1'),
      );
      expect(find.text('Harbour'), findsWidgets);
      for (var i = 0; i < 7; i++) {
        rig.press(WheelButton.menu); // delete one
      }
      rig.press(WheelButton.centre); // type 'a'
      rig.step(1);
      rig.press(WheelButton.centre); // 'b'
      rig.press(WheelButton.playPause);
      await settle(tester);
      expect(
        rig.container.read(customLooksProvider).byId('look-1')!.name,
        'ab',
      );
    });

    testWidgets('sixteen characters at most', (tester) async {
      final rig = await _pump(
        tester,
        (_) => const NameScreen(lookId: 'look-1'),
      );
      for (var i = 0; i < 20; i++) {
        rig.press(WheelButton.centre);
      }
      rig.press(WheelButton.playPause);
      await settle(tester);
      expect(
        rig.container.read(customLooksProvider).byId('look-1')!.name.length,
        16,
      );
    });

    testWidgets('an empty name is not accepted; Menu on empty keeps the old', (
      tester,
    ) async {
      final rig = await _pump(
        tester,
        (_) => const NameScreen(lookId: 'look-1'),
      );
      for (var i = 0; i < 7; i++) {
        rig.press(WheelButton.menu);
      }
      rig.press(WheelButton.playPause); // empty: refused
      rig.press(WheelButton.menu); // empty: leaves, nothing kept
      await settle(tester);
      expect(
        rig.container.read(customLooksProvider).byId('look-1')!.name,
        'Harbour',
      );
    });
  });

  group('reset and delete', () {
    testWidgets('Delete look asks first; Menu keeps the look', (tester) async {
      final rig = await _pump(
        tester,
        (_) => const ResetDeleteScreen(lookId: 'look-2'),
      );
      rig.step(1);
      rig.press(WheelButton.centre);
      await settle(tester);
      expect(
        rig.container.read(shellProvider).panel?.title,
        'Delete Sea glass?',
      );
      rig.press(WheelButton.menu);
      await settle(tester);
      expect(rig.container.read(shellProvider).panel, isNull);
      expect(rig.container.read(customLooksProvider).looks.length, 3);
    });

    testWidgets('Select on the panel deletes; the one above is in use', (
      tester,
    ) async {
      final rig = await _pump(
        tester,
        (_) => const ResetDeleteScreen(lookId: 'look-2'),
        looks: CustomLooks.starters.using('look-2'),
      );
      rig.step(1);
      rig.press(WheelButton.centre);
      await settle(tester);
      rig.press(WheelButton.centre);
      await settle(tester);
      final looks = rig.container.read(customLooksProvider);
      expect(looks.looks.map((l) => l.id), ['look-1', 'look-3']);
      expect(looks.lookInUse, 'look-1');
    });

    testWidgets('deleting the last look leaves Mode on Auto', (tester) async {
      final rig = await _pump(
        tester,
        (_) => const ResetDeleteScreen(lookId: 'look-9'),
        looks: CustomLooks(looks: [_plain()], lookInUse: 'look-9'),
      );
      rig.container
          .read(styleChoiceProvider.notifier)
          .set(const StyleChoice(mode: ModeSetting.custom));
      rig.step(1);
      rig.press(WheelButton.centre);
      await settle(tester);
      rig.press(WheelButton.centre);
      await settle(tester);
      expect(rig.container.read(customLooksProvider).looks, isEmpty);
      expect(rig.container.read(styleChoiceProvider).mode, ModeSetting.auto);
    });

    testWidgets('Reset to default asks, then sets the three parts solid', (
      tester,
    ) async {
      final rig = await _pump(
        tester,
        (_) => const ResetDeleteScreen(lookId: 'look-2'),
      );
      rig.press(WheelButton.centre);
      await settle(tester);
      expect(
        rig.container.read(shellProvider).panel?.title,
        'Reset Sea glass?',
      );
      rig.press(WheelButton.centre);
      await settle(tester);
      final look = rig.container.read(customLooksProvider).byId('look-2')!;
      expect(look.body.transparency, 0);
      expect(look.name, 'Sea glass');
      expect(look.board, 'main-top');
    });
  });

  group('the board picker', () {
    testWidgets('lists the boards; Select chooses', (tester) async {
      final rig = await _pump(
        tester,
        (_) => const BoardScreen(lookId: 'look-1'),
      );
      expect(_titles(tester), [for (final b in boardList) b.name]);
      expect(find.text('Placeholder picture'), findsOneWidget);
      rig.step(2);
      await tester.pump(const Duration(milliseconds: 300));
      expect(rig.container.read(lookPreviewProvider)?.board, 'copper');
      rig.press(WheelButton.centre);
      await settle(tester);
      expect(
        rig.container.read(customLooksProvider).byId('look-1')!.board,
        'copper',
      );
      expect(rig.container.read(lookPreviewProvider), isNull);
    });
  });

  group('the part editor', () {
    testWidgets('the wheel alone makes Harbour from the default', (
      tester,
    ) async {
      final plain = _plain();
      final rig = await _pump(
        tester,
        (_) => PartPickerScreen(lookId: plain.id),
        looks: CustomLooks(looks: [plain], lookInUse: plain.id),
      );
      // Hue 218 to 205: the first detent lands on the grid (215).
      rig.step(-3);
      rig.press(WheelButton.next);
      rig.step(14); // Saturation 4 to 60
      rig.press(WheelButton.next);
      rig.step(-11); // Brightness 88 to 44
      await settle(tester);
      final body = _paint(rig, plain.id, LookPart.body);
      // Harbour is 205, 62, 44: saturation 62 is between two 4 % detents.
      expect((body.hue, body.brightness, body.transparency), (205, 44, 0));
      expect((body.saturation - 62).abs() <= 2, isTrue);
      // The status bar says what is edited.
      expect(find.text('Body'), findsWidgets);
    });

    testWidgets('the centre steps the part: Body, Wheel, Centre, Body', (
      tester,
    ) async {
      final plain = _plain();
      final rig = await _pump(
        tester,
        (_) => PartPickerScreen(lookId: plain.id),
        looks: CustomLooks(looks: [plain], lookInUse: plain.id),
      );
      final titles = <String>[];
      for (var i = 0; i < 4; i++) {
        rig.press(WheelButton.centre);
        await settle(tester);
        titles.add(_statusTitle(tester));
      }
      expect(titles, ['Wheel', 'Centre', 'Body', 'Wheel']);
    });

    testWidgets('Previous and Next move the field and wrap', (tester) async {
      final plain = _plain();
      final rig = await _pump(
        tester,
        (_) => PartPickerScreen(lookId: plain.id),
        looks: CustomLooks(looks: [plain], lookInUse: plain.id),
      );
      // Previous from Hue is Transparency (wraps): a detent changes it.
      rig.press(WheelButton.previous);
      rig.step(2);
      await settle(tester);
      expect(_paint(rig, plain.id, LookPart.body).transparency, 10);
      expect(_paint(rig, plain.id, LookPart.body).hue, plain.body.hue);
      rig.press(WheelButton.next); // back to Hue
      rig.step(1);
      await settle(tester);
      expect(_paint(rig, plain.id, LookPart.body).hue, 220);
    });

    testWidgets('hue wraps; the ends give the end haptic', (tester) async {
      final plain = _plain().copyWith(
        body: const PartPaint(hue: 355, saturation: 100, brightness: 100),
      );
      final rig = await _pump(
        tester,
        (_) => PartPickerScreen(lookId: plain.id),
        looks: CustomLooks(looks: [plain], lookInUse: plain.id),
      );
      rig.step(1);
      await settle(tester);
      expect(_paint(rig, plain.id, LookPart.body).hue, 0);
      rig.haptics.played.clear();
      rig.press(WheelButton.next); // Saturation, already 100
      rig.step(1);
      expect(rig.haptics.played, contains(FeedbackKind.endReached));
      expect(_paint(rig, plain.id, LookPart.body).saturation, 100);
    });

    testWidgets('the wheel says the field, its value and the part', (
      tester,
    ) async {
      final plain = _plain();
      final rig = await _pump(
        tester,
        (_) => PartPickerScreen(lookId: plain.id),
        looks: CustomLooks(looks: [plain], lookInUse: plain.id),
      );
      await settle(tester);
      expect(rig.bus.semantics.value.label, 'Hue');
      expect(rig.bus.semantics.value.value, '218 degrees, body');
      rig.press(WheelButton.next);
      rig.press(WheelButton.centre); // the wheel part
      await settle(tester);
      expect(rig.bus.semantics.value.label, 'Saturation');
      expect(rig.bus.semantics.value.value, '2 percent, wheel');
    });

    testWidgets('Held at: the rule gave transparency back', (tester) async {
      // A see-through wheel in a mid colour over the board's frost: find one
      // the rule has to give transparency back for.
      PartPaint? wheel;
      var kept = 60;
      for (var b = 20; b <= 100 && wheel == null; b += 5) {
        for (var sat = 0; sat <= 100 && wheel == null; sat += 10) {
          for (var h = 0; h < 360 && wheel == null; h += 15) {
            final p = PartPaint(
              hue: h,
              saturation: sat,
              brightness: b,
              transparency: 60,
            );
            final r = paintPart(
              p,
              LookPart.wheel,
              grounds: boardById('main-top').grounds,
            );
            if (r.transparencyKept < 60) {
              wheel = p;
              kept = r.transparencyKept;
            }
          }
        }
      }
      expect(wheel, isNotNull, reason: 'the rule gives some back somewhere');
      final look = _plain().copyWith(wheel: wheel);
      await _pump(
        tester,
        (_) => PartPickerScreen(lookId: look.id, part: LookPart.wheel),
        looks: CustomLooks(looks: [look], lookInUse: look.id),
      );
      expect(
        find.text('Held at $kept % so MENU stays readable'),
        findsOneWidget,
      );
    });

    testWidgets('the note says the ink and the contrast; flat when it must', (
      tester,
    ) async {
      final harbour = CustomLooks.starters.looks.first;
      await _pump(
        tester,
        (_) => PartPickerScreen(lookId: harbour.id),
        looks: CustomLooks.starters,
      );
      expect(find.text('Legends: white ink, 6.3:1'), findsOneWidget);
    });

    testWidgets('Reduce Transparency greys the field and says why', (
      tester,
    ) async {
      final look = _plain().copyWith(
        body: _plain().body.copyWith(transparency: 40),
      );
      final rig = await _pump(
        tester,
        (_) => PartPickerScreen(lookId: look.id),
        looks: CustomLooks(looks: [look], lookInUse: look.id),
        reduceTransparency: true,
      );
      rig.press(WheelButton.previous); // Transparency
      await settle(tester);
      expect(find.text('Reduce Transparency is on'), findsOneWidget);
      rig.step(3);
      await settle(tester);
      // The look is not changed.
      expect(_paint(rig, look.id, LookPart.body).transparency, 40);
    });

    testWidgets('a part change pulses its edge, unless Reduce Motion', (
      tester,
    ) async {
      final plain = _plain();
      final looks = CustomLooks(looks: [plain], lookInUse: plain.id);
      final rig = await _pump(
        tester,
        (_) => PartPickerScreen(lookId: plain.id),
        looks: looks,
      );
      rig.press(WheelButton.centre);
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 200));
      final pulse = rig.container.read(edgePulseProvider);
      expect(pulse?.part, LookPart.wheel);
      expect(pulse!.strength, greaterThan(0.9));
      await tester.pump(const Duration(milliseconds: 300));
      expect(rig.container.read(edgePulseProvider), isNull);
    });

    testWidgets('Reduce Motion: no pulse', (tester) async {
      final plain = _plain();
      final rig = await _pump(
        tester,
        (_) => PartPickerScreen(lookId: plain.id),
        looks: CustomLooks(looks: [plain], lookInUse: plain.id),
        reduceMotion: true,
      );
      rig.press(WheelButton.centre);
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 200));
      expect(rig.container.read(edgePulseProvider), isNull);
    });

    testWidgets('touch: a tab, the colour wheel, a field', (tester) async {
      final plain = _plain();
      final rig = await _pump(
        tester,
        (_) => PartPickerScreen(lookId: plain.id),
        looks: CustomLooks(looks: [plain], lookInUse: plain.id),
      );
      await tester.tap(find.text('Wheel'));
      await settle(tester);
      expect(_statusTitle(tester), 'Wheel');
      // Three o'clock, half way out: hue 90, saturation 50.
      final rect = tester.getRect(find.byType(ColourWheel));
      await tester.tapAt(rect.center + Offset(rect.width / 4, 0));
      await settle(tester);
      final wheel = _paint(rig, plain.id, LookPart.wheel);
      expect(wheel.hue, 90);
      expect(wheel.saturation, 50);
      // Tapping Brightness focuses it: a detent changes it.
      await tester.tap(find.text('Brightness'));
      await settle(tester);
      rig.step(1);
      await settle(tester);
      expect(_paint(rig, plain.id, LookPart.wheel).brightness, 96);
    });

    testWidgets('the largest tier: no tabs, the focused field alone', (
      tester,
    ) async {
      final plain = _plain();
      await _pump(
        tester,
        (_) => PartPickerScreen(lookId: plain.id),
        looks: CustomLooks(looks: [plain], lookInUse: plain.id),
        scale: 1.4,
      );
      expect(find.text('Wheel'), findsNothing);
      expect(find.text('1 of 4'), findsOneWidget);
      expect(find.text('Hue'), findsOneWidget);
      expect(find.text('Saturation'), findsNothing);
      expect(tester.takeException(), isNull);
    });
  });
}
