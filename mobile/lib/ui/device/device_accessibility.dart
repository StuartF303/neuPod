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

/// The system's Reduce Transparency, as the platform reports it. Read it
/// through `reduceTransparencyProvider` (`app/accessibility_providers.dart`),
/// which a test can override with a value.
final systemReduceTransparencyProvider =
    NotifierProvider<ReduceTransparencyNotifier, bool?>(
      ReduceTransparencyNotifier.new,
    );

/// Holds Reduce Transparency: null until the platform has answered (the
/// device view loads no board picture before it knows), then the setting. A
/// failed platform call answers false, so see-through looks are never left
/// solid for want of an answer.
class ReduceTransparencyNotifier extends Notifier<bool?> {
  final Completer<void> _loaded = Completer<void>();

  /// Completes when the first answer (or failure) has arrived: `main` waits
  /// for it with the other launch reads, so the first frame is right.
  Future<void> get loaded => _loaded.future;

  @override
  bool? build() {
    final port = ref.watch(accessibilityPortProvider);
    var alive = true;
    void answer(bool v) {
      if (alive) state = v;
      if (!_loaded.isCompleted) _loaded.complete();
    }

    final sub = port.reduceTransparencyChanges().listen(answer);
    ref.onDispose(() {
      alive = false;
      unawaited(sub.cancel());
    });
    unawaited(
      port.reduceTransparency().then(
        answer,
        onError: (Object _) => answer(false),
      ),
    );
    return null;
  }
}
