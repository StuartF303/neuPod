// SPDX-License-Identifier: MPL-2.0

// Album art against the firmware itself (owner: firmware behaviour is the
// rule). test/fixtures/art/ holds small FLAC and MP3 files with pictures,
// cover files and expected.tsv, made by scripts/app/gen_art_vectors.py from
// the firmware's own code compiled for the host:
//
// - tags: which embedded picture `flac_parse` / `mp3_parse` keep
//   (`meta_art_offer`: JPEG over PNG over the rest, a front cover first, the
//   first on a tie; the kind from the bytes, not the mime string; ID3 frames
//   that are compressed or unsynchronised, and descriptions past 160 bytes,
//   are skipped), whether `art_decode` takes it, and `art_sources`' order;
// - image: whether `art_decode` takes a cover file (baseline JPEG only;
//   progressive, over 5120 px, a PNG named .jpg and a cut file are refused);
// - rank and pick: `art_folder_rank` and `art_folder_pick` (cover.jpg, then
//   folder.jpg, then front.jpg, ASCII case folded; the first of a rank wins).

import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/art/art_cache.dart';
import 'package:neupod/data/art/firmware_jpeg.dart';
import 'package:neupod/data/scan/tag_reader.dart';
import 'package:neupod/domain/library/art_rules.dart';
import 'package:neupod/domain/library_view/library_view.dart';

const _dir = 'test/fixtures/art';

/// What the app makes of a picture, in `art_err_t` words: the firmware's
/// header check, then the app's decoder.
String _decode(Uint8List bytes) {
  final check = firmwareJpegCheck(bytes);
  return switch (check) {
    FirmwareJpeg.ok => decodeArt(bytes) == null ? 'format' : 'ok',
    FirmwareJpeg.format => 'format',
    FirmwareJpeg.unsupported => 'unsupported',
    FirmwareJpeg.tooBig => 'too_big',
  };
}

void main() {
  final rows = [
    for (final l in File('$_dir/expected.tsv').readAsLinesSync())
      if (l.isNotEmpty && !l.startsWith('#')) l.split('\t'),
  ];
  List<List<String>> of(String mode) => [
    for (final r in rows)
      if (r.first == mode) r.sublist(1),
  ];

  test('the vectors are there', () {
    expect(of('tags'), hasLength(20));
    expect(of('image'), hasLength(28));
    expect(of('rank'), hasLength(21));
    expect(of('pick'), hasLength(8));
  });

  group('embedded picture', () {
    for (final [name, offset, length, kind, type, decode, sources] in of(
      'tags',
    )) {
      test(name, () {
        final bytes = File('$_dir/$name').readAsBytesSync();
        final ext = name.substring(name.lastIndexOf('.') + 1);
        final art = readTags(BytesSource(bytes), ext).art;
        if (kind == 'none') {
          expect(art?.kind ?? ArtKind.none, ArtKind.none);
        } else {
          expect(art, isNotNull);
          expect(art!.offset, int.parse(offset), reason: 'offset');
          expect(art.length, int.parse(length), reason: 'length');
          expect(art.kind.name, kind, reason: 'kind');
          expect(art.type, int.parse(type), reason: 'type');
        }
        expect(
          artSources(art, folderHas: true).map((s) => s.name).join(','),
          sources,
          reason: 'sources',
        );
        if (decode != '-') {
          final picture = Uint8List.sublistView(
            bytes,
            art!.offset,
            art.offset + art.length,
          );
          expect(_decode(picture), decode, reason: 'decode');
        }
      });
    }
  });

  group('cover file', () {
    for (final [name, decode, size, colours] in of('image')) {
      test(name, () {
        final bytes = File('$_dir/$name').readAsBytesSync();
        expect(_decode(bytes), decode);
        if (decode != 'ok') return;
        // The same picture: the same way up and the same colours (the
        // firmware ignores EXIF orientation and the Adobe transform) and the
        // same shape, at the centres of its quarters.
        final [tw, th] = size.split('x').map(int.parse).toList();
        final p = shrinkToFit(decodeArt(bytes)!, ArtSize.full.pixels);
        // One thumbnail pixel of rounding either way.
        expect(
          (p.height - p.width * th / tw).abs(),
          lessThanOrEqualTo(p.width / tw + 1),
          reason: 'shape',
        );
        final want = colours.split(',');
        final at = [(1, 1), (3, 1), (1, 3), (3, 3)];
        for (final (i, (fx, fy)) in at.indexed) {
          final px = p.getPixel(fx * p.width ~/ 4, fy * p.height ~/ 4);
          final c = int.parse(want[i], radix: 16);
          final rgb = [px.r.toInt(), px.g.toInt(), px.b.toInt()];
          final exp = [c >> 16, (c >> 8) & 0xFF, c & 0xFF];
          for (var k = 0; k < 3; k++) {
            expect(
              (rgb[k] - exp[k]).abs(),
              lessThan(48),
              reason: 'quarter $i channel $k: $rgb vs $exp',
            );
          }
        }
      });
    }
  });

  group('cover file names', () {
    for (final [name, rank] in of('rank')) {
      test('"$name"', () => expect(artFolderRank(name), int.parse(rank)));
    }
    for (final [listing, rank, picked] in of('pick')) {
      test('pick $listing', () {
        final p = artFolderPick(listing.split('|'));
        expect(p?.$2 ?? 0, int.parse(rank));
        expect(p?.$1 ?? '', picked);
      });
    }
  });

  test('a front cover beats an earlier picture of another type only when '
      'the kind ties', () {
    const jpegBack = EmbeddedArt(
      offset: 0,
      length: 10,
      kind: ArtKind.jpeg,
      type: 4,
    );
    const pngFront = EmbeddedArt(
      offset: 20,
      length: 10,
      kind: ArtKind.png,
      type: 3,
    );
    const jpegFront = EmbeddedArt(
      offset: 40,
      length: 10,
      kind: ArtKind.jpeg,
      type: 3,
    );
    expect(offerArt(jpegBack, pngFront), jpegBack);
    expect(offerArt(jpegBack, jpegFront), jpegFront);
    expect(offerArt(jpegFront, jpegFront), jpegFront);
  });
}
