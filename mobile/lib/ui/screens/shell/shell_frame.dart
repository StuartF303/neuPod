// SPDX-License-Identifier: MPL-2.0

/// The frame every screen is built in: the 600x450 canvas in the chosen style
/// (`NeupodScreen`), the status bar with its title, and the body.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/style/art_palette.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/style/backdrop.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// A screen's frame: a status bar titled [title] over [body], in the style
/// and mode the owner chose, at the phone's text tier.
///
/// [palette] is the album on screen (Cover Flow, an album); without it
/// Modern uses [ambientPaletteProvider].
class ShellFrame extends ConsumerWidget {
  /// A frame.
  const ShellFrame({
    required this.title,
    required this.body,
    this.context,
    this.contextLabel,
    this.message,
    this.shuffle = false,
    this.repeat = RepeatState.off,
    this.palette,
    this.backdrop = BackdropLayout.lists,
    super.key,
  });

  /// The status bar's title.
  final String title;

  /// Text after the play glyph at the left of the status bar (`9 albums`,
  /// `1,204`).
  final String? context;

  /// What a screen reader says for [context] (`track 3 of 12`).
  final String? contextLabel;

  /// A message in the title's place for about two seconds.
  final String? message;

  /// Shuffle and repeat glyphs after the play glyph (Now Playing).
  final bool shuffle;
  final RepeatState repeat;

  /// The album's colours for Modern.
  final ArtPalette? palette;

  /// Where Modern's washes sit.
  final BackdropLayout backdrop;

  /// Below the status bar.
  final Widget body;

  @override
  Widget build(BuildContext buildContext, WidgetRef ref) {
    final choice = ref.watch(styleChoiceProvider);
    final mode = effectiveMode(
      choice,
      MediaQuery.platformBrightnessOf(buildContext),
    );
    final art = palette ?? ref.watch(ambientPaletteProvider);
    final style = NeupodStyles.resolve(choice.family, mode, art: art);
    final status = ref.watch(deviceStatusProvider);
    final player = ref.watch(playerStateProvider).value ?? PlayerState.empty;
    final playState = player.current == null
        ? PlayState.stopped
        : (player.playWhenReady ? PlayState.playing : PlayState.paused);
    return NeupodScreen(
      style: style,
      backdrop: backdrop,
      child: NeupodScaffold(
        statusBar: StatusBar(
          title: title,
          playState: playState,
          context: context,
          contextLabel: contextLabel,
          message: message,
          shuffle: shuffle,
          repeat: repeat,
          output: status.output,
          bluetooth: status.bluetooth,
          battery: status.battery,
        ),
        body: body,
      ),
    );
  }
}
