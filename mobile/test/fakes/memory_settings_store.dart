// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/settings/settings_store.dart';

/// A [SettingsStore] in memory, for tests. [writes] counts the puts.
class MemorySettingsStore implements SettingsStore {
  /// A store holding [initial].
  MemorySettingsStore([Map<String, Object?>? initial]) : values = {...?initial};

  /// What is stored.
  final Map<String, Object?> values;

  /// Number of [put] calls.
  int writes = 0;

  @override
  Future<Map<String, Object?>> read() async => {...values};

  @override
  Future<void> put(String key, Object? value) async {
    writes++;
    values[key] = value;
  }
}
