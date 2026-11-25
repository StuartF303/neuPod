// SPDX-License-Identifier: MPL-2.0

/// Why a file's tags could not be read. Stored as [code] (never as text, so
/// the wording can change and be translated); the UI words it.
enum UnreadableReason {
  /// iCloud file not on the device (owner decision 7); never opened.
  notDownloaded(1),

  /// The file could not be opened. Transient: retried on the next scan.
  cannotOpen(2, transient: true),

  /// Reading failed part-way (I/O, a provider gone). Transient: retried.
  cannotRead(3, transient: true),

  /// Not the kind of file its name says.
  wrongFormat(4),

  /// The right kind of file, but damaged or cut short.
  damaged(5);

  const UnreadableReason(this.code, {this.transient = false});

  /// Stable code stored in the database.
  final int code;

  /// Whether a later scan should try the file again even if it is unchanged.
  final bool transient;

  /// The reason stored as [code], or null for none or an unknown code.
  static UnreadableReason? fromCode(int? code) {
    for (final r in values) {
      if (r.code == code) return r;
    }
    return null;
  }
}

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
  final UnreadableReason? unreadableReason;

  /// The title, or the file name when untagged (owner decision 15).
  String get displayTitle => displayTitleOf(title, location.relPath);

  /// [title], or the file name of [relPath], extension and all, as the
  /// device shows an untagged file (`index_file` in lib_build.c: `t->title[0]
  /// ? t->title : name`; owner decision 15).
  static String displayTitleOf(String title, String relPath) {
    if (title.isNotEmpty) return title;
    final slash = relPath.lastIndexOf('/');
    return slash < 0 ? relPath : relPath.substring(slash + 1);
  }

  /// The second line of a song row: "artist · album", skipping what is
  /// missing.
  String get artistAndAlbum =>
      [artist, album].where((s) => s.isNotEmpty).join(' · ');

  @override
  String toString() => 'Track($id, $displayTitle, ${location.relPath})';
}
