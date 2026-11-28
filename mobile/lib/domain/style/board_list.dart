// SPDX-License-Identifier: MPL-2.0

/// The boards a look may show through its see-through parts, in the order the
/// board picker lists them (design section 6).
///
/// **A minimal list, in one place.** The real list is `boards.json` beside the
/// board pictures, which the transparency step (stage 2 of AP-1) adds; it
/// replaces this file. Until then the names are the design's, and the ground
/// and frost colours are those the design's `render.py` gives its
/// placeholder pictures. None of these is a neuPod board picture.
library;

import 'package:neupod/domain/style/custom_look.dart';

/// One board.
final class BoardInfo {
  const BoardInfo({
    required this.id,
    required this.name,
    required this.ground,
    required this.frostDark,
    required this.frostLight,
  });

  /// One of [boardIds].
  final String id;

  /// What the board picker shows.
  final String name;

  /// The colour painted where the picture does not reach (`0xrrggbb`).
  final int ground;

  /// The darkest and lightest colour of the blurred copy inside the largest
  /// wheel circle (`0xrrggbb`): what shows through a see-through wheel.
  final int frostDark;
  final int frostLight;

  /// [frostDark] and [frostLight], the grounds `paintPart` takes for the wheel.
  List<int> get grounds => [frostDark, frostLight];
}

/// The boards, in picker order.
const List<BoardInfo> boardList = [
  BoardInfo(
    id: 'main-top',
    name: 'Main board, top',
    ground: 0x123524,
    frostDark: 0x143022,
    frostLight: 0x34543e,
  ),
  BoardInfo(
    id: 'main-bottom',
    name: 'Main board, bottom',
    ground: 0x101a2e,
    frostDark: 0x121a2c,
    frostLight: 0x303c56,
  ),
  BoardInfo(
    id: 'copper',
    name: 'Copper only',
    ground: 0x2a1c10,
    frostDark: 0x2c1e12,
    frostLight: 0x543c26,
  ),
];

/// The board with [id]; an unknown id is the first ([defaultBoard]).
BoardInfo boardById(String id) =>
    boardList.firstWhere((b) => b.id == id, orElse: () => boardList.first);
