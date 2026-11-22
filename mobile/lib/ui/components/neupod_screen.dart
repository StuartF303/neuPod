// SPDX-License-Identifier: MPL-2.0

/// The root of every device screen: a 600x450 canvas in design px (device view
/// design, section 1). Screens are laid out here with the design's exact
/// values; the device view scales the canvas as a whole (s = 0.88 W / 600),
/// so nothing below this widget knows the phone's size.
///
/// The phone's text size reaches the screen only as a [TextTier] (section 8):
/// inside the canvas the text scaler is reset to none, so type is never scaled
/// twice.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/style/backdrop.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/text_tier.dart';

/// The design canvas.
const Size neupodScreenSize = Size(600, 450);

class NeupodScreen extends StatelessWidget {
  const NeupodScreen({
    super.key,
    required this.style,
    required this.child,
    this.tier,
    this.backdrop = BackdropLayout.lists,
  });

  final NeupodStyle style;

  /// The text tier; null takes it from the phone's text size.
  final TextTier? tier;

  /// Where Modern's washes sit on this screen.
  final BackdropLayout backdrop;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    final t = tier ?? TextTier.of(context);
    final media = MediaQuery.maybeOf(context) ?? const MediaQueryData();
    return FittedBox(
      fit: BoxFit.contain,
      child: SizedBox.fromSize(
        size: neupodScreenSize,
        child: ClipRect(
          child: MediaQuery(
            data: media.copyWith(
              textScaler: TextScaler.noScaling,
              size: neupodScreenSize,
            ),
            child: NeupodTheme(
              style: style,
              tier: t,
              child: DefaultTextStyle(
                style: NeupodType(t).body(style.tokens.ink),
                child: Stack(
                  fit: StackFit.expand,
                  children: [
                    if (style.drawsBackdrop)
                      ModernBackdrop(
                        ground: style.ground,
                        palette: style.palette,
                        layout: backdrop,
                      )
                    else
                      ColoredBox(color: style.ground),
                    child,
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// A status bar above a body: the frame of every screen (owner decision 6).
class NeupodScaffold extends StatelessWidget {
  const NeupodScaffold({
    super.key,
    required this.statusBar,
    required this.body,
  });

  final Widget statusBar;
  final Widget body;

  @override
  Widget build(BuildContext context) {
    final tier = NeupodTheme.of(context).tier;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        SizedBox(height: tier.statusBarHeight, child: statusBar),
        Expanded(child: body),
      ],
    );
  }
}

/// One line of design text: no wrapping, an ellipsis when it does not fit
/// (device view section 8: "Text that no longer fits ends in an ellipsis").
class NeupodText extends StatelessWidget {
  const NeupodText(this.text, this.style, {super.key, this.align});

  final String text;
  final TextStyle style;
  final TextAlign? align;

  @override
  Widget build(BuildContext context) => Text(
    text,
    style: style,
    maxLines: 1,
    softWrap: false,
    overflow: TextOverflow.ellipsis,
    textAlign: align,
  );
}
