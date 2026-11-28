// SPDX-License-Identifier: MPL-2.0

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:neupod/domain/settings/settings_store.dart';

/// A [SettingsStore] in one JSON file, `settings.json`, in the directory
/// [directory] returns (a `settings/` folder beside the backed-up stats
/// database, owner decision 20). Written atomically: the new contents go to a
/// temp file beside it, which is renamed over the old one, so a crash leaves
/// the old file or the new one, never half of either. A missing, unreadable or
/// corrupt file reads as empty; nothing here throws.
class FileSettingsStore implements SettingsStore {
  /// A store in [directory] (created on first write).
  FileSettingsStore(this.directory);

  /// The folder to keep `settings.json` in.
  final Future<String> Function() directory;

  /// Writes go one at a time, in order.
  Future<void> _queue = Future<void>.value();

  Future<File> _file() async => File('${await directory()}/settings.json');

  @override
  Future<Map<String, Object?>> read() async {
    try {
      return await _readFile(await _file());
    } on Object catch (e) {
      debugPrint('neupod: settings unreadable: $e');
      return {};
    }
  }

  static Future<Map<String, Object?>> _readFile(File file) async {
    if (!await file.exists()) return {};
    try {
      // Bytes, then utf8.decode: bad UTF-8 is a FormatException here, where
      // readAsString would raise a FileSystemException.
      final decoded = jsonDecode(utf8.decode(await file.readAsBytes()));
      if (decoded is Map) return Map<String, Object?>.from(decoded);
    } on Object {
      // Corrupt or unreadable: treated as empty; the next write replaces it.
    }
    return {};
  }

  @override
  Future<void> put(String key, Object? value) {
    final next = _queue.then((_) => _put(key, value));
    _queue = next;
    return next;
  }

  Future<void> _put(String key, Object? value) async {
    try {
      final file = await _file();
      await file.parent.create(recursive: true);
      final all = await _readFile(file);
      all[key] = value;
      final temp = File('${file.path}.tmp');
      await temp.writeAsString(jsonEncode(all), flush: true);
      // No fsync of the directory after the rename: if the phone dies before
      // the rename reaches the disk, the old file comes back, which is safe.
      await temp.rename(file.path);
    } on Object catch (e) {
      debugPrint('neupod: settings not saved: $e');
    }
  }
}
