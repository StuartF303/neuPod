// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/library/track.dart';

/// Port: the library as the UI reads it. Implemented over the database in
/// `data/`.
abstract interface class LibraryRepository {
  /// Every song, in the firmware's sort order (decision 11): by the title's
  /// collation key, then by id. Emits again whenever the library changes.
  Stream<List<Track>> watchSongs();
}

/// Progress of one scan.
final class ScanProgress {
  const ScanProgress({
    this.filesFound = 0,
    this.filesRead = 0,
    this.done = false,
    this.error,
  });

  /// Audio files found so far by the walk.
  final int filesFound;

  /// New or changed files whose tags have been read so far.
  final int filesRead;

  final bool done;

  /// Set when the scan stopped early.
  final String? error;

  @override
  bool operator ==(Object other) =>
      other is ScanProgress &&
      other.filesFound == filesFound &&
      other.filesRead == filesRead &&
      other.done == done &&
      other.error == error;

  @override
  int get hashCode => Object.hash(filesFound, filesRead, done, error);

  @override
  String toString() =>
      'ScanProgress(found: $filesFound, read: $filesRead, done: $done'
      '${error == null ? '' : ', error: $error'})';
}

/// Port: builds the library from the folders. Only new or changed files
/// (by size and modification time) are read; files that have gone are
/// removed; the music files themselves are never written.
abstract interface class LibraryScanner {
  /// Scans every available folder. Cancelling the subscription stops it;
  /// what was written so far stays.
  Stream<ScanProgress> scan();
}
