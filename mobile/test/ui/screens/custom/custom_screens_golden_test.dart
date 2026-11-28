// SPDX-License-Identifier: MPL-2.0

// Goldens of Custom mode's screens in Classic Light and Modern Dark at the
// standard tier, and the two lists and the body editor at the larger tiers
// in Classic Light. Compare with the design's settings-mode-rows.png,
// looks-list.png, look.png, board-picker.png, editor-body.png,
// editor-wheel.png, editor-centre.png and editor-largest-text.png.
//
// macOS only: see ../../components/flutter_test_config.dart.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/screens/browse/settings_screen.dart';
import 'package:neupod/ui/screens/custom/board_screen.dart';
import 'package:neupod/ui/screens/custom/look_screen.dart';
import 'package:neupod/ui/screens/custom/looks_screen.dart';
import 'package:neupod/ui/screens/custom/part_picker_screen.dart';
import 'package:neupod/ui/screens/custom/reset_delete_screen.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/style/neupod_style.dart';

import '../../../fakes/memory_settings_store.dart';
import '../../components/golden_harness.dart' show withShadows;
import '../screens_harness.dart';

const _styles = {
  'classic_light': (StyleFamily.classic, false),
  'modern_dark': (StyleFamily.modern, true),
};

void _each(
  String name,
  WidgetBuilder home, {
  List<String> tiers = const ['standard'],
  Future<void> Function(ShellRig rig)? act,
}) {
  for (final style in _styles.keys) {
    for (final tier in tiers) {
      // The larger tiers are Classic Light only.
      if (tier != 'standard' && style != 'classic_light') continue;
      testWidgets('$name, $style, $tier', (tester) async {
        final (family, dark) = _styles[style]!;
        await withShadows(() async {
          final rig = await pumpShell(
            tester,
            home: home,
            family: family,
            dark: dark,
            scale: tierScales[tier]!,
            status: mockStatus,
            overrides: [
              settingsStoreProvider.overrideWithValue(MemorySettingsStore()),
            ],
            registry: (c) {
              c
                  .read(styleChoiceProvider.notifier)
                  .set(StyleChoice(family: family, mode: ModeSetting.custom));
            },
          );
          if (act != null) {
            await act(rig);
            await settle(tester);
          }
          await expectLater(
            find.byKey(const ValueKey('device')),
            matchesGoldenFile('goldens/$name.$style.$tier.png'),
          );
        });
      });
    }
  }
}

void main() {
  const all = ['standard', 'large', 'largest'];

  _each('settings_custom', (_) => const SettingsScreen());
  _each(
    'custom_looks',
    (_) => const LooksScreen(),
    tiers: const ['standard', 'largest'],
  );
  _each(
    'custom_look',
    (_) => const LookDetailScreen(lookId: 'look-1'),
    tiers: const ['standard', 'largest'],
  );
  _each(
    'custom_board',
    (_) => const BoardScreen(lookId: 'look-1'),
    act: (rig) async => rig.step(1),
  );
  _each(
    'custom_reset_delete',
    (_) => const ResetDeleteScreen(lookId: 'look-1'),
    act: (rig) async {
      rig.step(1);
      rig.press(WheelButton.centre);
    },
  );
  _each(
    'custom_part_body',
    (_) => const PartPickerScreen(lookId: 'look-1'),
    tiers: all,
  );
  _each(
    'custom_part_wheel_clear',
    (_) => const PartPickerScreen(lookId: 'look-3', part: LookPart.wheel),
  );
  _each(
    'custom_part_centre',
    (_) => const PartPickerScreen(lookId: 'look-1', part: LookPart.centre),
  );
}
