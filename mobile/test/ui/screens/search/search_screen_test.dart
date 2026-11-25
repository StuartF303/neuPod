// SPDX-License-Identifier: MPL-2.0

// Search wired to the wheel: the letter ring, the chips, the live results, the
// status-bar count, the Menu hold, and the results phase. The model's rules
// are also run by the spec suite (test/spec/wheel_text_entry_spec_test.dart).

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/components/search_entry.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/screens/search/search_screen.dart';
import 'package:neupod/ui/screens/search/search_session.dart';

import '../../../fakes/fake_player.dart';
import '../screens_harness.dart';

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
  _Rig(this.tester, this.rig);
  final WidgetTester tester;
  final ShellRig rig;
  final List<SearchHit> opened = [];

  ProviderContainer get container => rig.container;
  FakePlayer get player => rig.player;
  RecordedHaptics get haptics => rig.haptics;
  WheelBus get bus => rig.bus;
  bool get onScreen => find.byType(SearchScreen).evaluate().isNotEmpty;

  Future<void> press(WheelButton b) async {
    bus.press(b);
    await tester.pump();
  }

  Future<void> turn(int steps) async {
    bus.emit(WheelStep(steps, 1));
    await tester.pump();
  }
}

/// Search pushed on a stand-in Home in the shell, over the test's own
/// library function ([library] false: the gate library's).
Future<_Rig> _pump(
  WidgetTester tester, {
  String initial = '',
  bool library = false,
  bool pushed = true,
  PlayerState? playing,
  Future<SearchFunction> Function()? loader,
}) async {
  late final _Rig holder;
  final shell = await pumpShell(
    tester,
    playing: playing,
    home: (_) => pushed
        ? const SizedBox()
        : SearchScreen(
            initialText: initial,
            onOpen: (h) => holder.opened.add(h),
          ),
    overrides: [
      if (loader != null)
        searchFunctionProvider.overrideWith((ref) => loader())
      else if (!library)
        searchFunctionProvider.overrideWith((ref) async => _find),
    ],
  );
  holder = _Rig(tester, shell);
  if (pushed) {
    shell.container
        .read(shellProvider.notifier)
        .push(
          ShellPage(
            name: 'search',
            builder: (_) => SearchScreen(
              initialText: initial,
              onOpen: (h) => holder.opened.add(h),
            ),
          ),
        );
  }
  await settle(tester);
  return holder;
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
    expect(rig.onScreen, isTrue);
    // Hold Menu: the wheel's WheelHoldStart clears.
    rig.bus.emit(const WheelButtonDown(WheelButton.menu));
    rig.bus.emit(const WheelHoldStart(WheelButton.menu));
    rig.bus.emit(const WheelButtonUp(WheelButton.menu));
    rig.bus.emit(const WheelHoldEnd(WheelButton.menu));
    await tester.pump();
    expect(find.byType(SearchResultRow), findsNothing);
    await rig.press(WheelButton.menu);
    await settle(tester);
    expect(rig.onScreen, isFalse, reason: 'the shell went back');
  });

  testWidgets('Play/Pause goes to the results; the wheel scrolls them; the '
      'centre opens a row; Menu returns to typing with the text kept', (
    tester,
  ) async {
    final rig = await _pump(
      tester,
      initial: 'ab',
      playing: const PlayerState(
        status: PlaybackStatus.ready,
        queue: [QueueItem(id: 'x', uri: 'file:///x')],
        index: 0,
      ),
    );
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
    // Back in typing Play/Pause goes to the results again; there it is the
    // shell's play or pause.
    await rig.press(WheelButton.playPause);
    expect(rig.player.calls, isEmpty);
    await rig.press(WheelButton.playPause);
    expect(rig.player.calls, ['play']);
  });

  testWidgets('a panel on top stops Search acting on the wheel', (
    tester,
  ) async {
    final rig = await _pump(tester);
    unawaited(
      rig.container
          .read(shellProvider.notifier)
          .showPanel(title: 'Lost', message: 'Something'),
    );
    await settle(tester);
    await rig.press(WheelButton.next);
    await rig.turn(3);
    expect(find.byType(SearchResultRow), findsNothing);
    await rig.press(WheelButton.centre); // dismisses; not a typed glyph
    await settle(tester);
    expect(find.text('Lost'), findsNothing);
    await rig.press(WheelButton.centre);
    expect(find.text('a'), findsWidgets);
  });

  testWidgets('a screen over Search does not get the wheel', (tester) async {
    final rig = await _pump(tester);
    rig.container
        .read(shellProvider.notifier)
        .push(ShellPage(name: 'over', builder: (_) => const SizedBox()));
    await settle(tester);
    expect(rig.bus.holdMs, isEmpty);
    await rig.press(WheelButton.centre);
    rig.container.read(shellProvider.notifier).pop();
    await settle(tester);
    expect(rig.bus.holdMs[WheelButton.menu], 600);
    expect(
      find.byType(SearchResultRow),
      findsNothing,
      reason: 'nothing was typed',
    );
  });

  testWidgets('restores the buttons holds when it goes', (tester) async {
    final rig = await _pump(tester);
    rig.container.read(shellProvider.notifier).pop();
    await settle(tester);
    expect(rig.bus.holdMs, isEmpty);
  });

  testWidgets('the wheel says which chip and glyph; the results say Results', (
    tester,
  ) async {
    final rig = await _pump(tester, initial: 'ab');
    expect(rig.bus.semantics.value.label, 'Letter ring');
    expect(rig.bus.semantics.value.value, 'abc, a');
    await rig.press(WheelButton.next);
    expect(rig.bus.semantics.value.value, 'ABC, A');
    await rig.press(WheelButton.playPause);
    expect(rig.bus.semantics.value.label, 'Results');
  });

  testWidgets('at the 24-character cap a typed glyph is refused with the '
      'end-of-list haptic', (tester) async {
    final rig = await _pump(tester, initial: 'a' * maxLength);
    await rig.press(WheelButton.centre);
    expect(find.text('a' * maxLength), findsOneWidget);
    expect(rig.haptics.played, isNotEmpty);
    expect(rig.haptics.played.last, FeedbackKind.endReached);
  });

  testWidgets('Menu on an empty field with nowhere to go does nothing', (
    tester,
  ) async {
    final rig = await _pump(tester, pushed: false);
    await rig.press(WheelButton.menu);
    expect(find.byType(LetterRing), findsOneWidget);
    await rig.press(WheelButton.centre);
    expect(find.text('a'), findsWidgets);
  });

  testWidgets('the library search: names starting with the text first, '
      'grouped, from the gate library', (tester) async {
    final rig = await _pump(tester, library: true);
    final fn = await rig.container.read(searchFunctionProvider.future);
    final r = fn('s');
    expect(r.count, greaterThan(0));
    final songs = [for (final h in r.songs) h.title.toLowerCase()];
    final firstWithin = songs.indexWhere((t) => !t.startsWith('s'));
    if (firstWithin >= 0) {
      expect(songs.take(firstWithin).every((t) => t.startsWith('s')), isTrue);
      expect(songs.skip(firstWithin).any((t) => t.startsWith('s')), isFalse);
    }
    expect(fn('').count, 0);
  });

  testWidgets('a song that cannot play is greyed in the results with its '
      'reason, and opening it plays nothing', (tester) async {
    final rig = await _pump(tester, library: true, initial: '04 Garb');
    final fn = await rig.container.read(searchFunctionProvider.future);
    final hit = fn('04 garb').songs.single;
    expect(hit.reason, 'Damaged file');
    expect(hit.label, 'Damaged file');
    // The live result row: greyed, the reason at the right.
    expect(find.text('Damaged file'), findsOneWidget);
    final row = tester.widget<SearchResultRow>(find.byType(SearchResultRow));
    expect(row.greyed, isTrue);
  });

  testWidgets('while the library loads, and when it cannot be read, Search '
      'says so instead of looking like no matches', (tester) async {
    final gate = Completer<SearchFunction>();
    final rig = await _pump(tester, loader: () => gate.future);
    expect(find.text('Loading library'), findsOneWidget);
    await rig.press(WheelButton.centre);
    expect(find.text('Loading library'), findsOneWidget);
    expect(find.textContaining('found'), findsNothing);
    gate.complete(_find);
    await settle(tester);
    expect(find.text('Loading library'), findsNothing);
    expect(find.text('Abba'), findsOneWidget, reason: 'searched on arrival');

    final rig2 = await _pump(tester, loader: () async => throw StateError('x'));
    expect(find.text('Library unavailable'), findsOneWidget);
    expect(find.text('No matches'), findsNothing);
    expect(rig2.onScreen, isTrue);
  });

  testWidgets('a search that matches nothing says No matches', (tester) async {
    final rig = await _pump(tester);
    await rig.turn(16); // q
    await rig.press(WheelButton.centre);
    expect(find.text('No matches'), findsOneWidget);
  });

  testWidgets('the results build only the rows in view, however many match', (
    tester,
  ) async {
    final many = SearchResults(
      songs: [
        for (var i = 0; i < 5000; i++)
          SearchHit(
            kind: SearchKind.song,
            id: 's$i',
            title: 'Song $i',
            detail: 'Artist · Album',
          ),
      ],
    );
    final rig = await _pump(
      tester,
      initial: 'song',
      loader: () async =>
          (q) => many,
    );
    await rig.press(WheelButton.playPause);
    expect(find.byType(NeupodRow).evaluate().length, lessThan(15));
    await rig.turn(2400);
    expect(find.byType(NeupodRow).evaluate().length, lessThan(15));
    expect(find.text('Song 2400'), findsOneWidget);
    expect(find.text('Song 0'), findsNothing);
  });
}
