// SPDX-License-Identifier: MPL-2.0

/// One entry of the play queue: what to play and what the lock screen shows.
///
/// [uri] is what the folder-access layer hands over (decision 0004): a
/// `content://` URI on Android, a `file://` URL under an accessible root on
/// iOS.
final class QueueItem {
  const QueueItem({
    required this.id,
    required this.uri,
    this.title,
    this.artist,
    this.album,
    this.artUri,
    this.duration,
  });

  /// The track's id in the library (the same track may be queued twice).
  final String id;
  final String uri;
  final String? title;
  final String? artist;
  final String? album;

  /// Local artwork (`file://` or `content://`), for the lock screen.
  final String? artUri;

  /// Known before the file is opened, if the library has it.
  final Duration? duration;

  @override
  bool operator ==(Object other) =>
      other is QueueItem &&
      other.id == id &&
      other.uri == uri &&
      other.title == title &&
      other.artist == artist &&
      other.album == album &&
      other.artUri == artUri &&
      other.duration == duration;

  @override
  int get hashCode =>
      Object.hash(id, uri, title, artist, album, artUri, duration);

  @override
  String toString() => 'QueueItem($id, $uri)';
}
