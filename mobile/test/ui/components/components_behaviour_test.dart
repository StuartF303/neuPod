// SPDX-License-Identifier: MPL-2.0

// Behaviour and screen-reader semantics of the components (device view
// design, section 9), and the art palette pipeline.

import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/art_palette.dart';
import 'package:neupod/ui/components/bars.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/components/panel.dart';
import 'package:neupod/ui/components/small_parts.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/style/art_colours.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/text_tier.dart';

import 'golden_harness.dart';

Future<void> _pump(
  WidgetTester tester,
  Widget child, {
  String style = 'classic_light',
}) => tester.pumpWidget(
  Directionality(
    textDirection: TextDirection.ltr,
    child: NeupodTheme(
      style: designedStyles[style]!,
      tier: TextTier.standard,
      child: Align(
        alignment: Alignment.topLeft,
        child: SizedBox(width: 600, child: child),
      ),
    ),
  ),
);

void main() {
  group('status bar', () {
    test('reads as one element, in the design\'s order', () {
      const bar = StatusBar(
        title: 'Now Playing',
        playState: PlayState.playing,
        shuffle: true,
        context: '3 of 12',
        contextLabel: 'track 3 of 12',
        output: OutputIcon.headphones,
        battery: BatteryState(level: 0.78),
      );
      expect(
        bar.semanticsLabel,
        'Playing, shuffle on, track 3 of 12, headphones, battery 78 percent',
      );
    });

    test('the route decides the output icon (owner decision 13)', () {
      expect(outputIconFor(OutputRoute.phoneSpeaker), OutputIcon.none);
      expect(outputIconFor(OutputRoute.usb), OutputIcon.line);
      expect(outputIconFor(OutputRoute.wired), OutputIcon.line);
      expect(outputIconFor(OutputRoute.wireless), OutputIcon.external);
      expect(outputIconFor(OutputRoute.bluetooth), OutputIcon.none);
    });

    test('Bluetooth saved and connected differ in words and in shape', () {
      const saved = StatusBar(title: 'a', bluetooth: BluetoothIcon.saved);
      const active = StatusBar(title: 'a', bluetooth: BluetoothIcon.active);
      expect(saved.semanticsLabel, contains('Bluetooth not connected'));
      expect(active.semanticsLabel, contains('Bluetooth connected'));
    });

    test('volume wins over a message', () {
      const bar = StatusBar(
        title: 'Songs',
        message: 'Library updated',
        volume: 0.5,
      );
      expect(bar.semanticsLabel, contains('volume 50 percent'));
      expect(bar.semanticsLabel, isNot(contains('Library updated')));
    });

    test('battery rules as built', () {
      expect(const BatteryState(level: 0.14).low, isTrue);
      expect(const BatteryState(level: 0.14, charging: true).low, isFalse);
      expect(const BatteryState(level: 0.19).showsPercent, isTrue);
      expect(const BatteryState(level: 0.5).showsPercent, isFalse);
      expect(
        const BatteryState(level: 0.5, charging: true).showsPercent,
        isTrue,
      );
    });

    testWidgets('draws no Wi-Fi icon and shows the message in the title', (
      tester,
    ) async {
      await _pump(
        tester,
        const SizedBox(
          height: 45,
          child: StatusBar(title: 'Songs', message: 'Headphones unplugged'),
        ),
      );
      expect(find.text('Headphones unplugged'), findsOneWidget);
      expect(find.text('Songs'), findsNothing);
    });
  });

  group('rows', () {
    testWidgets('a row reads its text and position, and taps', (tester) async {
      var taps = 0;
      await _pump(
        tester,
        NeupodRow(
          title: 'Albums',
          chevron: true,
          position: '4 of 7',
          onTap: () => taps++,
        ),
      );
      expect(find.bySemanticsLabel('Albums, 4 of 7'), findsOneWidget);
      await tester.tap(find.byType(NeupodRow));
      expect(taps, 1);
    });

    testWidgets('a greyed row reads its reason and does not tap', (
      tester,
    ) async {
      var taps = 0;
      await _pump(
        tester,
        NeupodRow(
          title: 'Carrier Wave',
          subtitle: 'Beacon · Signal',
          disabledReason: "AAC, can't play",
          onTap: () => taps++,
        ),
      );
      expect(
        find.bySemanticsLabel("Carrier Wave, Beacon · Signal, AAC, can't play"),
        findsOneWidget,
      );
      await tester.tap(find.byType(NeupodRow));
      expect(taps, 0);
    });

    testWidgets('row heights follow the tier', (tester) async {
      for (final tier in TextTier.values) {
        await tester.pumpWidget(
          Directionality(
            textDirection: TextDirection.ltr,
            child: NeupodTheme(
              style: designedStyles['modern_dark']!,
              tier: tier,
              child: const Align(
                alignment: Alignment.topLeft,
                child: SizedBox(
                  width: 600,
                  child: Column(
                    children: [
                      NeupodRow(title: 'One'),
                      NeupodRow(title: 'Two', subtitle: 'lines'),
                    ],
                  ),
                ),
              ),
            ),
          ),
        );
        final sizes = tester
            .widgetList<NeupodRow>(find.byType(NeupodRow))
            .map((w) => tester.getSize(find.byWidget(w)).height);
        expect(sizes, [tier.rowHeight, tier.row2Height], reason: tier.name);
      }
    });

    testWidgets('a long reason does not overflow the row', (tester) async {
      await _pump(
        tester,
        const NeupodRow(
          title: 'A title that is far too long to fit in the row at all',
          disabledReason:
              'A reason that is also much too long to fit in the row',
        ),
      );
      expect(tester.takeException(), isNull);
    });
  });

  group('progress bar', () {
    testWidgets('not seekable by default; seeks when the flag is on', (
      tester,
    ) async {
      double? seeked;
      await _pump(
        tester,
        NeupodProgressBar(value: 0.2, onSeek: (v) => seeked = v),
      );
      await tester.tap(find.byType(NeupodProgressBar));
      expect(seeked, isNull);

      await _pump(
        tester,
        Column(
          children: [
            const SizedBox(height: 40),
            NeupodProgressBar(
              value: 0.2,
              seekable: true,
              onSeek: (v) => seeked = v,
            ),
            const SizedBox(height: 40),
          ],
        ),
      );
      final box = tester.getRect(find.byType(NeupodProgressBar));
      await tester.tapAt(Offset(box.left + box.width * 0.75, box.center.dy));
      expect(seeked, closeTo(0.75, 0.01));

      // The touch area reaches 12 px above and below without taking layout
      // height (a 12 px Classic bar stays 12 px tall).
      expect(box.height, 12);
      seeked = null;
      await tester.tapAt(Offset(box.left + box.width * 0.5, box.top - 10));
      expect(seeked, closeTo(0.5, 0.01));
      seeked = null;
      await tester.tapAt(Offset(box.left + box.width * 0.5, box.bottom + 20));
      expect(seeked, isNull);
    });

    testWidgets('seekable: the screen reader can step it', (tester) async {
      final handle = tester.ensureSemantics();
      double? seeked;
      await _pump(
        tester,
        NeupodProgressBar(
          value: 0.5,
          seekable: true,
          step: 0.1,
          onSeek: (v) => seeked = v,
        ),
      );
      final node = tester.getSemantics(find.bySemanticsLabel('Progress'));
      expect(node.flagsCollection.isSlider, isTrue);
      node.owner!.performAction(node.id, SemanticsAction.increase);
      expect(seeked, closeTo(0.6, 1e-9));
      node.owner!.performAction(node.id, SemanticsAction.decrease);
      expect(seeked, closeTo(0.4, 1e-9));
      handle.dispose();
    });
  });

  testWidgets('seek bar reads as Seek with its value', (tester) async {
    await _pump(
      tester,
      const SeekBar(
        value: 0.55,
        semanticsValue: '2 minutes 14 seconds of 4 minutes 1',
      ),
    );
    expect(
      tester.getSemantics(find.byType(SeekBar)),
      matchesSemantics(
        label: 'Seek',
        value: '2 minutes 14 seconds of 4 minutes 1',
      ),
    );
  });

  test('a row label leads with the track number and keeps the value', () {
    const row = NeupodRow(
      title: 'Planet of the Shapes',
      number: '2',
      subtitle: 'Orbital',
      value: '5:12',
      position: '2 of 10',
    );
    expect(
      row.semanticsLabel,
      '2, Planet of the Shapes, Orbital, 5:12, 2 of 10',
    );
  });

  testWidgets('a status-bar message is a live region', (tester) async {
    await _pump(
      tester,
      const SizedBox(
        height: 45,
        child: StatusBar(title: 'Songs', message: 'Library updated'),
      ),
    );
    final node = tester.getSemantics(find.byType(StatusBar));
    expect(node.flagsCollection.isLiveRegion, isTrue);
  });

  testWidgets('panel: OK is a button with its label', (tester) async {
    var ok = 0;
    await _pump(
      tester,
      NeupodPanel(
        title: 'Card is full',
        message: 'Nothing was saved.',
        onOk: () => ok++,
      ),
      style: 'modern_dark',
    );
    await tester.tap(find.text('OK'));
    expect(ok, 1);
    expect(find.bySemanticsLabel('OK'), findsOneWidget);
  });

  testWidgets('rating stars read their value', (tester) async {
    await _pump(tester, const RatingStars(rating: 4));
    expect(find.bySemanticsLabel('Rating, 4 of 5 stars'), findsOneWidget);
    await _pump(tester, const RatingStars(rating: 0));
    expect(find.bySemanticsLabel('Not rated'), findsOneWidget);
  });

  group('art colours', () {
    testWidgets('a cover is decoded small and gives its palette', (
      tester,
    ) async {
      final palette = await tester.runAsync(() async {
        final recorder = ui.PictureRecorder();
        final canvas = Canvas(recorder);
        canvas.drawRect(
          const Rect.fromLTWH(0, 0, 200, 200),
          Paint()..color = const Color(0xff16324f),
        );
        canvas.drawRect(
          const Rect.fromLTWH(0, 0, 200, 120),
          Paint()..color = const Color(0xffd9734e),
        );
        final image = await recorder.endRecording().toImage(200, 200);
        final png = await image.toByteData(format: ui.ImageByteFormat.png);
        return paletteFromEncodedArt(png!.buffer.asUint8List());
      });
      expect(palette, isNotNull);
      expect(palette!.dominant, 0xd9734e);
      expect(palette.secondary, 0x16324f);
    });

    testWidgets('undecodable bytes give null', (tester) async {
      final palette = await tester.runAsync(
        () => paletteFromEncodedArt(Uint8List.fromList([1, 2, 3, 4])),
      );
      expect(palette, isNull);
    });

    test('the cache computes once per key and forgets the oldest', () async {
      final cache = ArtPaletteCache(capacity: 2);
      var loads = 0;
      Future<ArtPalette?> load() async {
        loads++;
        return null;
      }

      await cache.get('a', load);
      await cache.get('a', load);
      expect(loads, 1);
      await cache.get('b', load);
      await cache.get('c', load);
      expect(cache.length, 2);
      await cache.get('a', load);
      expect(loads, 4);
    });
  });
}
