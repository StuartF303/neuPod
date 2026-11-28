// SPDX-License-Identifier: MPL-2.0

/// Where the app's small settings live: one JSON object, a key per setting.
/// Generic on purpose, so a later setting (Style, haptics, ...) uses the same
/// store. Implementations never throw: a store that cannot be read is an
/// empty one, and a write that fails is dropped.
abstract interface class SettingsStore {
  /// Everything stored, or an empty map when nothing is stored or what is
  /// there cannot be read.
  Future<Map<String, Object?>> read();

  /// Stores [value] (JSON-encodable) under [key], keeping every other key.
  /// Writes are applied in the order they were made.
  Future<void> put(String key, Object? value);
}
