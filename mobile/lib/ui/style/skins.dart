// SPDX-License-Identifier: MPL-2.0

/// The skins a style owns (UI styles design, "What a style owns"): status bar
/// fill, battery, selection bar, scroll bar, progress and volume bars, panel,
/// art frame and list ground, plus the smaller parts the device view design
/// added (letter tile and rail, disc heading, seek bar, search field).
///
/// Skins are plain data. Components read them and never ask which style is
/// active, so Classic Dark and Modern Light slot in as two more tables
/// (`neupod_style.dart`) once they are designed (UI styles Open question 1).
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/style/icons/neupod_glyphs.dart';

/// A CSS-like box: base fill, an optional overlay gradient (Modern's gloss),
/// a border, outer shadows (a zero-blur spread shadow is a ring), and a 1 px
/// inset highlight along the top edge (CSS `inset 0 1px 0 <colour>`).
@immutable
final class BoxSkin {
  const BoxSkin({
    this.color,
    this.gradient,
    this.overlay,
    this.radius = 0,
    this.border,
    this.shadows = const [],
    this.insetTop,
  });

  final Color? color;
  final Gradient? gradient;
  final Gradient? overlay;
  final double radius;
  final BoxBorder? border;
  final List<BoxShadow> shadows;
  final Color? insetTop;

  BoxSkin copyWith({
    Color? color,
    List<BoxShadow>? shadows,
    double? radius,
    Gradient? gradient,
  }) => BoxSkin(
    color: color ?? this.color,
    gradient: gradient ?? this.gradient,
    overlay: overlay,
    radius: radius ?? this.radius,
    border: border,
    shadows: shadows ?? this.shadows,
    insetTop: insetTop,
  );
}

/// Paints a [BoxSkin] behind [child].
class SkinBox extends StatelessWidget {
  const SkinBox({super.key, required this.skin, this.child});

  final BoxSkin skin;
  final Widget? child;

  @override
  Widget build(BuildContext context) {
    final radius = BorderRadius.circular(skin.radius);
    // CSS order: shadows and fill, then the gloss and the inset highlight,
    // then the content.
    var inner = child;
    if (skin.overlay != null || skin.insetTop != null) {
      inner = CustomPaint(
        painter: _OverlayPainter(skin.overlay, skin.insetTop, skin.radius),
        child: inner,
      );
    }
    return DecoratedBox(
      decoration: BoxDecoration(
        color: skin.gradient == null ? skin.color : null,
        gradient: skin.gradient,
        borderRadius: skin.radius > 0 ? radius : null,
        border: skin.border,
        boxShadow: skin.shadows,
      ),
      child: inner,
    );
  }
}

class _OverlayPainter extends CustomPainter {
  _OverlayPainter(this.overlay, this.insetTop, this.radius);

  final Gradient? overlay;
  final Color? insetTop;
  final double radius;

  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    final rrect = RRect.fromRectAndRadius(rect, Radius.circular(radius));
    if (overlay != null) {
      canvas.drawRRect(rrect, Paint()..shader = overlay!.createShader(rect));
    }
    if (insetTop != null) {
      final shifted = rrect.shift(const Offset(0, 1));
      final band = Path.combine(
        PathOperation.difference,
        Path()..addRRect(rrect),
        Path()..addRRect(shifted),
      );
      canvas.drawPath(band, Paint()..color = insetTop!);
    }
  }

  @override
  bool shouldRepaint(_OverlayPainter old) =>
      old.overlay != overlay ||
      old.insetTop != insetTop ||
      old.radius != radius;
}

/// A vertical CSS gradient, top to bottom, with stops in 0..1.
LinearGradient vertical(List<Color> colors, [List<double>? stops]) =>
    LinearGradient(
      begin: Alignment.topCenter,
      end: Alignment.bottomCenter,
      colors: colors,
      stops: stops,
    );

/// A horizontal CSS gradient (`90deg`), left to right.
LinearGradient horizontal(List<Color> colors, [List<double>? stops]) =>
    LinearGradient(
      begin: Alignment.centerLeft,
      end: Alignment.centerRight,
      colors: colors,
      stops: stops,
    );

@immutable
final class BatterySkin {
  const BatterySkin({
    required this.outline,
    required this.radius,
    required this.fill,
    required this.fillRadius,
    required this.terminal,
    required this.terminalGap,
    required this.low,
    this.inside,
  });

  /// Body 44x22 with a 2 px outline and 2 px padding (both styles).
  static const Size body = Size(44, 22);
  static const double outlineWidth = 2;
  static const double padding = 2;

  final Color outline;
  final double radius;
  final Color? inside;
  final Gradient fill;
  final double fillRadius;
  final Size terminal;
  final double terminalGap;

  /// The fill below 15 % (as built: red below 15 %).
  final Color low;
}

@immutable
final class StatusBarSkin {
  const StatusBarSkin({
    required this.ground,
    required this.glyph,
    required this.context,
    required this.title,
    required this.dim,
    required this.bluetoothActive,
    required this.iconGap,
    required this.play,
    required this.pause,
    required this.playPaint,
    required this.battery,
    required this.volume,
    required this.volumeGlyphPaint,
  });

  /// Fill and bottom rule.
  final BoxSkin ground;

  /// Play-state and hardware glyphs.
  final Color glyph;

  /// The context text after the glyphs (`3 of 12`, `1,204`).
  final Color context;
  final Color title;

  /// Bluetooth saved but not connected.
  final Color dim;

  /// Bluetooth connected or in use (fixed, not the accent).
  final Color bluetoothActive;

  /// Space after each hardware icon.
  final double iconGap;
  final NeupodGlyph play;
  final NeupodGlyph pause;
  final GlyphPaint playPaint;
  final BatterySkin battery;

  /// The small bar that takes the title's place while the volume changes.
  final BarSkin volume;

  /// Classic fills its speaker glyphs and strokes them; Modern the same in its
  /// ink (both mock-ups use `fill` and a 2 px stroke).
  final GlyphPaint volumeGlyphPaint;
}

/// A bar: a track and a fill (progress, the volume bars, the status volume).
@immutable
final class BarSkin {
  const BarSkin({
    required this.height,
    required this.radius,
    required this.track,
    required this.fill,
    this.fillRadius = 0,
  });

  final double height;
  final double radius;
  final BoxSkin track;
  final BoxSkin fill;
  final double fillRadius;
}

/// Now Playing's volume bar, which replaces the progress bar while the wheel
/// turns.
@immutable
final class VolumeSkin {
  const VolumeSkin({
    required this.bar,
    required this.glyphSize,
    required this.gap,
    required this.glyph,
    this.capsule,
    this.capsuleHeight = 0,
    this.capsulePadding = 0,
  });

  final BarSkin bar;
  final double glyphSize;
  final double gap;
  final Color glyph;

  /// Modern floats the bar in a capsule; Classic has none.
  final BoxSkin? capsule;
  final double capsuleHeight;
  final double capsulePadding;
}

@immutable
final class SelectionSkin {
  const SelectionSkin({
    required this.insetX,
    required this.bar,
    required this.radiusOneLine,
    required this.radiusTwoLine,
    required this.text,
    required this.flash,
    required this.flashText,
    required this.pressedTint,
    this.textShadows = const [],
  });

  /// Space between the list's edges and the bar (Modern inset 6 px).
  final double insetX;
  final BoxSkin bar;
  final double radiusOneLine;
  final double radiusTwoLine;
  final Color text;
  final List<Shadow> textShadows;

  /// The bar while it confirms an action with a tick.
  final BoxSkin flash;

  /// Text and tick on the flash.
  final Color flashText;

  /// A row under a finger (device view, candidate 2).
  final Color pressedTint;
}

@immutable
final class ListSkin {
  const ListSkin({
    required this.rowPadX,
    required this.secondary,
    required this.number,
    required this.chevron,
    required this.disabled,
    required this.reason,
    this.ground,
    this.paneGround,
    this.padX = 0,
    this.divider,
  });

  /// Text inset inside a row, from the list's padded edge.
  final double rowPadX;

  /// The list ground over the backdrop (Modern), and the list next to a
  /// preview pane (Home, Music).
  final BoxSkin? ground;
  final BoxSkin? paneGround;

  /// Horizontal padding of the list (Modern 6 px, the bar's inset).
  final double padX;
  final Color? divider;
  final Color secondary;

  /// Track numbers.
  final Color number;
  final Color chevron;

  /// A greyed row's text.
  final Color disabled;

  /// The reason on a greyed row (must reach 4.5:1).
  final Color reason;
}

@immutable
final class ScrollBarSkin {
  const ScrollBarSkin({
    required this.width,
    required this.padding,
    required this.thumb,
    this.gutter,
  });

  final double width;
  final EdgeInsets padding;
  final BoxSkin? gutter;
  final BoxSkin thumb;
}

@immutable
final class PanelSkin {
  const PanelSkin({
    required this.scrim,
    required this.box,
    required this.title,
    required this.body,
    required this.okHeight,
    required this.ok,
    required this.okText,
    required this.padding,
    required this.gap,
    this.titleStrip,
    this.okTextShadows = const [],
    this.okMarginTop = 0,
  });

  static const double width = 452;

  final Color scrim;
  final BoxSkin box;

  /// Classic's title sits in a strip of the status-bar gradient.
  final BoxSkin? titleStrip;
  final Color title;
  final Color body;

  /// Padding of the panel (Modern) or of the text block (Classic).
  final EdgeInsets padding;
  final double gap;
  final double okHeight;
  final BoxSkin ok;
  final Color okText;
  final List<Shadow> okTextShadows;
  final double okMarginTop;
}

/// Where a piece of art is shown; each use has its own radius and shadow.
enum ArtUse { home, nowPlaying, header, thumbnail, thumbnailSelected }

@immutable
final class ArtFrameSkin {
  const ArtFrameSkin({
    required this.radius,
    required this.shadows,
    required this.reflection,
    this.insetTop,
    this.placeholder = const Color(0xff9aa0a8),
  });

  final double Function(ArtUse use) radius;
  final List<BoxShadow> Function(ArtUse use) shadows;

  /// Classic's Now Playing reflection: 56 px at 30 % fading to nothing, 2 px
  /// below the art.
  final bool reflection;
  final Color? insetTop;

  /// Behind a missing cover. Not designed; a flat neutral square, no glyph.
  final Color placeholder;
}

@immutable
final class HeaderSkin {
  const HeaderSkin({required this.ground, required this.secondary});

  /// The album header band (121 px), with its rule.
  final BoxSkin ground;
  final Color secondary;
}

@immutable
final class IndexSkin {
  const IndexSkin({
    required this.tile,
    required this.tileLetter,
    required this.rail,
    required this.railCurrent,
  });

  final Color tile;
  final Color tileLetter;
  final Color rail;
  final Color railCurrent;
}

@immutable
final class SectionSkin {
  const SectionSkin({
    required this.ground,
    required this.text,
    this.extraPadX = 0,
  });

  /// Disc headings and Search's section headings.
  final Color ground;
  final Color text;

  /// Modern adds the list's 6 px padding to the text inset.
  final double extraPadX;
}

@immutable
final class SeekSkin {
  const SeekSkin({required this.amber, required this.track});

  final Color amber;
  final Color track;
}

@immutable
final class SearchFieldSkin {
  const SearchFieldSkin({
    required this.box,
    required this.text,
    required this.glyph,
    required this.caret,
    required this.ringRule,
    required this.chipOn,
    required this.chipOnText,
    required this.chipOff,
    required this.chipOffText,
  });

  final BoxSkin box;
  final Color text;
  final Color glyph;

  /// The caret, and the letter ring's current glyph (Classic the accent;
  /// Modern the light art tint, as the design's drawing has it).
  final Color caret;

  /// The hairline above the letter ring.
  final Color ringRule;

  /// The active mode chip (filled) and the others (outlined).
  final BoxSkin chipOn;
  final Color chipOnText;
  final BoxSkin chipOff;
  final Color chipOffText;
}

@immutable
final class RatingSkin {
  const RatingSkin({required this.filled, required this.empty});

  /// Not designed (the mock-ups have no stars; the specification asks for
  /// them). Filled stars in the secondary ink, empty ones as outlines in the
  /// tertiary ink: the shape differs, so it does not rely on colour.
  final Color filled;
  final Color empty;
}
