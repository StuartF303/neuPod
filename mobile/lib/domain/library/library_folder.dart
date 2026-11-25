// SPDX-License-Identifier: MPL-2.0

/// Where a library folder comes from.
enum FolderKind {
  /// iOS: the app's own Documents folder ("On My iPhone > neuPod" in Files,
  /// and the app's files in Finder). Always present; cannot be removed.
  appDocuments,

  /// A folder the user picked with the system picker.
  pickedFolder,
}

/// A folder the library is built from. The app only ever reads it.
final class LibraryFolder {
  const LibraryFolder({
    required this.id,
    required this.name,
    required this.kind,
    required this.available,
  });

  /// Stable id from the platform (a tree URI on Android, a bookmark id or
  /// `documents` on iOS). Tracks keep it as their source location.
  final String id;

  /// The folder's name as the system shows it.
  final String name;

  final FolderKind kind;

  /// False when access could not be restored: the grant was revoked, the
  /// folder was deleted, or its drive is unplugged.
  final bool available;

  /// Only picked folders can be removed; the Documents folder is permanent.
  bool get removable => kind == FolderKind.pickedFolder;

  @override
  bool operator ==(Object other) =>
      other is LibraryFolder &&
      other.id == id &&
      other.name == name &&
      other.kind == kind &&
      other.available == available;

  @override
  int get hashCode => Object.hash(id, name, kind, available);

  @override
  String toString() =>
      'LibraryFolder($id, $name, ${kind.name}, available: $available)';
}

/// A failure the folder screen can explain.
final class FolderAccessException implements Exception {
  const FolderAccessException(this.code, [this.message]);

  /// For example `busy` (the picker is already open) or `not-persistable`.
  final String code;
  final String? message;

  @override
  String toString() =>
      'FolderAccessException($code${message == null ? '' : ': $message'})';
}

/// Port: read-only access to the folders the library is built from
/// (decision 0004). Implemented in `data/` over the neupod_storage plugin.
abstract interface class FolderAccess {
  /// The folders the app holds access to, in the order they were added.
  Future<List<LibraryFolder>> folders();

  /// Shows the system folder picker. Returns the folder (or the one already
  /// held, if the user picked it again), or null if the user cancelled.
  Future<LibraryFolder?> pickFolder();

  /// Gives up access to a picked folder. Its files are never touched.
  Future<void> removeFolder(String id);
}
