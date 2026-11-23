// SPDX-License-Identifier: MPL-2.0

/// Home: Music, Playlists, Settings, Shuffle Songs and, only while something
/// is loaded, Now Playing (the specification's five items), with the right
/// pane showing what is playing.
///
/// Not drawn by the design: the pane when nothing is playing (UI styles Open
/// question 9, since the clock is gone). It shows the cover placeholder,
/// `Nothing playing` and the song count (docs/ui.md's idle Home, minus the
/// clock); with no songs at all, `No songs yet` and `Settings > Library`.
library;

import 'dart:io';
import 'dart:math';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/ui/components/art_frame.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/library_screen.dart';
import 'package:neupod/ui/screens/browse/music_menu_screen.dart';
import 'package:neupod/ui/screens/browse/settings_screen.dart';
import 'package:neupod/ui/screens/shell/cover_art.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/screens/shell/stand_in_screen.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

/// The width of Home's and the Music menu's list; the pane takes the rest.
const double listBesidePaneWidth = 336;

/// Home.
class HomeScreen extends ConsumerWidget {
  /// Home.
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final player = ref.watch(playerStateProvider).value ?? PlayerState.empty;
    final loaded = player.current != null;
    return ShellFrame(
      title: 'neuPod',
      body: Builder(
        builder: (context) {
          final tier = NeupodTheme.of(context).tier;
          final h = tier.rowHeight;
          final items = [
            NeupodListItem.row(
              height: h,
              title: 'Music',
              chevron: true,
              onActivate: () =>
                  pushScreen(ref, 'Music', (_) => const MusicMenuScreen()),
            ),
            NeupodListItem.row(
              height: h,
              title: 'Playlists',
              chevron: true,
              onActivate: () => pushScreen(
                ref,
                'Playlists',
                (_) => const StandInScreen('Playlists'),
              ),
            ),
            NeupodListItem.row(
              height: h,
              title: 'Settings',
              chevron: true,
              onActivate: () =>
                  pushScreen(ref, 'Settings', (_) => const SettingsScreen()),
            ),
            NeupodListItem.row(
              height: h,
              title: 'Shuffle Songs',
              onActivate: () => _shuffle(ref),
            ),
            if (loaded)
              NeupodListItem.row(
                height: h,
                title: 'Now Playing',
                chevron: true,
                onActivate: () => ref
                    .read(shellProvider.notifier)
                    .open(ShellDestination.nowPlaying),
              ),
          ];
          return Row(
            children: [
              SizedBox(
                width: listBesidePaneWidth,
                child: BrowseListBody(
                  besidePane: true,
                  child: NeupodList(indexed: false, items: items),
                ),
              ),
              const Expanded(child: _HomePane()),
            ],
          );
        },
      ),
    );
  }

  Future<void> _shuffle(WidgetRef ref) async {
    final library = await ref.read(libraryViewProvider.future);
    final songs = [...await library.songs()]..shuffle(Random());
    await playAll(ref, songs);
  }
}

/// The right-hand pane: the cover, title, artist and a thin progress bar of
/// what is loaded; or `Nothing playing` and the song count.
class _HomePane extends ConsumerWidget {
  const _HomePane();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = NeupodTheme.of(context);
    final style = theme.style;
    final type = NeupodType(theme.tier);
    final player = ref.watch(playerStateProvider).value ?? PlayerState.empty;
    final current = player.current;
    final playingArt = ref.watch(playingArtProvider).value;
    final count = ref.watch(songCountProvider).value;
    final art = switch (theme.tier) {
      _ when theme.tier.k <= 1 => 220.0,
      _ when theme.tier.k < 1.3 => 210.0,
      _ => 200.0,
    };

    Widget? cover;
    if (playingArt != null) {
      cover = CoverArt(playingArt, decodeWidth: art);
    } else if (current?.artUri != null) {
      final uri = Uri.parse(current!.artUri!);
      if (uri.scheme == 'file') {
        cover = Image.file(
          File.fromUri(uri),
          fit: BoxFit.cover,
          errorBuilder: (_, _, _) => const SizedBox.shrink(),
        );
      }
    }

    final title = current?.title ?? 'Nothing playing';
    final second = current != null
        ? current.artist
        : switch (count) {
            null => null,
            0 => noSongsTitle,
            _ => songsLabel(count),
          };
    // An empty library says where music is added.
    final third = current == null && count == 0 ? addMusicHint : null;
    final progress = current == null || player.duration == null
        ? 0.0
        : (player.position.inMilliseconds / player.duration!.inMilliseconds)
              .clamp(0.0, 1.0);

    return PreviewPane(
      child: ExcludeSemantics(
        // The pane is for the eyes; the status bar says what plays.
        child: Column(
          children: [
            ArtFrame(size: art, use: ArtUse.home, art: cover),
            const SizedBox(height: 14),
            NeupodText(
              title,
              type.body(style.tokens.ink),
              align: TextAlign.center,
            ),
            if (second != null)
              NeupodText(
                second,
                type.small(style.list.secondary),
                align: TextAlign.center,
              ),
            if (third != null)
              NeupodText(
                third,
                type.small(style.list.secondary),
                align: TextAlign.center,
              ),
            if (current != null) ...[
              const SizedBox(height: 14),
              BarView(
                skin: style.homeProgress,
                value: progress,
                k: theme.tier.k,
              ),
            ],
          ],
        ),
      ),
    );
  }
}

/// How many songs the library holds.
final songCountProvider = FutureProvider<int>((ref) async {
  final library = await ref.watch(libraryViewProvider.future);
  return (await library.songs()).length;
});
