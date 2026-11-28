// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/style/accessibility_port.dart';

/// The legend note the part editor shows while the Transparency field has the
/// focus and Reduce Transparency is on (design section 8). The field itself
/// is greyed; the stored look is not changed.
const String reduceTransparencyReason = 'Reduce Transparency is on';

/// The phone's accessibility settings that Flutter does not expose.
final accessibilityPortProvider = Provider<AccessibilityPort>(
  (ref) => const NoAccessibilitySettings(),
);

/// Whether the system's Reduce Transparency is on. While it is, the device
/// paints every part of a custom look solid and loads no board picture, and
/// the editor greys its Transparency field with [reduceTransparencyReason].
final reduceTransparencyProvider =
    NotifierProvider<ReduceTransparencyNotifier, bool>(
      ReduceTransparencyNotifier.new,
    );

/// Holds Reduce Transparency: off until the platform says otherwise.
class ReduceTransparencyNotifier extends Notifier<bool> {
  @override
  bool build() {
    final port = ref.watch(accessibilityPortProvider);
    var alive = true;
    final sub = port.reduceTransparencyChanges().listen((v) {
      if (alive) state = v;
    });
    ref.onDispose(() {
      alive = false;
      unawaited(sub.cancel());
    });
    unawaited(
      port.reduceTransparency().then((v) {
        if (alive) state = v;
      }, onError: (Object _) {}),
    );
    return false;
  }
}
