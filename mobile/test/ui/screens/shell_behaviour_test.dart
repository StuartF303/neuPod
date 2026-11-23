// SPDX-License-Identifier: MPL-2.0

// The shell and the browsing screens' behaviour, from the contract in
// lib/ui/device/README.md and the specification's Browsing section.

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/screens/browse/album_screen.dart';
import 'package:neupod/ui/screens/browse/cover_flow_screen.dart';
import 'package:neupod/ui/screens/browse/home_screen.dart';
import 'package:neupod/ui/screens/browse/songs_screen.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';

import 'screens_harness.dart';

Finder _rowText(String text) =>
    find.descendant(of: find.byType(NeupodRow), matching: find.text(text));

String _wheelValue(ShellRig rig) => rig.bus.semantics.value.value;

Future<void> _pressCentre(WidgetTester tester, ShellRig rig) async {
  rig.press(WheelButton.centre);
  await settle(tester);
}

void main() {
  group('Home', () {
    testWidgets('lists the specification\'s items; Now Playing only when '
        'something is loaded', (tester) async {
      await pumpShell(tester, home: (_) => const HomeScreen());
      expect(_rowText('Music'), findsOneWidget);
      expect(_rowText('Playlists'), findsOneWidget);
      expect(_rowText('Settings'), findsOneWidget);
      expect(_rowText('Shuffle Songs'), findsOneWidget);
      expect(_rowText('Now Playing'), findsNothing);
      expect(find.text('Nothing playing'), findsOneWidget);
    });

    testWidgets('Now Playing appears while a song is loaded', (tester) async {
      final song = (await gateLibrary.songs()).first;
      await pumpShell(
        tester,
        home: (_) => const HomeScreen(),
        playing: playingState(song),
      );
      expect(_rowText('Now Playing'), findsOneWidget);
      expect(find.text(song.title), findsOneWidget);
    });

    testWidgets('the wheel moves the selection and the screen reader hears '
        'the row', (tester) async {
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      expect(_wheelValue(rig), 'Music, 1 of 4');
      rig.step(2);
      await settle(tester);
      expect(_wheelValue(rig), 'Settings, 3 of 4');
      expect(rig.bus.semantics.value.increasedValue, 'Shuffle Songs, 4 of 4');
      expect(rig.bus.semantics.value.decreasedValue, 'Playlists, 2 of 4');
      expect(rig.haptics.played, contains(FeedbackKind.detent));
    });

    testWidgets('a push past the end gives the end haptic once', (
      tester,
    ) async {
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      rig.step(10);
      rig.step(1);
      rig.step(1);
      expect(
        rig.haptics.played.where((k) => k == FeedbackKind.endReached),
        hasLength(1),
      );
    });
  });

  group('Cover Flow', () {
    testWidgets('a tap on a side cover centres it, near or far', (
      tester,
    ) async {
      final rig = await pumpShell(tester, home: (_) => const CoverFlowScreen());
      final first = _wheelValue(rig);
      expect(first, contains('1 of'));
      // Classic: the far right cover is centred 319 px right of the middle,
      // half off screen.
      await tester.tapAt(const Offset(595, 200));
      await settle(tester);
      expect(_wheelValue(rig), contains('4 of'));
      // The first cover on the left of the new centre.
      await tester.tapAt(const Offset(300 - 153, 200));
      await settle(tester);
      expect(_wheelValue(rig), contains('3 of'));
    });
  });

  group('Cover Flow centre and Modern', () {
    for (final modern in [false, true]) {
      final name = modern ? 'Modern' : 'Classic';
      testWidgets('$name: a tap on the centre cover opens its album', (
        tester,
      ) async {
        final rig = await pumpShell(
          tester,
          home: (_) => const CoverFlowScreen(),
          family: modern ? StyleFamily.modern : StyleFamily.classic,
          dark: modern,
        );
        expect(rig.container.read(shellProvider).pages, hasLength(1));
        await tester.tapAt(const Offset(300, 200));
        await settle(tester);
        expect(rig.container.read(shellProvider).pages, hasLength(2));
      });
    }

    testWidgets('Modern: a tap on a side cover centres it', (tester) async {
      final rig = await pumpShell(
        tester,
        home: (_) => const CoverFlowScreen(),
        family: StyleFamily.modern,
        dark: true,
      );
      expect(_wheelValue(rig), contains('1 of'));
      await tester.tapAt(const Offset(300 + 284, 200)); // two covers right
      await settle(tester);
      expect(_wheelValue(rig), contains('3 of'));
      expect(rig.container.read(shellProvider).pages, hasLength(1));
    });
  });

  group('the stack', () {
    testWidgets('Select opens Music, Menu goes back, Menu at Home does '
        'nothing', (tester) async {
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      expect(rig.bus.atRoot.value, isTrue);
      await _pressCentre(tester, rig);
      expect(find.text('Cover Flow'), findsWidgets);
      expect(rig.container.read(shellProvider).pages, hasLength(2));
      expect(rig.bus.atRoot.value, isFalse);

      rig.press(WheelButton.menu);
      await settle(tester);
      expect(rig.container.read(shellProvider).pages, hasLength(1));
      expect(rig.bus.atRoot.value, isTrue);

      rig.press(WheelButton.menu);
      await settle(tester);
      expect(rig.container.read(shellProvider).pages, hasLength(1));
    });

    testWidgets('a screen keeps its selection while another is on top', (
      tester,
    ) async {
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      rig.step(2); // Settings
      await _pressCentre(tester, rig); // opens Settings
      rig.press(WheelButton.menu);
      await settle(tester);
      expect(_wheelValue(rig), 'Settings, 3 of 4');
    });

    testWidgets('events reach only the top screen', (tester) async {
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      await _pressCentre(tester, rig); // Music on top
      rig.step(1);
      await settle(tester);
      rig.press(WheelButton.menu);
      await settle(tester);
      // Home never moved.
      expect(_wheelValue(rig), 'Music, 1 of 4');
    });

    testWidgets('a push slides in over 80 ms; under Reduce Motion it does '
        'not', (tester) async {
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      rig.press(WheelButton.centre);
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 20));
      final slidingX = tester.getTopLeft(find.text('Cover Flow').first).dx;
      expect(slidingX, greaterThan(100), reason: 'still sliding in');
      await tester.pumpAndSettle();
      expect(tester.getTopLeft(find.text('Cover Flow').first).dx, lessThan(60));

      // Reduce Motion: there at once.
      final rig2 = await pumpShell(
        tester,
        home: (_) => const HomeScreen(),
        reduceMotion: true,
      );
      rig2.press(WheelButton.centre);
      await tester.pump();
      await tester.pump();
      expect(tester.getTopLeft(find.text('Cover Flow').first).dx, lessThan(60));
    });

    testWidgets('the registry supplies Now Playing and opens it; without one a '
        'stand-in opens', (tester) async {
      final song = (await gateLibrary.songs()).first;
      final rig = await pumpShell(
        tester,
        home: (_) => const HomeScreen(),
        playing: playingState(song),
        registry: (c) => c
            .read(shellRegistryProvider)
            .register(
              ShellDestination.nowPlaying,
              (_) => const Text('REAL NOW PLAYING'),
            ),
      );
      rig.step(4); // Now Playing is the fifth item
      await _pressCentre(tester, rig);
      expect(find.text('REAL NOW PLAYING'), findsOneWidget);

      final rig2 = await pumpShell(
        tester,
        home: (_) => const HomeScreen(),
        playing: playingState(song),
      );
      rig2.step(4);
      await _pressCentre(tester, rig2);
      expect(find.text('Not built yet'), findsOneWidget);
    });
  });

  group('the panel', () {
    testWidgets('shows over the screen; only Select or Menu dismiss it', (
      tester,
    ) async {
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      var done = false;
      unawaited(
        rig.container
            .read(shellProvider.notifier)
            .showPanel(title: 'Card is full', message: 'Delete something.')
            .then((_) => done = true),
      );
      await settle(tester);
      expect(find.text('Card is full'), findsOneWidget);
      expect(find.text('OK'), findsOneWidget);
      expect(rig.bus.atRoot.value, isFalse);

      rig.step(1); // swallowed
      rig.press(WheelButton.next);
      await settle(tester);
      expect(find.text('Card is full'), findsOneWidget);

      rig.press(WheelButton.centre);
      await settle(tester);
      expect(find.text('Card is full'), findsNothing);
      expect(done, isTrue);
      expect(rig.bus.atRoot.value, isTrue);
      expect(_wheelValue(rig), 'Music, 1 of 4');

      unawaited(
        rig.container
            .read(shellProvider.notifier)
            .showPanel(title: 'Again', message: 'x'),
      );
      await settle(tester);
      rig.press(WheelButton.menu);
      await settle(tester);
      expect(find.text('Again'), findsNothing);
    });

    testWidgets('a tap on OK dismisses it', (tester) async {
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      unawaited(
        rig.container
            .read(shellProvider.notifier)
            .showPanel(title: 'Battery empty', message: 'Charge me.'),
      );
      await settle(tester);
      await tester.tap(find.text('OK'));
      await settle(tester);
      expect(find.text('Battery empty'), findsNothing);
    });
  });

  group('Songs', () {
    testWidgets('two lines per row; Select plays the song and queues the '
        'list it came from, in order, from that song', (tester) async {
      final rig = await pumpShell(tester, home: (_) => const SongsScreen());
      final songs = await gateLibrary.songs();
      // The first two-line row: title over "Artist · Album".
      expect(
        find.text('${songs[0].artist} · ${songs[0].album}'),
        findsOneWidget,
      );

      rig.step(2);
      await settle(tester);
      await _pressCentre(tester, rig);
      await tester.pump();

      expect(rig.player.calls.first, startsWith('setQueue('));
      expect(rig.player.calls, contains('play'));
      final queue = rig.player.state.queue;
      final playable = songs.where((s) => s.playable).toList();
      expect(queue.map((q) => q.id), playable.map((s) => s.id));
      expect(rig.player.state.index, 2);
      expect(queue[2].title, songs[2].title);
      // Now Playing opened (the stand-in until its screen registers).
      expect(rig.container.read(shellProvider).top!.name, 'nowPlaying');
    });

    testWidgets('a greyed song is skipped by the wheel and left out of the '
        'queue', (tester) async {
      final songs = await gateLibrary.songs();
      final broken = songs.where((s) => !s.playable).toList();
      expect(broken, isNotEmpty);
      final rig = await pumpShell(tester, home: (_) => const SongsScreen());
      // The reason is visible before any press.
      expect(find.text(broken.first.unplayableReason!), findsNothing);
      rig.step(songs.length);
      await settle(tester);
      await _pressCentre(tester, rig);
      await tester.pump();
      expect(
        rig.player.state.queue.map((q) => q.id),
        isNot(contains(broken.first.id)),
      );
    });

    testWidgets('a long fast turn shows the letter tile, which fades after '
        '250 ms at rest', (tester) async {
      final rig = await pumpShell(tester, home: (_) => const SongsScreen());
      double tileOpacity() => tester
          .widget<AnimatedOpacity>(find.byType(AnimatedOpacity).last)
          .opacity;
      expect(tileOpacity(), 0);
      rig.step(3);
      rig.step(3); // 6 steps with no gap over 400 ms: the tile shows
      await tester.pump();
      final tile = find.byWidgetPredicate(
        (w) => w is Text && w.style?.fontSize == 48,
      );
      expect(tile, findsOneWidget);
      expect(tileOpacity(), 1);
      // Still showing 200 ms later; hidden once 250 ms have passed.
      await tester.pump(const Duration(milliseconds: 200));
      expect(tileOpacity(), 1);
      await tester.pump(const Duration(milliseconds: 100));
      await tester.pumpAndSettle();
      expect(tileOpacity(), 0);
    });

    testWidgets('a tap selects the row and opens it (candidate 2)', (
      tester,
    ) async {
      final rig = await pumpShell(tester, home: (_) => const SongsScreen());
      final songs = await gateLibrary.songs();
      await tester.tap(find.text(songs[1].title));
      await settle(tester);
      expect(rig.player.state.index, 1);
      expect(rig.container.read(shellProvider).top!.name, 'nowPlaying');
    });

    testWidgets('a drag scrolls and keeps the selection in view', (
      tester,
    ) async {
      final rig = await pumpShell(tester, home: (_) => const SongsScreen());
      await tester.drag(find.byType(NeupodRow).first, const Offset(0, -300));
      await settle(tester);
      expect(
        _wheelValue(rig),
        isNot(startsWith('${(await gateLibrary.songs()).first.title},')),
      );
    });
  });

  group('an album', () {
    testWidgets('the header has the cover, artist, year, song count and total '
        'time; disc headings only when there are several discs', (
      tester,
    ) async {
      final one = (await gateLibrary.albums()).firstWhere(
        (a) => a.title == 'Tidewater',
      );
      await pumpShell(tester, home: (_) => AlbumScreen(albumId: one.id));
      expect(find.text('Pale Harbour'), findsOneWidget);
      expect(find.textContaining('2004 · 3 songs · '), findsOneWidget);
      expect(find.text('Disc 1'), findsNothing);
      expect(find.text('1'), findsOneWidget); // numbered tracks
      expect(find.text('Low Light'), findsOneWidget);

      final two = (await gateLibrary.albums()).firstWhere(
        (a) => a.title == 'Greatest Hits' && a.artist == 'Zed Zone',
      );
      await pumpShell(tester, home: (_) => AlbumScreen(albumId: two.id));
      expect(find.text('Disc 1'), findsOneWidget);
      expect(find.text('Disc 2'), findsOneWidget);
      expect(find.text('Zinc'), findsOneWidget);
    });

    testWidgets('a track plays the album from it, discs in order', (
      tester,
    ) async {
      final two = (await gateLibrary.albums()).firstWhere(
        (a) => a.title == 'Greatest Hits' && a.artist == 'Zed Zone',
      );
      final rig = await pumpShell(
        tester,
        home: (_) => AlbumScreen(albumId: two.id),
      );
      rig.step(2); // Zinc, the first track of disc 2
      await settle(tester);
      await _pressCentre(tester, rig);
      await tester.pump();
      expect(rig.player.state.queue.map((q) => q.title), [
        'Zulu',
        'Zebra',
        'Zinc',
      ]);
      expect(rig.player.state.index, 2);
    });

    testWidgets('greyed tracks show their reason at the right', (tester) async {
      final a = (await gateLibrary.albums()).firstWhere(
        (a) => a.title == '2020 Damage',
      );
      await pumpShell(tester, home: (_) => AlbumScreen(albumId: a.id));
      expect(find.text('Damaged file'), findsWidgets);
    });
  });

  group('lists', () {
    testWidgets('an empty list shows a greyed None yet; Menu still pops and '
        'the wheel says nothing', (tester) async {
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      rig.container
          .read(shellProvider.notifier)
          .push(
            ShellPage(
              name: 'empty',
              builder: (_) => ShellFrame(
                title: 'Compilations',
                body: BrowseListBody(child: NeupodList(items: const [])),
              ),
            ),
          );
      await settle(tester);
      expect(find.text('None yet'), findsOneWidget);
      expect(_wheelValue(rig), '');
      rig.step(1);
      rig.press(WheelButton.centre);
      await settle(tester);
      rig.press(WheelButton.menu);
      await settle(tester);
      expect(rig.container.read(shellProvider).pages, hasLength(1));
    });

    testWidgets('the bar slides for up to three rows and jumps for more', (
      tester,
    ) async {
      final rig = await pumpShell(tester, home: (_) => const SongsScreen());
      Duration barDuration() => tester
          .widget<AnimatedPositioned>(find.byKey(const ValueKey('bar')))
          .duration;
      rig.step(1);
      await tester.pump();
      expect(barDuration(), const Duration(milliseconds: 80));
      rig.step(3);
      await tester.pump();
      expect(barDuration(), const Duration(milliseconds: 80));
      rig.step(4);
      await tester.pump();
      expect(barDuration(), Duration.zero);
    });

    testWidgets('the jump counts the rows crossed, disc headings included', (
      tester,
    ) async {
      final two = (await gateLibrary.albums()).firstWhere(
        (a) => a.title == 'Greatest Hits' && a.artist == 'Zed Zone',
      );
      final rig = await pumpShell(
        tester,
        home: (_) => AlbumScreen(albumId: two.id),
      );
      // Zulu, Zebra | Disc 2 heading | Zinc: crossing the
      // heading is two rows, still a slide.
      rig.step(1);
      await tester.pump();
      rig.step(1);
      await tester.pump();
      expect(
        tester
            .widget<AnimatedPositioned>(find.byKey(const ValueKey('bar')))
            .duration,
        const Duration(milliseconds: 80),
      );
    });

    testWidgets('a gap over 400 ms starts the fast-scroll count again', (
      tester,
    ) async {
      final rig = await pumpShell(tester, home: (_) => const SongsScreen());
      double tile() => tester
          .widget<AnimatedOpacity>(find.byType(AnimatedOpacity).last)
          .opacity;
      rig.step(3);
      await tester.pump(const Duration(milliseconds: 450));
      rig.step(3); // only 3 since the gap
      await tester.pump(const Duration(milliseconds: 16));
      expect(tile(), 0);
      await tester.pump(const Duration(milliseconds: 100));
      rig.step(3); // 6 within 400 ms
      await tester.pump(const Duration(milliseconds: 16));
      expect(tile(), 1);
    });

    testWidgets('a row is one semantics element that opens it (touch '
        'candidate 1 relies on this)', (tester) async {
      final handle = tester.ensureSemantics();
      final rig = await pumpShell(tester, home: (_) => const HomeScreen());
      final row = find.semantics.byLabel('Music, 1 of 4');
      expect(row, findsOneWidget);
      tester.semantics.tap(row);
      await settle(tester);
      expect(rig.container.read(shellProvider).pages, hasLength(2));
      handle.dispose();
    });
  });
}
