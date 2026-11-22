// SPDX-License-Identifier: MPL-2.0

/// Where a track comes from. R1 has only local folders; R2 (NAS) and R3 (a
/// neuPod's card) add kinds, not a new model (decision 0005).
enum SourceKind { localFolder, nas, device }

/// A track's source: its kind and the source's own id (for a local folder,
/// the [LibraryFolder] id: a tree URI on Android, a bookmark id or
/// `documents` on iOS).
final class TrackSource {
  const TrackSource(this.kind, this.id);

  final SourceKind kind;
  final String id;

  @override
  bool operator ==(Object other) =>
      other is TrackSource && other.kind == kind && other.id == id;

  @override
  int get hashCode => Object.hash(kind, id);

  @override
  String toString() => 'TrackSource(${kind.name}, $id)';
}

/// Where a track is within its source: a `/`-separated path relative to the
/// source's root (stable, used for playlists), and a URI the audio engine can
/// open (`content://` or `file://` for a local folder).
final class TrackLocation {
  const TrackLocation({required this.relPath, required this.uri});

  final String relPath;
  final String uri;

  /// The file name without its folders.
  String get fileName {
    final slash = relPath.lastIndexOf('/');
    return slash < 0 ? relPath : relPath.substring(slash + 1);
  }

  @override
  bool operator ==(Object other) =>
      other is TrackLocation && other.relPath == relPath && other.uri == uri;

  @override
  int get hashCode => Object.hash(relPath, uri);
}

/// One song in the library. Empty strings and zeros mean "not tagged".
final class Track {
  const Track({
    required this.id,
    required this.source,
    required this.location,
    required this.codec,
    this.title = '',
    this.artist = '',
    this.album = '',
    this.trackNo = 0,
    this.durationMs = 0,
    this.unreadableReason,
  });

  /// Database row id; stable across rescans while the file stays put.
  final int id;
  final TrackSource source;
  final TrackLocation location;

  /// Lower-case container or codec name from the file: `flac`, `mp3`, ...
  final String codec;
  final String title;
  final String artist;
  final String album;
  final int trackNo;
  final int durationMs;

  /// Why the file's tags could not be read (shown greyed), or null.
  final String? unreadableReason;

  /// The title, or the file name without its extension when untagged.
  /// (The folder-convention fallback comes with the full scanner, phase 3.)
  String get displayTitle => displayTitleOf(title, location.relPath);

  /// [title], or the file name of [relPath] without its extension.
  static String displayTitleOf(String title, String relPath) {
    if (title.isNotEmpty) return title;
    final slash = relPath.lastIndexOf('/');
    final name = slash < 0 ? relPath : relPath.substring(slash + 1);
    final dot = name.lastIndexOf('.');
    return dot > 0 ? name.substring(0, dot) : name;
  }

  /// The second line of a song row: "artist · album", skipping what is
  /// missing.
  String get artistAndAlbum =>
      [artist, album].where((s) => s.isNotEmpty).join(' · ');

  @override
  String toString() => 'Track($id, $displayTitle, ${location.relPath})';
}
