// SPDX-License-Identifier: MPL-2.0

import 'dart:ui' as ui;

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/style/board.dart';

/// Where the board pictures come from. The app reads its bundle; a test
/// swaps in a counting fake to see what was decoded.
class BoardAssets {
  const BoardAssets();

  /// Decodes the picture at asset [path].
  Future<ui.Image> decode(String path) async {
    final data = await rootBundle.load(path);
    final codec = await ui.instantiateImageCodec(data.buffer.asUint8List());
    try {
      return (await codec.getNextFrame()).image;
    } finally {
      codec.dispose();
    }
  }
}

/// The source of the board pictures.
final boardAssetsProvider = Provider<BoardAssets>((ref) => const BoardAssets());

/// The sharp picture of board [id], decoded once and kept. Read only while a
/// see-through body needs it: a look with every part solid never reads it.
final boardPictureProvider = FutureProvider.family<ui.Image, String>((
  ref,
  id,
) async {
  final image = await ref
      .watch(boardAssetsProvider)
      .decode('assets/boards/$id.webp');
  ref.onDispose(image.dispose);
  return image;
});

/// The blurred copy of board [id] (300 x 780), decoded once and kept. Read
/// only while a see-through wheel or centre needs it.
final boardFrostProvider = FutureProvider.family<ui.Image, String>((
  ref,
  id,
) async {
  final image = await ref
      .watch(boardAssetsProvider)
      .decode('assets/boards/$id.frost.webp');
  ref.onDispose(image.dispose);
  return image;
});

/// Where the picture goes for a phone of width [width] whose wheel centre is
/// at [wheelCentre]: scaled by width / 1200, the anchor (600, 2040) on the
/// wheel's centre. Never stretched or cropped.
Rect boardPictureRect({required double width, required Offset wheelCentre}) {
  final k = width / boardPictureWidth;
  return Rect.fromLTWH(
    wheelCentre.dx - boardAnchorX * k,
    wheelCentre.dy - boardAnchorY * k,
    boardPictureWidth * k,
    boardPictureHeight * k,
  );
}

/// Canvas units, in the wheel's 200-unit square, per pixel of the full-size
/// picture: the wheel of [wheelDiameter] points sits on a phone [width]
/// points wide, and the picture is drawn at width / 1200.
double boardUnitsPerPixel({
  required double width,
  required double wheelDiameter,
}) => (width / boardPictureWidth) * 200 / wheelDiameter;

/// The sharp picture, laid in [rect] (see [boardPictureRect]).
class BoardPictureLayer extends StatelessWidget {
  const BoardPictureLayer({required this.image, required this.rect, super.key});

  final ui.Image image;
  final Rect rect;

  @override
  Widget build(BuildContext context) {
    return Positioned.fromRect(
      rect: rect,
      child: IgnorePointer(
        child: RawImage(
          image: image,
          width: rect.width,
          height: rect.height,
          fit: BoxFit.fill,
          filterQuality: FilterQuality.medium,
        ),
      ),
    );
  }
}
