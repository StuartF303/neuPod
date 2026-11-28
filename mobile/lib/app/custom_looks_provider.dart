// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/style/custom_look.dart';

/// The key in the settings file that holds [CustomLooks].
const String customLooksKey = 'custom';

/// The saved looks and the one in use (design section 7). Seeded with the
/// three starters; replaced by what the settings file holds once it has been
/// read, and written back on every change. A missing or corrupt file keeps the
/// starters.
class CustomLooksNotifier extends Notifier<CustomLooks>
    with WidgetsBindingObserver {
  bool _changed = false;

  /// The first change after a quiet spell is written at once; changes that
  /// follow within [saveEvery] (a drag on the colour wheel makes hundreds)
  /// are kept live in memory and the latest is written when it passes, on
  /// [flush] (leaving the editor) and when the app is paused.
  static const Duration saveEvery = Duration(milliseconds: 250);
  Timer? _timer;
  bool _pending = false;

  /// Completes when the stored looks have been read (and applied, unless
  /// they were changed meanwhile). Never completes with an error.
  Future<void> loaded = Future<void>.value();

  @override
  CustomLooks build() {
    loaded = _load();
    final binding = WidgetsFlutterBinding.ensureInitialized();
    binding.addObserver(this);
    ref.onDispose(() {
      binding.removeObserver(this);
      try {
        flush();
      } on Object {
        // The container is going; nothing left to write to.
      }
      _timer?.cancel();
    });
    return CustomLooks.starters;
  }

  Future<void> _load() async {
    try {
      final stored = await ref.read(settingsStoreProvider).read();
      final looks = CustomLooks.tryFromJson(stored[customLooksKey]);
      if (looks != null && !_changed && ref.mounted) state = looks;
    } on Object catch (e) {
      debugPrint('neupod: custom looks not loaded: $e');
    }
  }

  /// Replaces the looks and saves them.
  void set(CustomLooks value) {
    _changed = true;
    state = value;
    if (_timer == null) {
      _write();
      _timer = Timer(saveEvery, _quiet);
    } else {
      _pending = true;
    }
  }

  void _write() {
    _pending = false;
    unawaited(
      ref.read(settingsStoreProvider).put(customLooksKey, state.toJson()),
    );
  }

  void _quiet() {
    _timer = null;
    if (_pending) {
      _write();
      _timer = Timer(saveEvery, _quiet);
    }
  }

  /// Writes any change still waiting.
  void flush() {
    if (_pending) _write();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state != AppLifecycleState.resumed) flush();
  }

  /// Changes the looks and saves them.
  void update(CustomLooks Function(CustomLooks) change) => set(change(state));
}

/// The saved looks (0 to 6) and the id of the one in use.
final customLooksProvider = NotifierProvider<CustomLooksNotifier, CustomLooks>(
  CustomLooksNotifier.new,
);
