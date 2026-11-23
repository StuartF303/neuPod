// SPDX-License-Identifier: MPL-2.0

/// Which picture is an album's or a track's art: the firmware's rules, ported
/// exactly (owner: firmware behaviour is the rule). Checked against the C
/// itself by `test/data/scan/art_vectors_test.dart` (vectors made by
/// `scripts/app/gen_art_vectors.py`).
///
/// - **Embedded picture** (`meta_art_offer`, `app/audio/meta.c`): every FLAC
///   PICTURE block and ID3v2 APIC/PIC frame is offered; the best is kept by
///   [artScore] (a JPEG over a PNG over anything else, plus one for a front
///   cover, type 3), the first on a tie. What a picture is comes from its
///   first bytes ([sniffArt]), never from the tag's mime string.
/// - **Sources, in order** (`art_sources`, `app/audio/art.c`): the embedded
///   picture when it is a JPEG of at least 4 bytes, then a cover file in the
///   track's own folder. A source that cannot be decoded gives way to the next
///   (`find_and_decode` in `app/runtime/art_service.c`).
/// - **Cover files** (`art_folder_rank`, `art_folder_pick`): `cover.jpg`, then
///   `folder.jpg`, then `front.jpg`, ASCII case folded (so `Folder.jpg` and
///   `COVER.JPG` count, `cover.jpeg` and `folder.png` do not); the first of the
///   best rank in the listing wins.
/// - **An album's art** is the art of its first song in album order (the
///   library's album header and Cover Flow ask by the album's first song,
///   `ui_library.c`); **a track's** (Now Playing, the lock screen) is its own.
library;

import 'dart:convert';
import 'dart:typed_data';

/// What an embedded picture is, from its first bytes (`art_kind_t`). The
/// [code] is what the database stores.
enum ArtKind {
  none(0),
  jpeg(1),
  png(2),
  other(3);

  const ArtKind(this.code);

  final int code;

  static ArtKind fromCode(int? code) => switch (code) {
    1 => jpeg,
    2 => png,
    3 => other,
    _ => none,
  };
}

/// `meta_art_sniff`: [first] holds the picture's first bytes (fewer than 4
/// means the picture is shorter or could not be read: [ArtKind.none]).
ArtKind sniffArt(List<int> first, int length) {
  if (length < 4 || first.length < 4) return ArtKind.none;
  if (first[0] == 0xFF && first[1] == 0xD8 && first[2] == 0xFF) {
    return ArtKind.jpeg;
  }
  if (first[0] == 0x89 &&
      first[1] == 0x50 &&
      first[2] == 0x4E &&
      first[3] == 0x47) {
    return ArtKind.png;
  }
  return ArtKind.other;
}

/// Where an embedded picture's bytes are in its file (`art_ref_t`). The bytes
/// are never copied into the database; the art cache reads them when it needs
/// them.
final class EmbeddedArt {
  const EmbeddedArt({
    required this.offset,
    required this.length,
    required this.kind,
    required this.type,
  });

  /// File offset of the first image byte.
  final int offset;

  /// Image bytes.
  final int length;
  final ArtKind kind;

  /// The ID3 / FLAC picture type (3 = front cover).
  final int type;

  @override
  bool operator ==(Object other) =>
      other is EmbeddedArt &&
      other.offset == offset &&
      other.length == length &&
      other.kind == kind &&
      other.type == type;

  @override
  int get hashCode => Object.hash(offset, length, kind, type);

  @override
  String toString() => 'EmbeddedArt($offset+$length ${kind.name} type $type)';
}

/// `art_score` (meta.c).
int artScore(EmbeddedArt? a) {
  if (a == null || a.kind == ArtKind.none) return 0;
  final k = switch (a.kind) {
    ArtKind.jpeg => 4,
    ArtKind.png => 2,
    _ => 1,
  };
  return k + (a.type == 3 ? 1 : 0);
}

/// `meta_art_offer`: [candidate] replaces [best] only when it scores higher.
EmbeddedArt? offerArt(EmbeddedArt? best, EmbeddedArt candidate) =>
    artScore(candidate) > artScore(best) ? candidate : best;

/// A source of a picture (`art_src_t`).
enum ArtSourceKind { embedded, folder }

/// `art_sources`: the sources to try, in order.
List<ArtSourceKind> artSources(
  EmbeddedArt? embedded, {
  required bool folderHas,
}) => [
  if (embedded != null && embedded.kind == ArtKind.jpeg && embedded.length >= 4)
    ArtSourceKind.embedded,
  if (folderHas) ArtSourceKind.folder,
];

/// `name_is`: [name] equals [lower] with ASCII `A`..`Z` folded, compared on
/// the name's UTF-8 bytes as the firmware does.
bool _nameIs(Uint8List name, String lower) {
  if (name.length != lower.length) return false;
  for (var i = 0; i < name.length; i++) {
    var c = name[i];
    if (c >= 0x41 && c <= 0x5A) c += 32;
    if (c != lower.codeUnitAt(i)) return false;
  }
  return true;
}

/// `art_folder_rank`: 1 for `cover.jpg`, 2 for `folder.jpg`, 3 for
/// `front.jpg` (ASCII case folded), 0 for any other name. Lower wins.
int artFolderRank(String name) {
  final b = utf8.encode(name);
  if (_nameIs(b, 'cover.jpg')) return 1;
  if (_nameIs(b, 'folder.jpg')) return 2;
  if (_nameIs(b, 'front.jpg')) return 3;
  return 0;
}

/// `art_folder_pick`: the best cover file among [names] (in listing order)
/// and its rank, or null when there is none. The first of the best rank wins.
(String, int)? artFolderPick(Iterable<String> names) {
  String? best;
  var bestRank = 0;
  for (final n in names) {
    final r = artFolderRank(n);
    if (r != 0 && (bestRank == 0 || r < bestRank)) {
      bestRank = r;
      best = n;
      if (r == 1) break; // cannot be beaten
    }
  }
  return best == null ? null : (best, bestRank);
}
