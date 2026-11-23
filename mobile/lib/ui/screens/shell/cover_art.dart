// SPDX-License-Identifier: MPL-2.0

/// An album's or a track's cover as a picture to put in an `ArtFrame`: its
/// picture from the art cache when it has one that decodes (or a ready image
/// file), otherwise a placeholder like the mock-ups' (two colours split on the
/// diagonal, from the album's seed).
library;

import 'dart:io';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/art_providers.dart';
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
    if (art.sources.isNotEmpty) {
      return _CachedCover(art, decodeWidth, placeholder);
    }
    final path = art.path;
    if (path == null) return placeholder;
    return _file(path, decodeWidth, placeholder);
  }
}

Widget _file(String path, double decodeWidth, Widget placeholder) => Image.file(
  File(path),
  fit: BoxFit.cover,
  cacheWidth: (decodeWidth * 2).round(),
  gaplessPlayback: true,
  excludeFromSemantics: true,
  errorBuilder: (_, _, _) => placeholder,
);

/// A cover from the art cache: the placeholder until the cached file is
/// there, and for good when no picture decodes.
class _CachedCover extends ConsumerWidget {
  const _CachedCover(this.art, this.decodeWidth, this.placeholder);

  final ArtRef art;
  final double decodeWidth;
  final Widget placeholder;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final size = ArtSize.forPixels(decodeWidth * 2);
    final path = ref.watch(artFileProvider((art, size))).value;
    if (path == null) return placeholder;
    return _file(path, decodeWidth, placeholder);
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
