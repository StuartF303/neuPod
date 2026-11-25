// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/ui/device/device_options.dart';
import 'package:neupod/ui/features/player_debug/player_debug_screen.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_gate.dart';

/// The design gate's switches, for the owner to compare the remaining
/// candidates on real screens: opened by a long press on the device's body.
/// Temporary: the touch and seek candidates are settled (owner decision 18);
/// the user-facing switches (Haptics, Click sound) move to Settings.
class GatePanel extends ConsumerWidget {
  /// The panel; [onClose] hides it.
  const GatePanel({required this.onClose, super.key});

  /// Hides the panel.
  final VoidCallback onClose;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final o = ref.watch(deviceViewOptionsProvider);
    final n = ref.read(deviceViewOptionsProvider.notifier);
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
                      const NowPlayingGateSection(),
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
