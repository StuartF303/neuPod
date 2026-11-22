// SPDX-License-Identifier: MPL-2.0

/// Styles and modes (UI styles design, owner decision 1): a theme is a style
/// (Classic or Modern) and a mode (Light or Dark). Each designed combination is
/// a [NeupodStyle]: the firmware's 14 colour tokens (`app/ui/ui_theme.h`) plus
/// the skins.
///
/// Designed so far: Classic Light and Modern Dark. Classic Dark and Modern
/// Light are not designed (UI styles Open question 1, BLOCKED in PROGRESS.md);
/// they slot in as two more builders in [NeupodStyles._builders].
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/style/art_palette.dart';
import 'package:neupod/ui/style/classic_light.dart';
import 'package:neupod/ui/style/modern_dark.dart';
import 'package:neupod/ui/style/skins.dart';
import 'package:neupod/ui/style/text_tier.dart';

enum StyleFamily { classic, modern }

enum StyleMode {
  light,
  dark;

  static StyleMode fromBrightness(Brightness b) =>
      b == Brightness.dark ? dark : light;
}

/// The firmware's 14 colour tokens (`ui_tok_t`), with the same names.
@immutable
final class NeupodTokens {
  const NeupodTokens({
    required this.bg,
    required this.surface,
    required this.bar,
    required this.line,
    required this.ink,
    required this.ink2,
    required this.ink3,
    required this.accent,
    required this.onAccent,
    required this.accentSoft,
    required this.amber,
    required this.amberSoft,
    required this.green,
    required this.red,
  });

  /// Screen background.
  final Color bg;

  /// Cards, input fields, the right pane.
  final Color surface;

  /// Status bar fill (its main tone; the skin has the full gradient).
  final Color bar;

  /// Hairlines and tracks.
  final Color line;

  /// Primary, secondary and tertiary text.
  final Color ink;
  final Color ink2;
  final Color ink3;

  /// Selection bar, progress fill, focused value; and text on it.
  final Color accent;
  final Color onAccent;
  final Color accentSoft;

  /// Seek mode, warnings.
  final Color amber;
  final Color amberSoft;

  /// Charging; battery under 15 % and errors.
  final Color green;
  final Color red;
}

/// One designed style and mode: tokens and skins.
@immutable
final class NeupodStyle {
  const NeupodStyle({
    required this.family,
    required this.mode,
    required this.tokens,
    required this.drawsBackdrop,
    required this.ground,
    required this.statusBar,
    required this.selection,
    required this.list,
    required this.scrollBar,
    required this.progress,
    required this.homeProgress,
    required this.volume,
    required this.panel,
    required this.art,
    required this.header,
    required this.pane,
    required this.index,
    required this.section,
    required this.seek,
    required this.searchField,
    required this.rating,
    this.palette,
  });

  final StyleFamily family;
  final StyleMode mode;
  final NeupodTokens tokens;

  /// Whether the style draws an art backdrop behind the screen (Modern).
  final bool drawsBackdrop;

  /// The art colours the style was built with (Modern), if any.
  final ArtPalette? palette;

  /// The screen ground (under the backdrop, if any).
  final Color ground;

  final StatusBarSkin statusBar;
  final SelectionSkin selection;
  final ListSkin list;
  final ScrollBarSkin scrollBar;

  /// Now Playing's progress bar, and the small one on Home's right pane.
  final BarSkin progress;
  final BarSkin homeProgress;
  final VolumeSkin volume;
  final PanelSkin panel;
  final ArtFrameSkin art;
  final HeaderSkin header;

  /// Home and Music's right-hand preview pane.
  final BoxSkin pane;
  final IndexSkin index;
  final SectionSkin section;
  final SeekSkin seek;
  final SearchFieldSkin searchField;
  final RatingSkin rating;
}

typedef StyleBuilder = NeupodStyle Function(ArtPalette? art);

abstract final class NeupodStyles {
  static final Map<(StyleFamily, StyleMode), StyleBuilder> _builders = {
    (StyleFamily.classic, StyleMode.light): (_) => classicLight(),
    (StyleFamily.modern, StyleMode.dark): modernDark,
    // (StyleFamily.classic, StyleMode.dark): not designed (Open question 1).
    // (StyleFamily.modern, StyleMode.light): not designed (Open question 1).
  };

  /// Whether the combination has been designed.
  static bool isDesigned(StyleFamily f, StyleMode m) =>
      _builders.containsKey((f, m));

  /// The designed style, or null for a combination not designed yet. [art] is
  /// the playing (or shown) album's palette; Modern takes its accent and
  /// backdrop from it, Classic ignores it.
  static NeupodStyle? maybeOf(StyleFamily f, StyleMode m, {ArtPalette? art}) {
    final builder = _builders[(f, m)];
    if (builder == null) return null;
    // The same inputs give the same instance, so NeupodTheme only notifies
    // when something changed. Classic ignores the art.
    final key = (f, m, f == StyleFamily.classic ? null : art);
    final hit = _cache.remove(key);
    final style = hit ?? builder(art);
    _cache[key] = style;
    if (_cache.length > 16) _cache.remove(_cache.keys.first);
    return style;
  }

  static final Map<(StyleFamily, StyleMode, ArtPalette?), NeupodStyle> _cache =
      {};

  /// The style for [f] in mode [m]; while [m] is not designed for [f], the
  /// family's designed mode instead. This stopgap keeps the app drawable
  /// until Open question 1 is answered; it does not decide what the missing
  /// modes look like.
  static NeupodStyle resolve(StyleFamily f, StyleMode m, {ArtPalette? art}) {
    final exact = maybeOf(f, m, art: art);
    if (exact != null) return exact;
    for (final mode in StyleMode.values) {
      final s = maybeOf(f, mode, art: art);
      if (s != null) return s;
    }
    throw StateError('no designed mode for $f');
  }
}

/// Provides the style and text tier to the components below it.
class NeupodTheme extends InheritedWidget {
  const NeupodTheme({
    super.key,
    required this.style,
    required this.tier,
    required super.child,
  });

  final NeupodStyle style;
  final TextTier tier;

  static NeupodTheme of(BuildContext context) {
    final t = context.dependOnInheritedWidgetOfExactType<NeupodTheme>();
    assert(t != null, 'No NeupodTheme above this widget (use NeupodScreen)');
    return t!;
  }

  @override
  bool updateShouldNotify(NeupodTheme old) =>
      old.style != style || old.tier != tier;
}
