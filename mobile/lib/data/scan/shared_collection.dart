// SPDX-License-Identifier: MPL-2.0

/// Android's shared music collection (MediaStore) beside picked folders:
/// where a picked folder is on the phone's storage, and whether a collection
/// item is inside one.
///
/// The same file can reach the library twice: through a folder the user
/// picked (its path relative to that folder, `Album/01 Song.flac`) and
/// through the collection (its path from the top of the storage volume,
/// `Music/Album/01 Song.flac`). The scanner lists it once, under the picked
/// folder, and leaves it out of the collection (owner decision 21, decision
/// 0012): a picked folder gets the folder's cover art, which the
/// collection cannot read (owner decision 8), and its paths are the ones the
/// user chose.
library;

/// The external storage provider's authority: the phone's storage and SD
/// cards, as the system folder picker offers them.
const externalStorageAuthority = 'com.android.externalstorage.documents';

/// Where a picked folder is: a MediaStore volume name (`external_primary`
/// for the phone's own storage, the lower-case volume id such as
/// `1a2b-3c4d` for an SD card) and the folder's path from the top of the
/// volume, without slashes at either end (`Music/Library`; empty for the top).
typedef VolumePath = ({String volume, String path});

/// The volume and path of the folder of the tree URI [treeUri]
/// (`content://com.android.externalstorage.documents/tree/primary%3AMusic`),
/// or null for any other provider, which the collection never lists.
VolumePath? pickedFolderLocation(String treeUri) {
  final u = Uri.tryParse(treeUri);
  if (u == null ||
      u.scheme != 'content' ||
      u.authority != externalStorageAuthority) {
    return null;
  }
  final seg = u.pathSegments;
  if (seg.length < 2 || seg[0] != 'tree') return null;
  final docId = seg[1];
  final colon = docId.indexOf(':');
  if (colon <= 0) return null;
  final id = docId.substring(0, colon);
  var path = docId.substring(colon + 1);
  while (path.startsWith('/')) {
    path = path.substring(1);
  }
  while (path.endsWith('/')) {
    path = path.substring(0, path.length - 1);
  }
  return (
    volume: id == 'primary' ? 'external_primary' : id.toLowerCase(),
    path: path,
  );
}

/// The MediaStore volume of a collection item's URI
/// (`content://media/external_primary/audio/media/42`), or null.
String? collectionItemVolume(String uri) {
  final u = Uri.tryParse(uri);
  if (u == null || u.scheme != 'content' || u.authority != 'media') {
    return null;
  }
  final seg = u.pathSegments;
  return seg.isEmpty ? null : seg[0].toLowerCase();
}

/// Whether the collection item at [relPath] (its path from the top of its
/// volume) with the URI [uri] is inside one of [folders]. Android's shared
/// storage ignores case in names, so paths are compared without case.
bool insidePickedFolder(
  String relPath,
  String uri,
  Iterable<VolumePath> folders,
) {
  final volume = collectionItemVolume(uri);
  if (volume == null) return false;
  final rel = relPath.toLowerCase();
  for (final f in folders) {
    if (f.volume != volume) continue;
    if (f.path.isEmpty || rel.startsWith('${f.path.toLowerCase()}/')) {
      return true;
    }
  }
  return false;
}
