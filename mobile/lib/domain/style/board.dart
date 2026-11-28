// SPDX-License-Identifier: MPL-2.0

/// The board pictures' data (design 2026-11-25-app-custom-mode, section 6):
/// `assets/boards/boards.json`, one entry per board. Pure Dart.
library;

import 'dart:convert';

import 'package:neupod/domain/style/custom_look.dart';

/// Width of a board picture, px. This is W, the phone's width.
const int boardPictureWidth = 1200;

/// Height of a board picture, px.
const int boardPictureHeight = 3120;

/// Where the wheel's centre sits on the picture, px.
const double boardAnchorX = 600;
const double boardAnchorY = 2040;

/// The frost copy's size, px.
const int boardFrostWidth = 300;
const int boardFrostHeight = 780;

/// Radius about the anchor, at full size, over which the frost pair is
/// measured: the largest wheel circle.
const double boardMeasureRadius = 384;

/// One board: what the paint rule and the layers need that is not the
/// picture itself.
final class BoardInfo {
  const BoardInfo({
    required this.id,
    required this.name,
    required this.ground,
    required this.frostDark,
    required this.frostLight,
    this.placeholder = false,
  });

  /// `main-top`, `main-bottom` or `copper`.
  final String id;

  /// What the board picker shows.
  final String name;

  /// `0xRRGGBB`, painted wherever the picture does not reach.
  final int ground;

  /// The darkest pixel of the blurred copy near the anchor, `0xRRGGBB`.
  final int frostDark;

  /// The lightest pixel of the blurred copy near the anchor, `0xRRGGBB`.
  final int frostLight;

  /// The picture is a stand-in, not a neuPod board. A store build must not
  /// contain one (the release check).
  final bool placeholder;

  /// The pair the contrast rule measures a see-through wheel against.
  List<int> get frostPair => [frostDark, frostLight];

  /// [frostPair] by the name the part editor uses.
  List<int> get grounds => frostPair;

  /// Asset path of the sharp picture.
  String get picturePath => 'assets/boards/$id.webp';

  /// Asset path of the blurred copy.
  String get frostPath => 'assets/boards/$id.frost.webp';
}

int? _hex(Object? v) {
  if (v is! String) return null;
  final m = RegExp(r'^#([0-9a-fA-F]{6})$').firstMatch(v);
  return m == null ? null : int.parse(m.group(1)!, radix: 16);
}

/// Reads `boards.json`. An entry without a usable id, name or colours is
/// left out; nothing throws on a bad file (an empty list is returned).
List<BoardInfo> parseBoards(String json) {
  final Object? root;
  try {
    root = jsonDecode(json);
  } on FormatException {
    return const [];
  }
  final entries = root is Map ? root['boards'] : null;
  if (entries is! List) return const [];
  final out = <BoardInfo>[];
  for (final e in entries) {
    if (e is! Map) continue;
    final id = e['id'];
    final name = e['name'];
    final ground = _hex(e['ground']);
    final dark = _hex(e['frostDark']);
    final light = _hex(e['frostLight']);
    if (id is! String ||
        name is! String ||
        ground == null ||
        dark == null ||
        light == null) {
      continue;
    }
    out.add(
      BoardInfo(
        id: id,
        name: name,
        ground: ground,
        frostDark: dark,
        frostLight: light,
        placeholder: e['placeholder'] == true,
      ),
    );
  }
  return out;
}

/// The board named [id], else [defaultBoard], else the first, else null.
BoardInfo? boardFor(List<BoardInfo> boards, String id) {
  for (final b in boards) {
    if (b.id == id) return b;
  }
  for (final b in boards) {
    if (b.id == defaultBoard) return b;
  }
  return boards.isEmpty ? null : boards.first;
}
