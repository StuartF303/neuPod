// SPDX-License-Identifier: MPL-2.0

/// Now Playing, drawn (UI styles design "Now Playing" and the device view
/// design, section 7): at rest, with the wheel setting the volume, in seek
/// mode, and page 2. Pure drawing of [NowPlayingData]; the wiring is
/// `now_playing_screen.dart`.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/art_frame.dart';
import 'package:neupod/ui/components/bars.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/components/small_parts.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/screens/common/scroll_window.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_format.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_options.dart';
import 'package:neupod/ui/style/icons/neupod_glyphs.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

/// What Now Playing shows.
enum NowPlayingMode {
  /// Art, text, progress.
  rest,

  /// The wheel is turning: the volume bar replaces the progress bar.
  volume,

  /// Seek mode: the target in amber.
  seek,

  /// Page 2: the rating, shuffle, repeat and the way to the queue.
  page2,
}

/// The rows of page 2, in order.
enum Page2Row {
  /// The five stars.
  rating,

  /// Shuffle on or off.
  shuffle,

  /// Repeat off, all or one.
  repeat,

  /// Opens the queue.
  queue,
}

/// The track and the player's state, as Now Playing draws them.
@immutable
class NowPlayingData {
  /// What to draw.
  const NowPlayingData({
    required this.title,
    required this.artist,
    this.album,
    this.year,
    this.formatLine,
    this.position = Duration.zero,
    this.duration,
    this.art,
    this.playState = PlayState.playing,
    this.shuffle = false,
    this.repeat = RepeatState.off,
    this.rating = 0,
    this.queueLabel,
    this.contextLabel,
  });

  /// The title.
  final String title;

  /// The artist.
  final String artist;

  /// The album, if the tags give one.
  final String? album;

  /// Its year.
  final int? year;

  /// `FLAC 16/44.1`.
  final String? formatLine;

  /// Where the track is (the target, while one is being moved).
  final Duration position;

  /// The track's length, when known.
  final Duration? duration;

  /// The cover, sized to fill its square.
  final Widget? art;

  /// Playing, paused or stopped, for the status bar.
  final PlayState playState;

  /// Shuffle is on.
  final bool shuffle;

  /// Repeat off, all or one.
  final RepeatState repeat;

  /// Stars, 0 to 5.
  final int rating;

  /// `3 of 12`, in the status bar.
  final String? queueLabel;

  /// What a screen reader says for [queueLabel] (`track 3 of 12`).
  final String? contextLabel;

  /// `Album · Year`, or whichever part there is.
  String? get albumLine {
    final parts = [
      if (album != null && album!.isNotEmpty) album!,
      if (year != null) '$year',
    ];
    return parts.isEmpty ? null : parts.join(' · ');
  }

  /// 0 to 1.
  double get progress {
    final d = duration;
    if (d == null || d.inMilliseconds == 0) return 0;
    return (position.inMilliseconds / d.inMilliseconds).clamp(0.0, 1.0);
  }

  /// The word between the times (`shuffle`, `repeat 1`).
  String? get modeWord {
    final repeatWord = switch (repeat) {
      RepeatState.off => null,
      RepeatState.all => 'repeat',
      RepeatState.one => 'repeat 1',
    };
    final parts = [if (shuffle) 'shuffle', ?repeatWord];
    return parts.isEmpty ? null : parts.join(' · ');
  }
}

/// Now Playing in one of its [NowPlayingMode]s.
class NowPlayingView extends StatelessWidget {
  /// Draws [data] in [mode].
  const NowPlayingView({
    required this.data,
    this.mode = NowPlayingMode.rest,
    this.volume,
    this.seekTarget,
    this.ratingPlacement = RatingPlacement.page2,
    this.message,
    this.page2Focus = Page2Row.rating,
    this.ratingEditing = false,
    this.onArtTap,
    this.dragToSeek = false,
    this.onSeekFraction,
    this.onPage2Tap,
    this.withStatusBar = true,
    super.key,
  });

  /// The track.
  final NowPlayingData data;

  /// What is showing.
  final NowPlayingMode mode;

  /// The volume, 0 to 1, for [NowPlayingMode.volume].
  final double? volume;

  /// The seek target, for [NowPlayingMode.seek].
  final Duration? seekTarget;

  /// Where the stars are drawn (design gate switch).
  final RatingPlacement ratingPlacement;

  /// A message in the status bar's title place (`Seek not available`).
  final String? message;

  /// The focused row of page 2.
  final Page2Row page2Focus;

  /// The stars are being set: the wheel changes them.
  final bool ratingEditing;

  /// A tap on the art opens page 2 (direct touch).
  final VoidCallback? onArtTap;

  /// The progress bar may be dragged (a gate switch).
  final bool dragToSeek;

  /// Where a drag or tap on the progress bar went, 0 to 1.
  final ValueChanged<double>? onSeekFraction;

  /// A page 2 row was tapped.
  final ValueChanged<Page2Row>? onPage2Tap;

  /// Draw the status bar (goldens of the view alone); the shell's frame
  /// draws it for the screen, from [statusTitle] and the rest of the data.
  final bool withStatusBar;

  @override
  Widget build(BuildContext context) {
    final body = mode == NowPlayingMode.page2
        ? _page2(context)
        : _page1(context);
    if (!withStatusBar) return body;
    return NeupodScaffold(
      body: body,
      statusBar: StatusBar(
        title: 'Now Playing',
        playState: data.playState,
        shuffle: data.shuffle,
        repeat: data.repeat,
        context: data.queueLabel,
        contextLabel: data.contextLabel,
        message: message,
      ),
    );
  }

  // ---- page 1 ------------------------------------------------------------

  Widget _page1(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final type = NeupodType(theme.tier);
    final t = theme.style.tokens;
    final k = theme.tier.k;
    final seek = mode == NowPlayingMode.seek;
    final duration = data.duration ?? Duration.zero;
    final shown = seek ? (seekTarget ?? data.position) : data.position;
    final stars = ratingPlacement == RatingPlacement.belowFormat && !seek;

    final art = GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onArtTap,
      child: ArtFrame(
        size: 210,
        use: ArtUse.nowPlaying,
        art: data.art,
        reflection: !seek,
        semanticLabel: data.album == null
            ? 'Album art'
            : 'Album art, ${data.album}',
      ),
    );

    final column = Expanded(
      child: SizedBox(
        height: 210,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            NeupodText(data.title, type.large(t.ink)),
            const SizedBox(height: 6),
            NeupodText(data.artist, type.body(t.ink2)),
            if (!seek && data.albumLine != null) ...[
              const SizedBox(height: 4),
              NeupodText(data.albumLine!, type.small(t.ink2)),
            ],
            const Spacer(),
            if (seek)
              NeupodText(
                formatClock(shown),
                type.sized(72, theme.style.seek.amber, lineHeight: 76),
              )
            else ...[
              if (data.formatLine != null)
                NeupodText(data.formatLine!, type.small(t.ink3)),
              if (stars) ...[
                SizedBox(
                  height: k > 1 ? 0 : 6,
                ), // no room for it at the larger tiers
                RatingStars(rating: data.rating),
              ],
            ],
          ],
        ),
      ),
    );

    final times = ProgressTimes(
      elapsed: formatClock(data.position),
      remaining: data.duration == null
          ? ''
          : formatRemaining(data.position, duration),
      middle: data.modeWord,
    );
    final spoken = data.duration == null
        ? null
        : '${spokenClock(data.position)} of ${spokenClock(duration)}';

    final Widget lower;
    if (seek) {
      lower = Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // The bar sits 34 under the art; closer at the larger tiers, where
          // the bigger text and bar need the room.
          SizedBox(height: 34 / k),
          SeekBar(
            value: duration.inMilliseconds == 0
                ? 0
                : (shown.inMilliseconds / duration.inMilliseconds),
            semanticsValue: spoken,
          ),
          const SizedBox(height: 12),
          times,
          const SizedBox(height: 14),
          NeupodText(
            'Scroll: seek · Select: go · Menu: cancel',
            type.small(theme.style.list.secondary),
            align: TextAlign.center,
          ),
        ],
      );
    } else {
      final barH = (theme.style.progress.height + 8 + 24) * k;
      final slotH = mode == NowPlayingMode.volume
          ? (theme.style.volume.capsuleHeight * k > barH
                ? theme.style.volume.capsuleHeight * k
                : barH)
          : barH;
      lower = Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const SizedBox(height: 22),
          mode == NowPlayingMode.volume
              ? SizedBox(
                  height: slotH,
                  child: Center(child: NeupodVolumeBar(value: volume ?? 0)),
                )
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    NeupodProgressBar(
                      value: data.progress,
                      seekable: dragToSeek,
                      onSeek: onSeekFraction,
                      semanticsValue: spoken,
                    ),
                    const SizedBox(height: 8),
                    times,
                  ],
                ),
        ],
      );
    }

    return Padding(
      padding: const EdgeInsets.fromLTRB(19, 22, 19, 0),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SizedBox(
            height: seek ? 210 : 270,
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [art, const SizedBox(width: 22), column],
            ),
          ),
          lower,
        ],
      ),
    );
  }

  // ---- page 2 ------------------------------------------------------------

  Widget _page2(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final tier = theme.tier;
    final focused = page2Focus.index;
    final rows = <Widget>[
      _RatingRow(
        rating: data.rating,
        selected: focused == Page2Row.rating.index,
        editing: ratingEditing,
        onTap: () => onPage2Tap?.call(Page2Row.rating),
      ),
      NeupodRow(
        title: 'Shuffle',
        value: data.shuffle ? 'On' : 'Off',
        selection: focused == Page2Row.shuffle.index
            ? RowSelection.selected
            : RowSelection.none,
        position: '2 of 4',
        onTap: () => onPage2Tap?.call(Page2Row.shuffle),
      ),
      NeupodRow(
        title: 'Repeat',
        value: switch (data.repeat) {
          RepeatState.off => 'Off',
          RepeatState.all => 'All',
          RepeatState.one => 'One',
        },
        selection: focused == Page2Row.repeat.index
            ? RowSelection.selected
            : RowSelection.none,
        position: '3 of 4',
        onTap: () => onPage2Tap?.call(Page2Row.repeat),
      ),
      NeupodRow(
        title: 'Queue',
        chevron: true,
        selection: focused == Page2Row.queue.index
            ? RowSelection.selected
            : RowSelection.none,
        position: '4 of 4',
        onTap: () => onPage2Tap?.call(Page2Row.queue),
      ),
    ];
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        AlbumHeader(
          artist: data.title,
          details: [
            data.artist,
            if (data.album != null && data.album!.isNotEmpty) data.album!,
          ],
          art: data.art,
        ),
        Expanded(
          child: ListGround(
            topRule: true,
            child: ScrollWindow(
              heights: [for (final _ in rows) tier.rowHeight],
              selected: focused,
              children: rows,
            ),
          ),
        ),
      ],
    );
  }
}

/// Page 2's Rating row: the title and five stars at the right, drawn on the
/// selection bar's colours when it is focused.
class _RatingRow extends StatelessWidget {
  const _RatingRow({
    required this.rating,
    required this.selected,
    required this.editing,
    this.onTap,
  });

  final int rating;
  final bool selected;
  final bool editing;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final style = theme.style;
    final k = theme.tier.k;
    final base = NeupodRow(
      title: editing ? 'Rating: scroll to set' : 'Rating',
      selection: selected ? RowSelection.selected : RowSelection.none,
      position: rating == 0 ? 'not rated' : '$rating of 5 stars',
      onTap: onTap,
    );
    final filled = selected ? style.selection.text : style.rating.filled;
    final empty = selected ? style.selection.text : style.rating.empty;
    final size = 24 * k;
    return Stack(
      children: [
        base,
        Positioned.fill(
          child: IgnorePointer(
            child: ExcludeSemantics(
              child: Padding(
                padding: EdgeInsets.only(right: style.list.rowPadX),
                child: Align(
                  alignment: Alignment.centerRight,
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      for (var i = 0; i < 5; i++) ...[
                        if (i > 0) SizedBox(width: size * 0.2),
                        i < rating
                            ? NeupodIcon(
                                NeupodGlyph.star,
                                size: size,
                                color: filled,
                                paint: GlyphPaint.fillAndStroke,
                              )
                            : NeupodIcon(
                                NeupodGlyph.star,
                                size: size,
                                color: empty,
                                strokeWidth: 2,
                              ),
                      ],
                    ],
                  ),
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}
