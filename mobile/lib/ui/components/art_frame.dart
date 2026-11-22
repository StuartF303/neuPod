// SPDX-License-Identifier: MPL-2.0

/// The art frame (Classic: square corners and a soft shadow, a reflection on
/// Now Playing; Modern: rounded, a deep shadow and a 1 px light top edge) and
/// the album header, which shows the cover, the artist, the year, the song
/// count and the total time (specification, "Browsing").
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

class ArtFrame extends StatelessWidget {
  const ArtFrame({
    super.key,
    required this.size,
    required this.use,
    this.art,
    this.reflection = false,
    this.semanticLabel,
  });

  /// Edge length in design px.
  final double size;
  final ArtUse use;

  /// The cover, sized to fill the square; null draws the placeholder.
  final Widget? art;

  /// Classic's Now Playing reflection (ignored by styles without one).
  final bool reflection;

  /// `Album art, First Light`; null hides the art from the screen reader.
  final String? semanticLabel;

  /// Classic reflection: 56 px at 30 %, 2 px under the art, fading out.
  static const double reflectionHeight = 56;
  static const double reflectionGap = 2;
  static const double reflectionOpacity = 0.3;

  @override
  Widget build(BuildContext context) {
    final skin = NeupodTheme.of(context).style.art;
    final radius = BorderRadius.circular(skin.radius(use));
    final picture = art ?? ColoredBox(color: skin.placeholder);
    Widget framed = SizedBox.square(
      dimension: size,
      child: DecoratedBox(
        decoration: BoxDecoration(
          borderRadius: radius,
          boxShadow: skin.shadows(use),
        ),
        child: ClipRRect(
          borderRadius: radius,
          child: CustomPaint(
            foregroundPainter: skin.insetTop == null
                ? null
                : _TopEdgePainter(skin.insetTop!, skin.radius(use)),
            child: SizedBox.expand(child: picture),
          ),
        ),
      ),
    );
    if (reflection && skin.reflection) {
      framed = Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          framed,
          const SizedBox(height: reflectionGap),
          SizedBox(
            width: size,
            height: reflectionHeight,
            child: ClipRect(
              child: ShaderMask(
                blendMode: BlendMode.dstIn,
                shaderCallback: (r) => const LinearGradient(
                  begin: Alignment.topCenter,
                  end: Alignment.bottomCenter,
                  colors: [Color(0xff000000), Color(0x00000000)],
                ).createShader(r),
                child: Opacity(
                  opacity: reflectionOpacity,
                  child: OverflowBox(
                    alignment: Alignment.topCenter,
                    minHeight: size,
                    maxHeight: size,
                    child: Transform.flip(
                      flipY: true,
                      child: SizedBox.square(dimension: size, child: picture),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ],
      );
    }
    return semanticLabel == null
        ? ExcludeSemantics(child: framed)
        : Semantics(
            image: true,
            label: semanticLabel,
            child: ExcludeSemantics(child: framed),
          );
  }
}

class _TopEdgePainter extends CustomPainter {
  _TopEdgePainter(this.color, this.radius);

  final Color color;
  final double radius;

  @override
  void paint(Canvas canvas, Size size) {
    final rrect = RRect.fromRectAndRadius(
      Offset.zero & size,
      Radius.circular(radius),
    );
    canvas.drawPath(
      Path.combine(
        PathOperation.difference,
        Path()..addRRect(rrect),
        Path()..addRRect(rrect.shift(const Offset(0, 1))),
      ),
      Paint()..color = color,
    );
  }

  @override
  bool shouldRepaint(_TopEdgePainter old) =>
      old.color != color || old.radius != radius;
}

/// The album header: 121 px, a 96 px cover, the artist in Large, then a Small
/// line of [details] joined with ` · ` (`1993 · 10 songs · 52 min`). The
/// screen formats the details; the header only lays them out.
class AlbumHeader extends StatelessWidget {
  const AlbumHeader({
    super.key,
    required this.artist,
    required this.details,
    this.art,
  });

  final String artist;
  final List<String> details;
  final Widget? art;

  static const double height = 121;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final type = NeupodType(theme.tier);
    final skin = theme.style.header;
    final line = details.join(' · ');
    return Semantics(
      header: true,
      label: [artist, ...details].join(', '),
      child: ExcludeSemantics(
        child: SizedBox(
          height: height,
          child: SkinBox(
            skin: skin.ground,
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 19),
              child: Row(
                children: [
                  ArtFrame(size: 96, use: ArtUse.header, art: art),
                  const SizedBox(width: 18),
                  Expanded(
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        NeupodText(artist, type.large(theme.style.tokens.ink)),
                        const SizedBox(height: 4),
                        NeupodText(line, type.small(skin.secondary)),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}
