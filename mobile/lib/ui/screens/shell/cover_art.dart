// SPDX-License-Identifier: MPL-2.0

/// An album's cover as a picture to put in an `ArtFrame`: the cover file when
/// there is one, otherwise a placeholder like the mock-ups' (two colours split
/// on the diagonal, from the album's seed).
library;

import 'dart:io';

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';

/// The cover for [art], filling its parent. [decodeWidth] is the widest it is
/// drawn, in design px (the image is decoded no larger).
class CoverArt extends StatelessWidget {
  /// A cover.
  const CoverArt(this.art, {this.decodeWidth = 240, super.key});

  final ArtRef art;
  final double decodeWidth;

  @override
  Widget build(BuildContext context) {
    final placeholder = _Placeholder(art.seed);
    final path = art.path;
    if (path == null) return placeholder;
    return Image.file(
      File(path),
      fit: BoxFit.cover,
      cacheWidth: (decodeWidth * 2).round(),
      gaplessPlayback: true,
      excludeFromSemantics: true,
      errorBuilder: (_, _, _) => placeholder,
    );
  }
}

class _Placeholder extends StatelessWidget {
  const _Placeholder(this.seed);

  final int seed;

  @override
  Widget build(BuildContext context) {
    final (dark, light) = placeholderColours(seed);
    return CustomPaint(painter: _Split(dark, light));
  }
}

class _Split extends CustomPainter {
  _Split(this.dark, this.light);

  final Color dark;
  final Color light;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = light);
    canvas.drawPath(
      Path()
        ..moveTo(0, 0)
        ..lineTo(size.width, 0)
        ..lineTo(0, size.height)
        ..close(),
      Paint()..color = dark,
    );
  }

  @override
  bool shouldRepaint(_Split old) => old.dark != dark || old.light != light;
}
