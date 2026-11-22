// SPDX-License-Identifier: MPL-2.0

// Two screens assembled from the components inside NeupodScreen's 600x450
// canvas, to compare with the UI styles mock-ups (`classic-songs.png`,
// `modern-now-playing.png` ...). Not the screens themselves: those are built
// by the screen items, and Now Playing's layout at the larger tiers is theirs.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/ui/components/art_frame.dart';
import 'package:neupod/ui/components/bars.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/style/backdrop.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';
import 'package:neupod/ui/style/text_tier.dart';

import 'golden_harness.dart';

const _songs = [
  ('Carrier Wave', 'Beacon · Signal'),
  ('Café Del Mar', 'Aurora Lane · First Light'),
  ('Ember', 'The Cinders · Now 7'),
  ('Harbour Lights', 'Aurora Lane · First Light'),
  ('Hit A', 'Zed Zed · Hit A'),
];

Future<void> _pumpScreen(
  WidgetTester tester,
  NeupodStyle style,
  TextTier tier,
  BackdropLayout backdrop,
  Widget child,
) async {
  debugDisableShadows = false;
  tester.view.physicalSize = neupodScreenSize;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  await tester.pumpWidget(
    Directionality(
      textDirection: TextDirection.ltr,
      child: RepaintBoundary(
        key: const ValueKey('screen'),
        child: NeupodScreen(
          style: style,
          tier: tier,
          backdrop: backdrop,
          child: child,
        ),
      ),
    ),
  );
}

class _Songs extends StatelessWidget {
  const _Songs();

  @override
  Widget build(BuildContext context) {
    final tier = NeupodTheme.of(context).tier;
    final n = tier.rowsTwoLine;
    final selected = n > 3 ? 3 : n - 1;
    return NeupodScaffold(
      statusBar: const StatusBar(
        title: 'Songs',
        playState: PlayState.playing,
        context: '1,204',
        bluetooth: BluetoothIcon.saved,
        output: OutputIcon.headphones,
        battery: BatteryState(level: 0.78),
      ),
      body: ListGround(
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Expanded(
              child: Column(
                children: [
                  for (var i = 0; i < n; i++)
                    NeupodRow(
                      title: _songs[i].$1,
                      subtitle: _songs[i].$2,
                      selection: i == selected
                          ? RowSelection.selected
                          : RowSelection.none,
                      position: '${i + 1} of 1,204',
                    ),
                ],
              ),
            ),
            const NeupodScrollBar(visible: 0.17, position: 0.45),
          ],
        ),
      ),
    );
  }
}

class _NowPlaying extends StatelessWidget {
  const _NowPlaying();

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final type = NeupodType(theme.tier);
    final t = theme.style.tokens;
    return NeupodScaffold(
      statusBar: const StatusBar(
        title: 'Now Playing',
        playState: PlayState.playing,
        repeat: RepeatState.one,
        context: '3 of 12',
        contextLabel: 'track 3 of 12',
        bluetooth: BluetoothIcon.saved,
        output: OutputIcon.headphones,
        battery: BatteryState(level: 0.78),
      ),
      body: Padding(
        padding: const EdgeInsets.fromLTRB(19, 22, 19, 0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            SizedBox(
              height: 270,
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const ArtFrame(
                    size: 210,
                    use: ArtUse.nowPlaying,
                    art: TestArt.harbour,
                    reflection: true,
                  ),
                  const SizedBox(width: 22),
                  Expanded(
                    child: SizedBox(
                      height: 210,
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          NeupodText('Harbour Lights', type.large(t.ink)),
                          const SizedBox(height: 6),
                          NeupodText('Aurora Lane', type.body(t.ink2)),
                          const SizedBox(height: 4),
                          NeupodText('First Light · 2019', type.small(t.ink2)),
                          const Spacer(),
                          NeupodText('FLAC 16/44.1', type.small(t.ink3)),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 22),
            const NeupodProgressBar(value: 0.38),
            const SizedBox(height: 8),
            const ProgressTimes(
              elapsed: '1:32',
              remaining: '-2:29',
              middle: 'repeat 1',
            ),
          ],
        ),
      ),
    );
  }
}

void main() {
  for (final MapEntry(key: name, value: style) in designedStyles.entries) {
    for (final tier in TextTier.values) {
      testWidgets('songs, $name, ${tier.name}', (tester) async {
        await _pumpScreen(
          tester,
          style,
          tier,
          BackdropLayout.lists,
          const _Songs(),
        );
        await expectGoldenWithShadows(
          tester,
          find.byKey(const ValueKey('screen')),
          'goldens/screen_songs.$name.${tier.name}.png',
        );
      });
    }
    testWidgets('now playing, $name, standard', (tester) async {
      await _pumpScreen(
        tester,
        style,
        TextTier.standard,
        BackdropLayout.nowPlaying,
        const _NowPlaying(),
      );
      await expectGoldenWithShadows(
        tester,
        find.byKey(const ValueKey('screen')),
        'goldens/screen_now_playing.$name.standard.png',
      );
    });
  }

  testWidgets('the canvas ignores the phone text scaler', (tester) async {
    tester.view.physicalSize = const Size(1200, 900);
    tester.view.devicePixelRatio = 2;
    addTearDown(tester.view.reset);
    late double scale;
    late TextTier seen;
    await tester.pumpWidget(
      MediaQuery(
        data: const MediaQueryData(textScaler: TextScaler.linear(1.4)),
        child: Directionality(
          textDirection: TextDirection.ltr,
          child: NeupodScreen(
            style: designedStyles['classic_light']!,
            child: Builder(
              builder: (context) {
                scale = MediaQuery.textScalerOf(context).scale(10) / 10;
                seen = NeupodTheme.of(context).tier;
                return const SizedBox();
              },
            ),
          ),
        ),
      ),
    );
    expect(scale, 1);
    expect(seen, TextTier.largest);
  });
}
