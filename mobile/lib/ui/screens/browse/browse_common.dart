// SPDX-License-Identifier: MPL-2.0

/// Parts the browsing screens share: loading a list from the library view,
/// the rows for songs, albums and the rest, opening screens, and playing from
/// a list.
library;

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/icloud_download_providers.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/library_view/reason_words.dart';
import 'package:neupod/ui/components/art_frame.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/screens/shell/cover_art.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/style/icons/neupod_glyphs.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/skins.dart';

/// `1,204`.
String groupedCount(int n) {
  final s = '$n';
  final out = StringBuffer();
  for (var i = 0; i < s.length; i++) {
    if (i > 0 && (s.length - i) % 3 == 0) out.write(',');
    out.write(s[i]);
  }
  return out.toString();
}

/// `1 song`, `10 songs`.
String songsLabel(int n) => n == 1 ? '1 song' : '${groupedCount(n)} songs';

/// `1 album`, `9 albums`.
String albumsLabel(int n) => n == 1 ? '1 album' : '${groupedCount(n)} albums';

/// An album's length: `6:41`, `1:12:05`.
String durationLabel(Duration d) {
  final h = d.inHours;
  final m = d.inMinutes.remainder(60);
  final s = d.inSeconds.remainder(60);
  String two(int n) => n.toString().padLeft(2, '0');
  return h > 0 ? '$h:${two(m)}:${two(s)}' : '$m:${two(s)}';
}

/// The year, the song count and the length: what an album's header and its
/// rows say.
List<String> albumDetails(Album a, {bool length = true}) => [
  if (a.year != null) '${a.year}',
  songsLabel(a.songCount),
  if (length && a.totalTime != null) durationLabel(a.totalTime!),
];

/// The artist an album's list row shows: none for a compilation. The
/// firmware's `lib_list_row` (lib_reader.c) gives an album row its artist
/// only when the album has one (`en.artist != LIB_NONE`), so a compilation's
/// row in the Albums or Compilations list, and its Cover Flow caption, has
/// no artist; only the album's header says "Various Artists" (`album_header`
/// in lib_view.c).
String albumRowArtist(Album a) => a.compilation ? '' : a.artist;

/// An album row's second line, `Artist · 1993`: the artist left out for a
/// compilation ([albumRowArtist]).
String albumArtistYear(Album a) => [
  if (albumRowArtist(a).isNotEmpty) albumRowArtist(a),
  if (a.year != null) '${a.year}',
].join(' · ');

/// Pushes a screen.
void pushScreen(WidgetRef ref, String name, WidgetBuilder builder) => ref
    .read(shellProvider.notifier)
    .push(ShellPage(name: name, builder: builder));

/// A list's body: the list ground (Modern's veil and padding) around the
/// list.
class BrowseListBody extends StatelessWidget {
  /// A body of [child], beside a pane when [besidePane].
  const BrowseListBody({
    required this.child,
    this.besidePane = false,
    this.topRule = false,
    super.key,
  });

  final Widget child;
  final bool besidePane;

  /// A hairline above the list (Modern, under the album header).
  final bool topRule;

  @override
  Widget build(BuildContext context) =>
      ListGround(besidePane: besidePane, topRule: topRule, child: child);
}

/// Loads from the library view (again whenever a finished scan rebuilds it)
/// and builds with the result, or with
/// null while it loads (the screens draw their status bar either way).
class LibraryLoader<T> extends ConsumerStatefulWidget {
  /// Loads with [load] and builds with [builder].
  const LibraryLoader({required this.load, required this.builder, super.key});

  final Future<T> Function(LibraryView library) load;
  final Widget Function(BuildContext context, T? data) builder;

  @override
  ConsumerState<LibraryLoader<T>> createState() => _LibraryLoaderState<T>();
}

class _LibraryLoaderState<T> extends ConsumerState<LibraryLoader<T>> {
  T? _data;
  bool _loaded = false;

  @override
  void initState() {
    super.initState();
    unawaited(_load());
  }

  Future<void> _load() async {
    try {
      final library = await ref.read(libraryViewProvider.future);
      final data = await widget.load(library);
      _loaded = true;
      if (mounted) setState(() => _data = data);
    } on Object {
      if (!mounted) return;
      unawaited(
        ref
            .read(shellProvider.notifier)
            .showPanel(
              title: "Can't open",
              message: "The library couldn't be read.",
            ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    // A finished scan rebuilds the library view: load again, so an open list
    // shows what the scan found (a song downloaded from iCloud now playable).
    // (Not the first load's own loading-to-data step: initState covers it.)
    ref.listen(libraryViewProvider, (was, now) {
      if (_loaded && was?.isLoading == true && !now.isLoading && now.hasValue) {
        unawaited(_load());
      }
    });
    return widget.builder(context, _data);
  }
}

/// The glyph before a greyed song's reason: `cloud-download` for an iCloud
/// file not on the phone, `play-off` for one that cannot play.
NeupodGlyph songReasonGlyph(Song song) =>
    song.downloadable ? NeupodGlyph.cloudDownload : NeupodGlyph.playOff;

/// A song as a two-line row: the title over `Artist · Album`. [downloading]
/// is `icloudDownloadsProvider`'s set, watched by the screen.
NeupodListItem songItem(
  BuildContext context, {
  required Song song,
  required VoidCallback onActivate,
  Set<String> downloading = const {},
}) {
  final tier = NeupodTheme.of(context).tier;
  return NeupodListItem.row(
    height: tier.row2Height,
    title: song.title,
    subtitle: '${song.artist} · ${song.album}',
    initial: song.sortLetter,
    disabledReason: songRowReason(song, downloading),
    reasonGlyph: songReasonGlyph(song),
    greyedAction: song.downloadable ? downloadActionText : null,
    onActivate: onActivate,
    id: song.id,
  );
}

/// An album as a two-line row with its cover.
NeupodListItem albumItem(
  BuildContext context, {
  required Album album,
  required String subtitle,
  required VoidCallback onActivate,
}) {
  final tier = NeupodTheme.of(context).tier;
  return NeupodListItem.row(
    height: tier.row2Height,
    title: album.title,
    subtitle: subtitle,
    chevron: true,
    initial: album.sortLetter,
    onActivate: onActivate,
    thumbnail: (selected) => ArtFrame(
      size: 59 * tier.k,
      use: selected ? ArtUse.thumbnailSelected : ArtUse.thumbnail,
      art: CoverArt(album.art, decodeWidth: 59 * tier.k),
    ),
  );
}

/// Plays [songs] from [chosen]: the list it came from is the queue, the songs
/// before it kept for Previous (docs/ui.md). Greyed songs are left out of the
/// queue, so one that cannot play never stops it. Opens Now Playing.
///
/// A [chosen] song that is in iCloud and not on the phone is not played:
/// Select asks iOS to download it (owner decision 7), and its row says
/// `Downloading` until the scan finds it on the phone.
Future<void> playFrom(WidgetRef ref, List<Song> songs, Song chosen) async {
  if (chosen.downloadable) return download(ref, chosen);
  final playable = [
    for (final s in songs)
      if (s.playable) s,
  ];
  final start = playable.indexWhere((s) => s.id == chosen.id);
  if (start < 0) return;
  await _start(ref, playable, start);
}

/// Plays all of [songs] in the order given (Shuffle Songs passes a shuffled
/// list).
Future<void> playAll(WidgetRef ref, List<Song> songs) async {
  final playable = [
    for (final s in songs)
      if (s.playable) s,
  ];
  if (playable.isEmpty) return;
  await _start(ref, playable, 0);
}

/// Asks iOS to download [song] (owner decision 7). If iOS refuses (signed
/// out of iCloud, say), a centred panel says so, as the one for a song that
/// will not play does.
Future<void> download(WidgetRef ref, Song song) async {
  try {
    await ref.read(icloudDownloadsProvider.notifier).request(song.uri);
  } on Object {
    unawaited(
      ref
          .read(shellProvider.notifier)
          .showPanel(
            title: "Can't download",
            message: "iCloud couldn't download this song.",
          ),
    );
  }
}

Future<void> _start(WidgetRef ref, List<Song> songs, int start) async {
  final shell = ref.read(shellProvider.notifier);
  try {
    final items = await ref.read(queueMapperProvider)(songs, start: start);
    final player = ref.read(playerProvider);
    await player.setQueue(items, startIndex: start);
    // Now Playing opens only once there is something to show.
    shell.open(ShellDestination.nowPlaying);
    await player.play();
  } on Object {
    unawaited(
      shell.showPanel(title: "Can't play", message: "Can't play this song."),
    );
  }
}
