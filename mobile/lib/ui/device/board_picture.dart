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

  /// Decodes the picture at asset [path], scaled down to [targetWidth] pixels
  /// wide when that is given (and under the picture's own width).
  Future<ui.Image> decode(String path, {int? targetWidth}) async {
    final data = await rootBundle.load(path);
    final codec = await ui.instantiateImageCodec(
      data.buffer.asUint8List(),
      targetWidth: targetWidth,
    );
    try {
      return (await codec.getNextFrame()).image;
    } finally {
      codec.dispose();
    }
  }
}

/// The source of the board pictures.
final boardAssetsProvider = Provider<BoardAssets>((ref) => const BoardAssets());

/// Which sharp picture to decode: board [id], [width] pixels across.
typedef BoardPictureKey = ({String id, int width});

/// The pixel width to decode the sharp picture at for a phone [logicalWidth]
/// points wide at [devicePixelRatio]: the width it is drawn at, never more
/// than the picture has.
int boardPictureDecodeWidth(double logicalWidth, double devicePixelRatio) =>
    (logicalWidth * devicePixelRatio).round().clamp(1, boardPictureWidth);

/// The sharp picture of a board, decoded at the width it is drawn at (not the
/// full 1200 x 3120, about 15 MB) and released when nothing shows it. Read
/// only while a see-through body needs it: a look with every part solid never
/// reads it.
final boardPictureProvider = FutureProvider.autoDispose
    .family<ui.Image, BoardPictureKey>((ref, key) async {
      final image = await ref
          .watch(boardAssetsProvider)
          .decode('assets/boards/${key.id}.webp', targetWidth: key.width);
      if (!ref.mounted) {
        image.dispose();
        throw StateError('board picture released before it was decoded');
      }
      ref.onDispose(image.dispose);
      return image;
    });

/// The blurred copy of board [id] (300 x 780, small), released when nothing
/// shows it. Read only while a see-through wheel or centre needs it.
final boardFrostProvider = FutureProvider.autoDispose.family<ui.Image, String>((
  ref,
  id,
) async {
  final image = await ref
      .watch(boardAssetsProvider)
      .decode('assets/boards/$id.frost.webp');
  if (!ref.mounted) {
    image.dispose();
    throw StateError('board frost released before it was decoded');
  }
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
