// SPDX-License-Identifier: MPL-2.0

// The exact renders for the owner's design gate 2: every gate screen in the
// real app's device view at 393x852 (safe area 59 / 34, as an iPhone with a
// Dynamic Island), in Classic Light and Modern Dark, drawn at twice the
// point size. Run with
//
//   flutter test tool/gate2_shots/gate2_renders_test.dart \
//     --dart-define=GATE2_OUT=<output dir>
//
// (see README.md). The PNGs are written to GATE2_OUT (default
// build/gate2_renders); nothing is compared.
// Montserrat comes from the bundled asset (flutter_test_config.dart of the
// components' tests), as in the goldens.

import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/data/gate_library/manifest_library_view.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/browse/album_screen.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/collection_screen.dart';
import 'package:neupod/ui/screens/browse/cover_flow_screen.dart';
import 'package:neupod/ui/screens/browse/songs_screen.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_options.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';

import '../../test/fakes/fake_player.dart';
import 'gate2_app.dart';

const _out = String.fromEnvironment(
  'GATE2_OUT',
  defaultValue: 'build/gate2_renders',
);
const _boundary = ValueKey('gate2');

/// The gate library, read synchronously (the asset bundle's async load does
/// not complete reliably in widget tests).
final ManifestLibraryView _library = ManifestLibraryView.parse(
  File('assets/gate_library/manifest.json').readAsStringSync(),
);

/// The text tiers (Android's thresholds, the test platform's).
const _tiers = {'standard': 1.0, 'large': 1.2, 'largest': 1.4};

class _Rig {
  _Rig(this.tester, this.container, this.player);

  final WidgetTester tester;
  final ProviderContainer container;
  final FakePlayer player;

  WheelBus get bus => container.read(wheelBusProvider);

  Future<void> advance(int ms) => tester.pump(Duration(milliseconds: ms));

  Future<void> settle() async {
    // Not pumpAndSettle: a playing track keeps frames coming.
    await tester.pump();
    await tester.pump();
    for (var i = 0; i < 12; i++) {
      await tester.pump(const Duration(milliseconds: 40));
    }
  }

  Future<void> step(int n) async {
    bus.emit(WheelStep(n, 1));
    await tester.pump();
  }

  /// A press, or with [holdMs] a hold (as the wheel produces them).
  Future<void> press(WheelButton b, {int holdMs = 0}) async {
    bus.emit(WheelButtonDown(b));
    await tester.pump();
    if (holdMs > 0) {
      await advance(holdMs);
      bus
        ..emit(WheelHoldStart(b))
        ..emit(WheelButtonUp(b))
        ..emit(WheelHoldEnd(b));
    } else {
      bus
        ..emit(WheelButtonUp(b))
        ..emit(WheelPress(b));
    }
    await tester.pump();
    await settle();
  }

  void push(String name, WidgetBuilder b) => gate2Push(container, name, b);

  Future<void> open(ShellDestination d) async {
    container.read(shellProvider.notifier).open(d);
    await settle();
  }

  /// Writes the capture of the whole device view.
  Future<void> shot(String file) async {
    await tester.runAsync(() async {
      final boundary = tester.renderObject<RenderRepaintBoundary>(
        find.byKey(_boundary),
      );
      final image = await boundary.toImage(pixelRatio: 2);
      final data = await image.toByteData(format: ui.ImageByteFormat.png);
      final f = File('$_out/$file.png')..createSync(recursive: true);
      f.writeAsBytesSync(data!.buffer.asUint8List());
    });
  }
}

void main() {
  for (final style in gate2Styles.keys) {
    void scene(
      String name,
      Future<void> Function(_Rig rig) act, {
      bool playing = false,
      String tier = 'standard',
      void Function(ProviderContainer c)? preset,
    }) {
      final suffix = tier == 'standard' ? '' : '-$tier';
      testWidgets('$name, $style, $tier', (tester) async {
        tester.view
          ..devicePixelRatio = 2
          ..physicalSize = const Size(393 * 2, 852 * 2);
        final pad = const FakeViewPadding(top: 59 * 2, bottom: 34 * 2);
        tester.view
          ..padding = pad
          ..viewPadding = pad;
        tester.platformDispatcher.textScaleFactorTestValue = _tiers[tier]!;
        addTearDown(tester.view.reset);
        addTearDown(tester.platformDispatcher.clearAllTestValues);
        final player = FakePlayer();
        final container = gate2Container(
          style: style,
          player: player,
          overrides: [
            libraryViewProvider.overrideWith((ref) => _library),
            wheelClockProvider.overrideWithValue(
              () => tester.binding.clock.now().millisecondsSinceEpoch,
            ),
          ],
        );
        addTearDown(container.dispose);
        preset?.call(container);
        await tester.pumpWidget(gate2Widget(container, _boundary));
        if (playing) {
          await tester.pump();
          final song = (await _library.songs()).firstWhere(
            (s) => s.title == 'Salt Road',
          );
          player.state = gate2Playing(song);
        }
        final rig = _Rig(tester, container, player);
        await rig.settle();
        await act(rig);
        await rig.shot('render-$name-$style$suffix');
      });
    }

    Future<Album> album(_Rig rig, String title, [String? artist]) async {
      final lib = _library;
      return (await lib.albums()).firstWhere(
        (a) => a.title == title && (artist == null || a.artist == artist),
      );
    }

    scene('home-idle', (rig) async {});
    scene('home-playing', (rig) async {}, playing: true);
    for (final t in ['large', 'largest']) {
      scene('home-playing', (rig) async {}, playing: true, tier: t);
    }
    scene('music-menu', (rig) async {
      await rig.press(WheelButton.centre); // Home's Music
      await rig.step(3); // Albums, as drawn
      await rig.settle();
    });
    for (final t in _tiers.keys) {
      scene('songs', (rig) async {
        rig.push('Songs', (_) => const SongsScreen());
        await rig.settle();
        await rig.step(2);
        await rig.settle();
      }, tier: t);
    }
    // A fast turn: the letter tile and the rail (they fade once the wheel
    // rests, so this is taken straight after the turn).
    scene('songs-fast-scroll', (rig) async {
      rig.push('Songs', (_) => const SongsScreen());
      await rig.settle();
      await rig.step(8);
      await rig.advance(16);
    });
    scene('artist', (rig) async {
      final lib = _library;
      final a = (await lib.artists()).firstWhere(
        (a) => a.name == 'Pale Harbour',
      );
      rig.push('Artist', (_) => CollectionScreen.artist(a));
      await rig.settle();
      await rig.step(1);
      await rig.settle();
    });
    scene('album-discs', (rig) async {
      final a = await album(rig, 'Greatest Hits', 'Zed Zone');
      rig.push('Album', (_) => AlbumScreen(albumId: a.id));
      await rig.settle();
      await rig.step(2);
      await rig.settle();
    });
    scene('cover-flow', (rig) async {
      rig.push('Cover Flow', (_) => const CoverFlowScreen());
      await rig.settle();
      await rig.step(3);
      await rig.settle();
    });
    scene('now-playing', (rig) async {
      await rig.open(ShellDestination.nowPlaying);
    }, playing: true);
    scene(
      'now-playing-stars',
      (rig) async {
        await rig.open(ShellDestination.nowPlaying);
      },
      playing: true,
      preset: (c) => c
          .read(ratingPlacementProvider.notifier)
          .set(RatingPlacement.belowFormat),
    );
    scene('now-playing-volume', (rig) async {
      await rig.open(ShellDestination.nowPlaying);
      await rig.step(3);
      await rig.advance(100);
      await rig.advance(100);
    }, playing: true);
    scene('now-playing-seek', (rig) async {
      await rig.open(ShellDestination.nowPlaying);
      await rig.press(WheelButton.centre, holdMs: 520);
      await rig.step(6);
      await rig.advance(100);
    }, playing: true);
    scene('now-playing-page2', (rig) async {
      await rig.open(ShellDestination.nowPlaying);
      await rig.press(WheelButton.centre);
    }, playing: true);
    scene('panel', (rig) async {
      await rig.press(WheelButton.centre);
      unawaited(
        rig.container
            .read(shellProvider.notifier)
            .showPanelRequest(const PanelRequest.libraryUpdateStopped()),
      );
      await rig.settle();
    });
    Future<void> typeSal(_Rig rig) async {
      await rig.open(ShellDestination.search);
      var at = 0;
      for (final ch in 'sal'.codeUnits) {
        final to = ch - 0x61;
        await rig.step(to - at);
        at = to;
        await rig.press(WheelButton.centre);
      }
      await rig.advance(300);
      await rig.settle();
    }

    scene('search-typing', typeSal);
    scene('search-results', (rig) async {
      await typeSal(rig);
      await rig.press(WheelButton.playPause);
    });
    scene('empty-list', (rig) async {
      rig.push(
        'empty',
        (_) => ShellFrame(
          title: 'Compilations',
          body: BrowseListBody(child: NeupodList(items: const [])),
        ),
      );
      await rig.settle();
    });
  }
}
