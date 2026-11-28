// SPDX-License-Identifier: MPL-2.0

import 'dart:convert';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/style/neupod_style.dart';

import '../fakes/memory_settings_store.dart';

ProviderContainer _container(MemorySettingsStore store) =>
    ProviderContainer.test(
      overrides: [settingsStoreProvider.overrideWithValue(store)],
    );

void main() {
  group('customLooksProvider', () {
    test('starts with the three starters', () {
      final c = _container(MemorySettingsStore());
      expect(c.read(customLooksProvider), CustomLooks.starters);
    });

    test('an empty store keeps the starters after loading', () async {
      final c = _container(MemorySettingsStore());
      await c.read(customLooksProvider.notifier).loaded;
      expect(c.read(customLooksProvider), CustomLooks.starters);
    });

    test('loads what was saved', () async {
      final saved = CustomLooks.starters.copyWith(
        looks: [CustomLooks.starters.looks[1].copyWith(name: 'Mine')],
        lookInUse: () => 'look-2',
      );
      final store = MemorySettingsStore({
        customLooksKey: jsonDecode(jsonEncode(saved.toJson())),
      });
      final c = _container(store);
      c.read(customLooksProvider);
      await c.read(customLooksProvider.notifier).loaded;
      expect(c.read(customLooksProvider), saved);
    });

    test('every change is written, and a new session reads it back', () async {
      final store = MemorySettingsStore();
      final c = _container(store);
      final notifier = c.read(customLooksProvider.notifier);
      notifier.update(
        (l) => l.copyWith(
          looks: [
            for (final look in l.looks)
              look.id == 'look-1'
                  ? look.copyWith(body: look.body.copyWith(hue: 10))
                  : look,
          ],
        ),
      );
      expect(store.writes, 1);

      final again = _container(store);
      again.read(customLooksProvider);
      await again.read(customLooksProvider.notifier).loaded;
      expect(again.read(customLooksProvider).looks.first.body.hue, 10);
    });

    test('a corrupt entry falls back to the starters', () async {
      for (final bad in <Object?>[
        'junk',
        42,
        {'looks': 'x'},
        null,
      ]) {
        final c = _container(MemorySettingsStore({customLooksKey: bad}));
        c.read(customLooksProvider);
        await c.read(customLooksProvider.notifier).loaded;
        expect(
          c.read(customLooksProvider),
          CustomLooks.starters,
          reason: '$bad',
        );
      }
    });

    test('deleting every look is remembered, not restored', () async {
      final store = MemorySettingsStore();
      _container(store)
          .read(customLooksProvider.notifier)
          .set(const CustomLooks());
      final c = _container(store);
      c.read(customLooksProvider);
      await c.read(customLooksProvider.notifier).loaded;
      expect(c.read(customLooksProvider).looks, isEmpty);
    });

    test('a change made before the load finishes is not overwritten', () async {
      final store = MemorySettingsStore({
        customLooksKey: CustomLooks.starters.toJson(),
      });
      final c = _container(store);
      final notifier = c.read(customLooksProvider.notifier);
      notifier.set(const CustomLooks());
      await notifier.loaded;
      expect(c.read(customLooksProvider).looks, isEmpty);
    });
  });

  group('mode', () {
    test('defaults to Auto, loads and saves the stored mode', () async {
      final store = MemorySettingsStore({modeSettingKey: 'custom'});
      final c = _container(store);
      expect(c.read(styleChoiceProvider).mode, ModeSetting.auto);
      await c.read(styleChoiceProvider.notifier).loaded;
      expect(c.read(styleChoiceProvider).mode, ModeSetting.custom);

      c
          .read(styleChoiceProvider.notifier)
          .set(const StyleChoice(mode: ModeSetting.dark));
      expect(store.values[modeSettingKey], 'dark');
    });

    test('Style alone does not write the mode', () {
      final store = MemorySettingsStore();
      final c = _container(store);
      c
          .read(styleChoiceProvider.notifier)
          .set(const StyleChoice(family: StyleFamily.modern));
      expect(store.writes, 0);
    });
  });

  group('the mode in force', () {
    const harbourLight = LookScreen.light;
    final look = CustomLooks.starters.looks.first;

    test('Light, Dark and Auto are as before', () {
      expect(
        effectiveMode(
          const StyleChoice(mode: ModeSetting.light),
          Brightness.dark,
        ),
        StyleMode.light,
      );
      expect(
        effectiveMode(
          const StyleChoice(mode: ModeSetting.dark),
          Brightness.light,
        ),
        StyleMode.dark,
      );
      expect(
        effectiveMode(const StyleChoice(), Brightness.dark),
        StyleMode.dark,
      );
      expect(
        effectiveMode(const StyleChoice(), Brightness.light),
        StyleMode.light,
      );
    });

    test('Light and Dark ignore a look', () {
      expect(
        effectiveMode(
          const StyleChoice(mode: ModeSetting.light),
          Brightness.dark,
          look: look.copyWith(screen: LookScreen.dark),
        ),
        StyleMode.light,
      );
    });

    test('Custom takes the look\'s screen; Auto follows the phone', () {
      const choice = StyleChoice(mode: ModeSetting.custom);
      expect(
        effectiveMode(
          choice,
          Brightness.dark,
          look: look.copyWith(screen: harbourLight),
        ),
        StyleMode.light,
      );
      expect(
        effectiveMode(
          choice,
          Brightness.light,
          look: look.copyWith(screen: LookScreen.dark),
        ),
        StyleMode.dark,
      );
      expect(
        effectiveMode(choice, Brightness.dark, look: look),
        StyleMode.dark,
      );
      expect(
        effectiveMode(choice, Brightness.light, look: look),
        StyleMode.light,
      );
    });

    test('Custom with no look falls back to Auto', () {
      const choice = StyleChoice(mode: ModeSetting.custom);
      expect(effectiveMode(choice, Brightness.dark), StyleMode.dark);
      expect(effectiveMode(choice, Brightness.light), StyleMode.light);
      expect(pinnedMode(choice, null), isNull);
    });

    test('activeLookProvider is the look in use only while Mode is Custom', () {
      final c = _container(MemorySettingsStore());
      final notifier = c.read(styleChoiceProvider.notifier);
      expect(c.read(activeLookProvider), isNull);
      notifier.set(const StyleChoice(mode: ModeSetting.custom));
      expect(c.read(activeLookProvider)?.name, 'Harbour');
      c
          .read(customLooksProvider.notifier)
          .update((l) => l.copyWith(lookInUse: () => null));
      expect(c.read(activeLookProvider), isNull, reason: 'no look in use');
      notifier.set(const StyleChoice());
      expect(c.read(activeLookProvider), isNull);
    });
  });
}
