// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/style/custom_look.dart';

/// The key in the settings file that holds [CustomLooks].
const String customLooksKey = 'custom';

/// The saved looks and the one in use (design section 7). Seeded with the
/// three starters; replaced by what the settings file holds once it has been
/// read, and written back on every change. A missing or corrupt file keeps the
/// starters.
class CustomLooksNotifier extends Notifier<CustomLooks> {
  bool _changed = false;

  /// Completes when the stored looks have been read (and applied, unless
  /// they were changed meanwhile). Never completes with an error.
  Future<void> loaded = Future<void>.value();

  @override
  CustomLooks build() {
    loaded = _load();
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
    unawaited(
      ref.read(settingsStoreProvider).put(customLooksKey, value.toJson()),
    );
  }

  /// Changes the looks and saves them.
  void update(CustomLooks Function(CustomLooks) change) => set(change(state));
}

/// The saved looks (0 to 6) and the id of the one in use.
final customLooksProvider = NotifierProvider<CustomLooksNotifier, CustomLooks>(
  CustomLooksNotifier.new,
);
