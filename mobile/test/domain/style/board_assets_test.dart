// SPDX-License-Identifier: MPL-2.0

// The board assets (design section 6): boards.json, the pictures and their
// frost copies. The asset test fails a board whose frost pair would make a
// white, 60 % see-through wheel give back more than 10 %. The release check
// keeps a placeholder picture out of a store build.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;
import 'package:neupod/domain/style/board.dart';
import 'package:neupod/domain/style/colour_math.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/custom_paint.dart';

import '../../../tool/check_release_boards.dart';

const String _dir = 'assets/boards';

List<BoardInfo> _boards() =>
    parseBoards(File('$_dir/boards.json').readAsStringSync());

/// The transparency a white wheel (0, 0, 100) asked to keep 60 % keeps over
/// [board]'s frost pair.
int _whiteWheelKeeps(List<int> frostPair) => paintPart(
  const PartPaint(hue: 0, saturation: 0, brightness: 100, transparency: 60),
  LookPart.wheel,
  grounds: frostPair,
).transparencyKept;

void main() {
  test('boards.json lists the three boards of the design, in order', () {
    final boards = _boards();
    expect([for (final b in boards) b.id], boardIds);
    expect(
      [for (final b in boards) b.name],
      ['Main board, top', 'Main board, bottom', 'Copper only'],
    );
  });

  for (final board in _boards()) {
    group(board.id, () {
      final picture = img.decodeWebP(
        File('$_dir/${board.id}.webp').readAsBytesSync(),
      )!;
      final frostBytes = File('$_dir/${board.id}.frost.webp').readAsBytesSync();

      test('the picture is 1200 x 3120', () {
        expect(picture.width, boardPictureWidth);
        expect(picture.height, boardPictureHeight);
        expect(
          picture.getPixel(0, 0).a,
          picture.maxChannelValue,
          reason: 'opaque',
        );
      });

      test('the frost copy is 300 x 780', () {
        final d = img.decodeWebP(frostBytes)!;
        expect(d.width, boardFrostWidth);
        expect(d.height, boardFrostHeight);
      });

      test('frostDark and frostLight are the extremes of the saved copy', () {
        final frost = img.decodeWebP(frostBytes)!;
        final k = boardFrostWidth / boardPictureWidth;
        final (cx, cy, r) = (
          boardAnchorX * k,
          boardAnchorY * k,
          boardMeasureRadius * k,
        );
        var dark = 2.0, light = -1.0;
        for (final p in frost) {
          final dx = p.x + 0.5 - cx, dy = p.y + 0.5 - cy;
          if (dx * dx + dy * dy > r * r) continue;
          final l = relativeLuminance(rgbOf(p.r, p.g, p.b));
          if (l < dark) dark = l;
          if (l > light) light = l;
        }
        expect(relativeLuminance(board.frostDark), closeTo(dark, 1e-9));
        expect(relativeLuminance(board.frostLight), closeTo(light, 1e-9));
      });

      test('a white wheel asked for 60 % keeps at least 50 %', () {
        expect(
          _whiteWheelKeeps(board.frostPair),
          greaterThanOrEqualTo(50),
          reason:
              '${board.id}: the blurred copy is too uneven near the anchor '
              '(${hexOf(board.frostDark)} to ${hexOf(board.frostLight)}); '
              'keep large white silkscreen and bare metal out of radius '
              '384 px',
        );
      });
    });
  }

  test('the asset test measures: the widest possible pair gives back 10 %', () {
    // Black to white is the worst a frost pair can be: exactly the limit.
    expect(_whiteWheelKeeps([0x000000, 0xffffff]), 50);
    expect(_whiteWheelKeeps([0x303030, 0xd0d0d0]), greaterThan(50));
  });

  group('the release check', () {
    bool all(String _) => true;

    test('fails while a board is a placeholder, and names it', () {
      final problems = releaseProblems(_boards(), exists: all);
      // Today's pictures are the placeholders from the design's render.py.
      expect(problems, isNotEmpty);
      expect(problems.first, contains('PLACEHOLDER'));
    });

    test('passes real pictures, and fails a missing file', () {
      const real = BoardInfo(
        id: 'main-top',
        name: 'Main board, top',
        ground: 0x123524,
        frostDark: 0x0a0c10,
        frostLight: 0x5b6539,
      );
      expect(releaseProblems([real], exists: all), isEmpty);
      expect(
        releaseProblems([real], exists: (p) => !p.endsWith('.frost.webp')),
        hasLength(1),
      );
      expect(releaseProblems(const [], exists: all), isNotEmpty);
    });

    test('NEUPOD_RELEASE=true: the bundled boards must be real', () {
      expect(
        releaseProblems(_boards(), exists: (p) => File(p).existsSync()),
        isEmpty,
      );
    }, skip: !const bool.fromEnvironment('NEUPOD_RELEASE'));
  });

  test('parseBoards drops what it cannot read and never throws', () {
    expect(parseBoards('not json'), isEmpty);
    expect(parseBoards('{"boards": 3}'), isEmpty);
    expect(
      parseBoards(
        '{"boards": [{"id": "a", "name": "A", "ground": "#000000"}, 7, '
        '{"id": "b", "name": "B", "ground": "#102030", '
        '"frostDark": "#000001", "frostLight": "#ffffff"}]}',
      ).map((b) => b.id),
      ['b'],
    );
  });

  test('boardFor falls back to the default, then the first', () {
    final boards = _boards();
    expect(boardFor(boards, 'copper')!.id, 'copper');
    expect(boardFor(boards, 'nope')!.id, defaultBoard);
    expect(boardFor(boards.sublist(1), 'nope')!.id, 'main-bottom');
    expect(boardFor(const [], 'copper'), isNull);
  });
}
