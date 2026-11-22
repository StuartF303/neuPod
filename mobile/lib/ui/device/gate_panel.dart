// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/ui/device/device_geometry.dart';
import 'package:neupod/ui/device/device_options.dart';
import 'package:neupod/ui/features/player_debug/player_debug_screen.dart';

/// The design gate's switches, for the owner to compare the candidates on
/// real screens: opened by a long press on the device's body. Temporary:
/// after the gate the chosen values become defaults and the user-facing ones
/// (Body, Haptics, Click sound) move to Settings.
class GatePanel extends ConsumerWidget {
  /// The panel; [onClose] hides it.
  const GatePanel({required this.onClose, super.key});

  /// Hides the panel.
  final VoidCallback onClose;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final o = ref.watch(deviceViewOptionsProvider);
    final n = ref.read(deviceViewOptionsProvider.notifier);
    Widget choice<T>(
      String title,
      T value,
      Map<T, String> labels,
      DeviceViewOptions Function(T) apply,
    ) {
      return Padding(
        padding: const EdgeInsets.symmetric(vertical: 6),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(title, style: const TextStyle(fontWeight: FontWeight.w600)),
            const SizedBox(height: 4),
            SegmentedButton<T>(
              showSelectedIcon: false,
              segments: [
                for (final e in labels.entries)
                  ButtonSegment<T>(value: e.key, label: Text(e.value)),
              ],
              selected: {value},
              onSelectionChanged: (s) => n.set(apply(s.first)),
            ),
          ],
        ),
      );
    }

    Widget toggle(
      String title,
      bool value,
      DeviceViewOptions Function(bool) a,
    ) => SwitchListTile(
      contentPadding: EdgeInsets.zero,
      title: Text(title),
      value: value,
      onChanged: (v) => n.set(a(v)),
    );

    return GestureDetector(
      onTap: onClose,
      child: ColoredBox(
        color: const Color(0x99000000),
        child: SafeArea(
          child: Center(
            child: GestureDetector(
              onTap: () {}, // taps inside the card do not close it
              child: Card(
                margin: const EdgeInsets.all(16),
                child: SingleChildScrollView(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Text(
                        'Design gate',
                        style: Theme.of(context).textTheme.titleLarge,
                      ),
                      choice<DeviceFit>('Fit', o.fit, const {
                        DeviceFit.fillsPhone: 'A: fills',
                        DeviceFit.fixedProportions: 'B: fixed',
                      }, (v) => o.copyWith(fit: v)),
                      choice<BodySetting>('Body', o.body, const {
                        BodySetting.automatic: 'Automatic',
                        BodySetting.silver: 'Silver',
                        BodySetting.black: 'Black',
                      }, (v) => o.copyWith(body: v)),
                      choice<DirectTouch>(
                        'Touching the screen',
                        o.directTouch,
                        const {
                          DirectTouch.wheelOnly: '1: wheel only',
                          DirectTouch.wheelAndScreen: '2: wheel + screen',
                        },
                        (v) => o.copyWith(directTouch: v),
                      ),
                      toggle(
                        'Phone status bar',
                        o.showStatusBar,
                        (v) => o.copyWith(showStatusBar: v),
                      ),
                      toggle(
                        'Wordmark on the body',
                        o.showWordmark,
                        (v) => o.copyWith(showWordmark: v),
                      ),
                      toggle(
                        'Drag the progress bar to seek',
                        o.dragProgressToSeek,
                        (v) => o.copyWith(dragProgressToSeek: v),
                      ),
                      toggle(
                        'iOS: left-edge swipe is Menu',
                        o.leftEdgeSwipeIsMenu,
                        (v) => o.copyWith(leftEdgeSwipeIsMenu: v),
                      ),
                      toggle(
                        'Haptics',
                        o.haptics,
                        (v) => o.copyWith(haptics: v),
                      ),
                      const SizedBox(height: 8),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.end,
                        children: [
                          // The player debug screen (debug builds only).
                          if (kDebugMode)
                            TextButton(
                              onPressed: () {
                                onClose();
                                Navigator.of(context)
                                    .pushNamed(PlayerDebugScreen.routeName);
                              },
                              child: const Text('Player (debug)'),
                            ),
                          TextButton(
                            onPressed: onClose,
                            child: const Text('Done'),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
