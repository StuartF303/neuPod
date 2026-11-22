// SPDX-License-Identifier: MPL-2.0

/// The frame a gate screen draws in: the 600x450 canvas in the style the
/// phone's mode and the screen's art call for.
///
/// Until the navigation shell supplies the chosen style (Settings), a screen
/// that is not given one takes the designed combination for the phone's
/// brightness: Classic Light, or Modern Dark with the art's palette. That is a
/// stopgap, not a decision on UI styles Open question 1.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/domain/style/art_palette.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/style/backdrop.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/text_tier.dart';

/// The style for a screen with no style chosen for it.
NeupodStyle defaultScreenStyle(BuildContext context, {ArtPalette? art}) {
  final dark = MediaQuery.platformBrightnessOf(context) == Brightness.dark;
  return dark
      ? NeupodStyles.resolve(StyleFamily.modern, StyleMode.dark, art: art)
      : NeupodStyles.resolve(StyleFamily.classic, StyleMode.light, art: art);
}

/// A [NeupodScreen] whose style defaults to [defaultScreenStyle].
class ScreenFrame extends StatelessWidget {
  /// Draws [child] on the canvas.
  const ScreenFrame({
    required this.child,
    this.style,
    this.tier,
    this.art,
    this.backdrop = BackdropLayout.lists,
    super.key,
  });

  /// The style; null takes the designed one for the phone's mode.
  final NeupodStyle? style;

  /// The text tier; null follows the phone's text size.
  final TextTier? tier;

  /// The art's palette, for Modern's accent and backdrop.
  final ArtPalette? art;

  /// Where Modern's washes sit.
  final BackdropLayout backdrop;

  /// The screen.
  final Widget child;

  @override
  Widget build(BuildContext context) => NeupodScreen(
    style: style ?? defaultScreenStyle(context, art: art),
    tier: tier,
    backdrop: backdrop,
    child: child,
  );
}
