// SPDX-License-Identifier: MPL-2.0

// Goldens of the browsing screens in the shell, both styles (Classic Light,
// Modern Dark), at the standard text tier, and Home and Songs at all three.
// Real names from the generated test library's manifest, placeholder covers
// like the mock-ups'. Compare with docs/design/2026-11-21-ui-styles/mockups
// and docs/design/2026-11-21-app-device-view/mockups; where they differ the
// build follows the specification (docs/app/PROGRESS.md, Notes).
//
// macOS only: see ../components/flutter_test_config.dart.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/screens/browse/album_screen.dart';
import 'package:neupod/ui/screens/browse/albums_screen.dart';
import 'package:neupod/ui/screens/browse/artists_screen.dart';
import 'package:neupod/ui/screens/browse/collection_screen.dart';
import 'package:neupod/ui/screens/browse/compilations_screen.dart';
import 'package:neupod/ui/screens/browse/cover_flow_screen.dart';
import 'package:neupod/ui/screens/browse/genres_screen.dart';
import 'package:neupod/ui/screens/browse/home_screen.dart';
import 'package:neupod/ui/screens/browse/music_menu_screen.dart';
import 'package:neupod/ui/screens/browse/settings_screen.dart';
import 'package:neupod/ui/screens/browse/songs_screen.dart';
import 'package:neupod/ui/style/neupod_style.dart';

import '../components/golden_harness.dart' show withShadows;
import 'screens_harness.dart';

const _styles = {
  'classic_light': (StyleFamily.classic, false),
  'modern_dark': (StyleFamily.modern, true),
};

Future<void> _golden(
  WidgetTester tester,
  String name,
  String style,
  String tier,
  WidgetBuilder home, {
  Future<void> Function(ShellRig rig)? act,
  bool playing = false,
  bool settleAfterAct = true,
}) async {
  final (family, dark) = _styles[style]!;
  await withShadows(() async {
    final song = (await gateLibrary.songs()).firstWhere(
      (s) => s.title == 'Salt Road',
    );
    final rig = await pumpShell(
      tester,
      home: home,
      family: family,
      dark: dark,
      scale: tierScales[tier]!,
      status: mockStatus,
      playing: playing ? playingState(song) : null,
    );
    if (act != null) {
      await act(rig);
      if (settleAfterAct) {
        await settle(tester);
      } else {
        await tester.pump();
        await tester.pump(const Duration(milliseconds: 16));
      }
    }
    await expectLater(
      find.byKey(const ValueKey('device')),
      matchesGoldenFile('goldens/$name.$style.$tier.png'),
    );
  });
}

void _each(
  String name,
  WidgetBuilder home, {
  List<String> tiers = const ['standard'],
  Future<void> Function(ShellRig rig)? act,
  bool playing = false,
  bool settleAfterAct = true,
}) {
  for (final style in _styles.keys) {
    for (final tier in tiers) {
      testWidgets(
        '$name, $style, $tier',
        (tester) => _golden(
          tester,
          name,
          style,
          tier,
          home,
          act: act,
          playing: playing,
          settleAfterAct: settleAfterAct,
        ),
      );
    }
  }
}

Future<Artist> _artist(String name) async =>
    (await gateLibrary.artists()).firstWhere((a) => a.name == name);

Future<Album> _album(String title) async =>
    (await gateLibrary.albums()).firstWhere((a) => a.title == title);

void main() {
  const all = ['standard', 'large', 'largest'];

  _each('home_playing', (_) => const HomeScreen(), tiers: all, playing: true);
  _each('home_idle', (_) => const HomeScreen());
  _each(
    'music_menu',
    (_) => const MusicMenuScreen(),
    act: (rig) async {
      rig.step(3); // Albums, as drawn
    },
  );
  _each(
    'songs',
    (_) => const SongsScreen(),
    tiers: all,
    act: (rig) async {
      rig.step(2);
    },
  );
  // A fast turn: the letter tile and the rail (they fade once the wheel
  // rests, so this one is taken straight after the turn).
  for (final style in _styles.keys) {
    testWidgets('songs_fast_scroll, $style, standard', (tester) async {
      final (family, dark) = _styles[style]!;
      await withShadows(() async {
        final rig = await pumpShell(
          tester,
          home: (_) => const SongsScreen(),
          family: family,
          dark: dark,
          status: mockStatus,
        );
        rig.step(8);
        await tester.pump();
        await tester.pump(const Duration(milliseconds: 16));
        await expectLater(
          find.byKey(const ValueKey('device')),
          matchesGoldenFile('goldens/songs_fast_scroll.$style.standard.png'),
        );
      });
    });
  }
  _each('artists', (_) => const ArtistsScreen());
  _each('albums', (_) => const AlbumsScreen());
  _each('genres', (_) => const GenresScreen());
  _each('compilations', (_) => const CompilationsScreen());
  _each('settings', (_) => const SettingsScreen());

  testWidgets('artist (All Songs, then albums in year order), classic', (
    tester,
  ) async {
    final a = await _artist('Pale Harbour');
    await _golden(
      tester,
      'artist',
      'classic_light',
      'standard',
      (_) => CollectionScreen.artist(a),
      act: (rig) async => rig.step(1),
    );
  });
  testWidgets('artist (All Songs, then albums in year order), modern', (
    tester,
  ) async {
    final a = await _artist('Pale Harbour');
    await _golden(
      tester,
      'artist',
      'modern_dark',
      'standard',
      (_) => CollectionScreen.artist(a),
      act: (rig) async => rig.step(1),
    );
  });

  for (final style in _styles.keys) {
    testWidgets('genre, $style', (tester) async {
      final g = (await gateLibrary.genres()).first;
      await _golden(
        tester,
        'genre',
        style,
        'standard',
        (_) => CollectionScreen.genre(g),
      );
    });
    testWidgets('album, one disc, $style', (tester) async {
      final a = await _album('Tidewater');
      await _golden(
        tester,
        'album',
        style,
        'standard',
        (_) => AlbumScreen(albumId: a.id),
        act: (rig) async => rig.step(1),
      );
    });
    testWidgets('album, two discs, $style', (tester) async {
      final a = (await gateLibrary.albums()).firstWhere(
        (a) => a.title == 'Greatest Hits' && a.artist == 'Zed Zone',
      );
      await _golden(
        tester,
        'album_discs',
        style,
        'standard',
        (_) => AlbumScreen(albumId: a.id),
        act: (rig) async => rig.step(2),
      );
    });
    testWidgets('album, greyed rows with reasons, $style', (tester) async {
      final a = await _album('2020 Damage');
      await _golden(
        tester,
        'album_greyed',
        style,
        'standard',
        (_) => AlbumScreen(albumId: a.id),
      );
    });
    testWidgets('album, compilation, $style', (tester) async {
      final a = await _album('Night Drive');
      await _golden(
        tester,
        'album_compilation',
        style,
        'standard',
        (_) => AlbumScreen(albumId: a.id),
      );
    });
    testWidgets('cover flow, $style', (tester) async {
      await _golden(
        tester,
        'cover_flow',
        style,
        'standard',
        (_) => const CoverFlowScreen(),
        act: (rig) async {
          rig.bus.emit(const WheelStep(3, 1));
        },
      );
    });
  }

  for (final tier in ['large', 'largest']) {
    testWidgets('album at the $tier tier, classic', (tester) async {
      final a = await _album('Tidewater');
      await _golden(
        tester,
        'album',
        'classic_light',
        tier,
        (_) => AlbumScreen(albumId: a.id),
      );
    });
    testWidgets('cover flow at the $tier tier, modern', (tester) async {
      await _golden(
        tester,
        'cover_flow',
        'modern_dark',
        tier,
        (_) => const CoverFlowScreen(),
      );
    });
  }
}
