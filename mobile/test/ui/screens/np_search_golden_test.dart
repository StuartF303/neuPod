// SPDX-License-Identifier: MPL-2.0

// Goldens of the gate screens built by UI engineer B: Now Playing (at rest,
// with the stars under the format line, the wheel setting the volume, seek
// mode, page 2, a failed volume step), Search (typing, with matches, the
// results) and the centred panel, in Classic Light and Modern Dark.
//
// Compare the Now Playing ones with the device view design's mock-ups
// (`classic-now-playing*.png`, `modern-now-playing*.png`), the Search ones with
// `*-search*.png` and the panel with `*-panel.png`. Made on macOS only (see
// flutter_test_config.dart).

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_options.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_view.dart';
import 'package:neupod/ui/screens/browse/music_menu_screen.dart';
import 'package:neupod/ui/screens/search/search_session.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/screens/search/search_view.dart';
import 'package:neupod/ui/style/backdrop.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/text_tier.dart';

import '../components/golden_harness.dart';
import 'screens_harness.dart';
import 'screens_test_support.dart';

const _track = NowPlayingData(
  title: 'Harbour Lights',
  artist: 'Aurora Lane',
  album: 'First Light',
  year: 2019,
  formatLine: 'FLAC 16/44.1',
  position: Duration(minutes: 1, seconds: 32),
  duration: Duration(minutes: 4, seconds: 1),
  art: TestArt.harbour,
  queueLabel: '3 of 12',
  contextLabel: 'track 3 of 12',
  rating: 4,
);

SearchResults _search(String q) {
  if (q.isEmpty) return SearchResults.none;
  return const SearchResults(
    artists: [SearchHit(kind: SearchKind.artist, id: 'a1', title: 'Orbital')],
    albums: [
      SearchHit(
        kind: SearchKind.album,
        id: 'b1',
        title: 'Orbital',
        detail: 'Orbital',
        year: 1991,
      ),
      SearchHit(
        kind: SearchKind.album,
        id: 'b2',
        title: 'Orbital 2',
        detail: 'Orbital',
        year: 1993,
      ),
    ],
    songs: [
      SearchHit(
        kind: SearchKind.song,
        id: 's1',
        title: "Ska'd For Life (Instrumental Mix)",
        detail: 'Orbital · Back to Mine',
      ),
      SearchHit(
        kind: SearchKind.song,
        id: 's2',
        title: 'Halcyon',
        detail: 'Orbital · Orbital 2',
      ),
    ],
  );
}

SearchSession _typed(String text, {int ringTurns = 8}) {
  final s = SearchSession(search: _search);
  // The ring starts on `a`; turn it to each letter and type it.
  var at = 0;
  for (final ch in text.codeUnits) {
    final to = ch - 0x61;
    s.wheel(to - at);
    at = to;
    s.centre();
  }
  s.wheel(ringTurns);
  return s;
}

void main() {
  for (final MapEntry(key: name, value: style) in designedStyles.entries) {
    void nowPlaying(
      String state,
      NowPlayingView Function() view, {
      List<TextTier> tiers = const [TextTier.standard, TextTier.largest],
    }) {
      for (final tier in tiers) {
        testWidgets('now playing $state, $name, ${tier.name}', (tester) async {
          await pumpScreenCanvas(
            tester,
            style,
            tier,
            view(),
            backdrop: BackdropLayout.nowPlaying,
          );
          await expectGoldenWithShadows(
            tester,
            find.byKey(const ValueKey('screen')),
            'goldens/now_playing_$state.$name.${tier.name}.png',
          );
        });
      }
    }

    nowPlaying('rest', () => const NowPlayingView(data: _track));
    nowPlaying(
      'rest_stars',
      () => const NowPlayingView(
        data: _track,
        ratingPlacement: RatingPlacement.belowFormat,
      ),
    );
    nowPlaying(
      'volume',
      () => const NowPlayingView(
        data: _track,
        mode: NowPlayingMode.volume,
        volume: 0.62,
      ),
    );
    nowPlaying(
      'seek',
      () => const NowPlayingView(
        data: _track,
        mode: NowPlayingMode.seek,
        seekTarget: Duration(minutes: 2, seconds: 14),
      ),
    );
    nowPlaying(
      'page2',
      () => const NowPlayingView(data: _track, mode: NowPlayingMode.page2),
    );
    nowPlaying(
      'page2_queue',
      () => const NowPlayingView(
        data: _track,
        mode: NowPlayingMode.page2,
        page2Focus: Page2Row.queue,
      ),
      tiers: const [TextTier.largest],
    );
    nowPlaying(
      'volume_failed',
      () => const NowPlayingView(
        data: _track,
        mode: NowPlayingMode.volume,
        volume: 0.5,
        message: "Can't set volume: noSlider",
      ),
      tiers: const [TextTier.standard],
    );

    testWidgets('search typing, $name', (tester) async {
      await pumpScreenCanvas(
        tester,
        style,
        TextTier.standard,
        SearchView(session: _typed('orb')),
      );
      await expectGoldenWithShadows(
        tester,
        find.byKey(const ValueKey('screen')),
        'goldens/search_typing.$name.standard.png',
      );
    });

    testWidgets('search typing, empty, $name', (tester) async {
      await pumpScreenCanvas(
        tester,
        style,
        TextTier.standard,
        SearchView(session: SearchSession(search: _search)),
      );
      await expectGoldenWithShadows(
        tester,
        find.byKey(const ValueKey('screen')),
        'goldens/search_empty.$name.standard.png',
      );
    });

    testWidgets('search typing, largest text, $name', (tester) async {
      await pumpScreenCanvas(
        tester,
        style,
        TextTier.largest,
        SearchView(session: _typed('orb')),
      );
      await expectGoldenWithShadows(
        tester,
        find.byKey(const ValueKey('screen')),
        'goldens/search_typing.$name.largest.png',
      );
    });

    testWidgets('search results, $name', (tester) async {
      final s = _typed('orb')..playPause();
      await pumpScreenCanvas(
        tester,
        style,
        TextTier.standard,
        SearchView(session: s, playState: PlayState.playing),
      );
      await expectGoldenWithShadows(
        tester,
        find.byKey(const ValueKey('screen')),
        'goldens/search_results.$name.standard.png',
      );
    });

    testWidgets('search results, scrolled, $name', (tester) async {
      final s = _typed('orb')
        ..playPause()
        ..wheel(5);
      await pumpScreenCanvas(
        tester,
        style,
        TextTier.standard,
        SearchView(session: s),
      );
      await expectGoldenWithShadows(
        tester,
        find.byKey(const ValueKey('screen')),
        'goldens/search_results_scrolled.$name.standard.png',
      );
    });

    testWidgets('panel, library update stopped, $name', (tester) async {
      final (family, dark) = name == 'modern_dark'
          ? (StyleFamily.modern, true)
          : (StyleFamily.classic, false);
      await withShadows(() async {
        final rig = await pumpShell(
          tester,
          home: (_) => const MusicMenuScreen(),
          family: family,
          dark: dark,
        );
        unawaited(
          rig.container
              .read(shellProvider.notifier)
              .showPanelRequest(const PanelRequest.libraryUpdateStopped()),
        );
        await settle(tester);
        await expectGoldenWithShadows(
          tester,
          find.byKey(const ValueKey('device')),
          'goldens/panel_library_update_stopped.$name.standard.png',
        );
      });
    });
  }
}
