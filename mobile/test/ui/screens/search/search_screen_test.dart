// SPDX-License-Identifier: MPL-2.0

// Search wired to the wheel: the letter ring, the chips, the live results, the
// status-bar count, the Menu hold, and the results phase. The model's rules
// are also run by the spec suite (test/spec/wheel_text_entry_spec_test.dart).

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/search_entry.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/panel/panel_layer.dart';
import 'package:neupod/ui/screens/search/search_screen.dart';
import 'package:neupod/ui/screens/search/search_session.dart';

import '../../../fakes/fake_player.dart';
import '../../device/device_test_harness.dart';

const _library = [
  SearchHit(kind: SearchKind.artist, id: 'a', title: 'Abba'),
  SearchHit(
    kind: SearchKind.album,
    id: 'b',
    title: 'Abbey Road',
    detail: 'The Beatles',
    year: 1969,
  ),
  SearchHit(
    kind: SearchKind.song,
    id: 's1',
    title: 'Absolution',
    detail: 'Muse · Absolution',
  ),
  SearchHit(
    kind: SearchKind.song,
    id: 's2',
    title: 'Ablaze',
    detail: 'Muse · Drones',
  ),
  SearchHit(kind: SearchKind.song, id: 's3', title: 'Bark', detail: 'Dog'),
];

SearchResults _find(String q) {
  if (q.isEmpty) return SearchResults.none;
  final hits = [
    for (final h in _library)
      if (h.title.toLowerCase().contains(q.toLowerCase())) h,
  ];
  return SearchResults(
    artists: [
      for (final h in hits)
        if (h.kind == SearchKind.artist) h,
    ],
    albums: [
      for (final h in hits)
        if (h.kind == SearchKind.album) h,
    ],
    songs: [
      for (final h in hits)
        if (h.kind == SearchKind.song) h,
    ],
  );
}

class _Rig {
  _Rig(this.tester, this.container, this.player);
  final WidgetTester tester;
  final ProviderContainer container;
  final FakePlayer player;
  int backs = 0;
  final List<SearchHit> opened = [];

  WheelBus get bus => container.read(wheelBusProvider);

  Future<void> press(WheelButton b) async {
    bus.press(b);
    await tester.pump();
  }

  Future<void> turn(int steps) async {
    bus.emit(WheelStep(steps, 1));
    await tester.pump();
  }
}

Future<_Rig> _pump(WidgetTester tester, {String initial = ''}) async {
  tester.view.physicalSize = const Size(600, 450);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final player = FakePlayer();
  final container = ProviderContainer.test(
    overrides: [
      playerProvider.overrideWithValue(player),
      searchFunctionProvider.overrideWithValue(_find),
      hapticsPortProvider.overrideWithValue(RecordingHaptics()),
    ],
  );
  final rig = _Rig(tester, container, player);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: Directionality(
        textDirection: TextDirection.ltr,
        child: MediaQuery(
          data: const MediaQueryData(size: Size(600, 450)),
          child: SearchScreen(
            initialText: initial,
            onBack: () => rig.backs++,
            onOpen: rig.opened.add,
          ),
        ),
      ),
    ),
  );
  await tester.pump();
  return rig;
}

void main() {
  testWidgets('opens typing on the lower-case ring, nothing found yet', (
    tester,
  ) async {
    final rig = await _pump(tester);
    expect(find.byType(LetterRing), findsOneWidget);
    expect(find.byType(ModeChips), findsOneWidget);
    expect(
      find.text('Select: type · Menu: delete · Play: results'),
      findsOneWidget,
    );
    expect(find.text('abc'), findsOneWidget);
    expect(find.text('#!?'), findsOneWidget, reason: 'six chips, incl. #!?');
    expect(find.text('space'), findsOneWidget);
    expect(find.text('del'), findsOneWidget);
    expect(rig.bus.holdMs[WheelButton.menu], 600);
    expect(rig.bus.atRoot.value, isFalse);
  });

  testWidgets('the wheel turns the ring and the centre types; live results '
      'and the count follow', (tester) async {
    final rig = await _pump(tester);
    await rig.press(WheelButton.centre); // a
    await rig.turn(1);
    await rig.press(WheelButton.centre); // ab
    expect(find.text('ab'), findsOneWidget);
    expect(find.text('Abba'), findsOneWidget);
    expect(find.text('Artist'), findsOneWidget);
    expect(find.text('Album · 1969'), findsOneWidget);
    expect(find.text('4 found'), findsOneWidget, reason: 'status bar count');
    // At most three live results, though four match.
    expect(find.byType(SearchResultRow), findsNWidgets(3));
  });

  testWidgets('Previous and Next walk the chips; Menu deletes; Menu held '
      'clears; Menu on empty leaves', (tester) async {
    final rig = await _pump(tester);
    await rig.press(WheelButton.next); // ABC
    await rig.press(WheelButton.centre);
    expect(find.text('A'), findsWidgets);
    await rig.press(WheelButton.centre);
    await rig.press(WheelButton.menu);
    expect(rig.backs, 0);
    // Hold Menu: the wheel's WheelHoldStart clears.
    rig.bus.emit(const WheelButtonDown(WheelButton.menu));
    rig.bus.emit(const WheelHoldStart(WheelButton.menu));
    rig.bus.emit(const WheelButtonUp(WheelButton.menu));
    rig.bus.emit(const WheelHoldEnd(WheelButton.menu));
    await tester.pump();
    expect(find.byType(SearchResultRow), findsNothing);
    await rig.press(WheelButton.menu);
    expect(rig.backs, 1);
  });

  testWidgets('Play/Pause goes to the results; the wheel scrolls them; the '
      'centre opens a row; Menu returns to typing with the text kept', (
    tester,
  ) async {
    final rig = await _pump(tester, initial: 'ab');
    await rig.press(WheelButton.playPause);
    expect(rig.player.calls, isEmpty, reason: 'not play or pause');
    expect(find.text('Artists'), findsOneWidget);
    expect(find.text('Albums'), findsOneWidget);
    expect(find.text('Songs'), findsOneWidget);
    await rig.turn(2);
    expect(rig.bus.semantics.value.value, contains('Absolution'));
    await rig.press(WheelButton.centre);
    expect(rig.opened.single.id, 's1');
    await rig.press(WheelButton.menu);
    expect(find.byType(LetterRing), findsOneWidget);
    expect(find.text('ab'), findsOneWidget);
    // Back in the results, Play/Pause plays or pauses.
    await rig.press(WheelButton.playPause);
    await rig.press(WheelButton.playPause);
    expect(rig.player.calls, ['play']);
  });

  testWidgets('a panel on top stops Search acting on the wheel', (
    tester,
  ) async {
    final rig = await _pump(tester);
    // Search ignores the wheel while a panel is up.
    // (The panel layer is tested in panel_layer_test.dart.)
    rig.container
        .read(panelProvider.notifier)
        .show(const PanelRequest(title: 'Lost', message: 'Something'));
    await rig.press(WheelButton.centre);
    rig.container.read(panelProvider.notifier).dismiss();
    await tester.pump();
    expect(find.byType(SearchResultRow), findsNothing);
  });

  testWidgets('restores the buttons holds when it goes', (tester) async {
    final rig = await _pump(tester);
    await tester.pumpWidget(const SizedBox());
    expect(rig.bus.holdMs, isEmpty);
  });
}
