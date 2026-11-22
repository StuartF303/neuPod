// SPDX-License-Identifier: MPL-2.0

/// Modern's art backdrop (UI styles design, "Modern, Dark: the values"):
///
/// > two soft washes in the art's two main colours (one near a top corner at
/// > about 45 to 55 % strength, one near the opposite bottom corner at about
/// > 75 to 85 %), under a darkening scrim of 30 % at the top to 50 to 55 % at
/// > the bottom. Made once per track from the art (a small blurred, darkened
/// > copy), never blurred live. No art: the ground alone.
///
/// The colours come from [ArtPalette], computed once per track from a 24 px
/// copy of the cover (`art_colours.dart`). The washes are radial gradients:
/// a gradient is already as soft as a blur and costs nothing to draw, and the
/// painter sits in a repaint boundary, so it is rasterised once and reused
/// while lists scroll over it. Nothing is blurred at run time.
library;

import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/style/art_palette.dart';

/// One wash: a CSS radial gradient, `radial-gradient(RX% RY% at X% Y%,
/// colour, transparent FADE%)`.
@immutable
final class Wash {
  const Wash({
    required this.centre,
    required this.radii,
    required this.alpha,
    this.fade = 0.6,
  });

  /// Centre and radii as fractions of the screen's width and height.
  final Offset centre;
  final Size radii;
  final double alpha;
  final double fade;
}

/// The placement of the two washes and the scrim, as drawn in the mock-ups.
@immutable
final class BackdropLayout {
  const BackdropLayout({
    required this.primary,
    required this.secondary,
    required this.scrimTop,
    required this.scrimBottom,
  });

  final Wash primary;
  final Wash secondary;
  final double scrimTop;
  final double scrimBottom;

  /// Lists, Home and menus: the art's colour near the top right
  /// (`modern-songs.html`).
  static const lists = BackdropLayout(
    primary: Wash(
      centre: Offset(0.85, 0.20),
      radii: Size(1.2, 0.9),
      alpha: 0.45,
    ),
    secondary: Wash(
      centre: Offset(0.10, 1.00),
      radii: Size(1.1, 1.0),
      alpha: 0.75,
      fade: 0.65,
    ),
    scrimTop: 0.30,
    scrimBottom: 0.50,
  );

  /// Now Playing: the art's colour near the top left, behind the cover
  /// (`modern-now-playing.html`).
  static const nowPlaying = BackdropLayout(
    primary: Wash(
      centre: Offset(0.15, 0.25),
      radii: Size(1.1, 1.0),
      alpha: 0.55,
    ),
    secondary: Wash(
      centre: Offset(0.90, 1.00),
      radii: Size(1.2, 1.1),
      alpha: 0.85,
      fade: 0.65,
    ),
    scrimTop: 0.30,
    scrimBottom: 0.55,
  );

  /// An album (`modern-album.html`).
  static const album = BackdropLayout(
    primary: Wash(
      centre: Offset(0.15, 0.20),
      radii: Size(1.1, 0.9),
      alpha: 0.50,
    ),
    secondary: Wash(
      centre: Offset(0.90, 1.00),
      radii: Size(1.2, 1.1),
      alpha: 0.80,
      fade: 0.65,
    ),
    scrimTop: 0.35,
    scrimBottom: 0.60,
  );
}

/// Paints the ground, and over it the backdrop when there is a [palette].
class ModernBackdrop extends StatelessWidget {
  const ModernBackdrop({
    super.key,
    required this.ground,
    this.palette,
    this.layout = BackdropLayout.lists,
  });

  final Color ground;
  final ArtPalette? palette;
  final BackdropLayout layout;

  @override
  Widget build(BuildContext context) => RepaintBoundary(
    child: CustomPaint(
      painter: BackdropPainter(
        ground: ground,
        palette: palette,
        layout: layout,
      ),
      size: Size.infinite,
    ),
  );
}

class BackdropPainter extends CustomPainter {
  BackdropPainter({
    required this.ground,
    required this.palette,
    required this.layout,
  });

  final Color ground;
  final ArtPalette? palette;
  final BackdropLayout layout;

  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    canvas.drawRect(rect, Paint()..color = ground);
    final p = palette;
    if (p == null) return;
    // CSS paints the last background layer first: the second wash, the first,
    // then the scrim on top.
    _wash(canvas, size, layout.secondary, Color(0xff000000 | p.washSecondary));
    _wash(canvas, size, layout.primary, Color(0xff000000 | p.washPrimary));
    canvas.drawRect(
      rect,
      Paint()
        ..shader = ui.Gradient.linear(rect.topCenter, rect.bottomCenter, [
          Color.fromRGBO(0, 0, 0, layout.scrimTop),
          Color.fromRGBO(0, 0, 0, layout.scrimBottom),
        ]),
    );
  }

  void _wash(Canvas canvas, Size size, Wash w, Color c) {
    final centre = Offset(w.centre.dx * size.width, w.centre.dy * size.height);
    final rx = w.radii.width * size.width;
    final ry = w.radii.height * size.height;
    canvas.save();
    canvas.clipRect(Offset.zero & size);
    canvas.translate(centre.dx, centre.dy);
    canvas.scale(rx, ry);
    // A unit circle scaled to the ellipse; same colour at both ends so the
    // fade does not pass through grey.
    canvas.drawRect(
      const Rect.fromLTRB(-4, -4, 4, 4),
      Paint()
        ..shader = ui.Gradient.radial(
          Offset.zero,
          1,
          [c.withValues(alpha: w.alpha), c.withValues(alpha: 0)],
          [0, w.fade],
        ),
    );
    canvas.restore();
  }

  @override
  bool shouldRepaint(BackdropPainter old) =>
      old.ground != ground || old.palette != palette || old.layout != layout;
}
