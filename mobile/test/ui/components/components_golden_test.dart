// SPDX-License-Identifier: MPL-2.0

// Goldens of every component in Classic Light and Modern Dark at the three
// text tiers (device view design, section 8). See flutter_test_config.dart for
// how they are kept deterministic and where they are compared.
//
// Update on macOS:  flutter test --update-goldens test/ui/components

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/art_frame.dart';
import 'package:neupod/ui/components/bars.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/components/panel.dart';
import 'package:neupod/ui/components/search_entry.dart';
import 'package:neupod/ui/components/small_parts.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/style/icons/neupod_glyphs.dart';
import 'package:neupod/ui/style/skins.dart';

import 'golden_harness.dart';

const _gap = 8.0;

Widget _gapped(List<Widget> children) => Column(
  crossAxisAlignment: CrossAxisAlignment.stretch,
  children: [
    for (final c in children) ...[c, const SizedBox(height: _gap)],
  ],
);

void main() {
  // The status bar's states (UI styles mock-ups: status states, play states,
  // feedback in the status bar). No Wi-Fi icon in R1.
  const statusBars = 6;
  goldenSheet('status_bar', (t) => statusBars * (t.statusBarHeight + _gap), (
    style,
    tier,
  ) {
    Widget bar(StatusBar b) => SizedBox(height: tier.statusBarHeight, child: b);
    return _gapped([
      bar(
        const StatusBar(
          title: 'Songs',
          playState: PlayState.playing,
          context: '1,204',
          bluetooth: BluetoothIcon.saved,
          output: OutputIcon.headphones,
          battery: BatteryState(level: 0.78),
        ),
      ),
      bar(
        const StatusBar(
          title: 'Now Playing',
          playState: PlayState.playing,
          shuffle: true,
          repeat: RepeatState.all,
          context: '3 of 12',
          bluetooth: BluetoothIcon.active,
          battery: BatteryState(level: 0.78),
        ),
      ),
      bar(
        const StatusBar(
          title: 'Now Playing',
          playState: PlayState.paused,
          repeat: RepeatState.one,
          context: '3 of 12',
          output: OutputIcon.line,
          locked: true,
          battery: BatteryState(level: 0.1),
        ),
      ),
      bar(
        const StatusBar(
          title: 'Songs',
          playState: PlayState.playing,
          context: '1,204',
          volume: 0.62,
          output: OutputIcon.headphones,
          battery: BatteryState(level: 0.78),
        ),
      ),
      bar(
        const StatusBar(
          title: 'Songs',
          playState: PlayState.playing,
          context: '1,204',
          message: 'Headphones unplugged',
          bluetooth: BluetoothIcon.saved,
          output: OutputIcon.external,
          battery: BatteryState(level: 0.5, charging: true),
        ),
      ),
      bar(const StatusBar(title: 'neuPod')),
    ]);
  });

  // One-line rows: chevron, selected, pressed, value, track numbers, greyed
  // with a reason, and the flash with a tick.
  goldenSheet('rows_one_line', (t) => 8 * t.rowHeight, (style, tier) {
    return ListGround(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: const [
          NeupodRow(
            title: 'Music',
            chevron: true,
            selection: RowSelection.selected,
          ),
          NeupodRow(title: 'Podcasts', chevron: true),
          NeupodRow(title: 'Photos', chevron: true, pressed: true),
          NeupodRow(title: 'Audio output', value: 'Auto'),
          NeupodRow(title: 'Time Becomes', number: '1'),
          NeupodRow(
            title: 'Planet of the Shapes',
            number: '2',
            selection: RowSelection.selected,
          ),
          NeupodRow(
            title: 'Speaker',
            disabledReason: 'No speaker in this device',
          ),
          NeupodRow(
            title: 'Impact (The Earth Is Burning) - a very long title indeed',
            selection: RowSelection.flash,
          ),
        ],
      ),
    );
  });

  // The icon set's status bar states (icon set design, "The status bar"):
  // scanning, opening, can't play with Line out, a library update, recording.
  goldenSheet('status_bar_icons', (t) => 6 * (t.statusBarHeight + _gap), (
    style,
    tier,
  ) {
    Widget bar(StatusBar b) => SizedBox(height: tier.statusBarHeight, child: b);
    return _gapped([
      bar(
        const StatusBar(
          title: 'Now Playing',
          playState: PlayState.playing,
          transient: TransientState.scanForward,
          bluetooth: BluetoothIcon.active,
          battery: BatteryState(level: 0.78),
        ),
      ),
      bar(
        const StatusBar(
          title: 'Now Playing',
          playState: PlayState.playing,
          transient: TransientState.scanBack,
          bluetooth: BluetoothIcon.saved,
          output: OutputIcon.headphones,
          battery: BatteryState(level: 0.78),
        ),
      ),
      bar(
        const StatusBar(
          title: 'Radio',
          transient: TransientState.opening,
          output: OutputIcon.external,
          battery: BatteryState(level: 0.78),
        ),
      ),
      bar(
        const StatusBar(
          title: 'Songs',
          playState: PlayState.paused,
          busy: true,
          locked: true,
          battery: BatteryState(level: 0.78),
        ),
      ),
      bar(
        const StatusBar(
          title: 'Now Playing',
          transient: TransientState.cantPlay,
          output: OutputIcon.line,
          battery: BatteryState(level: 0.78),
        ),
      ),
      bar(
        const StatusBar(
          title: 'Recording',
          transient: TransientState.recording,
          battery: BatteryState(level: 0.78),
        ),
      ),
    ]);
  });

  // Rows with a rating, a song that cannot play and an iCloud file not on the
  // phone (reason on the second line).
  goldenSheet('rows_icons', (t) => 3 * t.rowHeight + 3 * t.row2Height, (
    style,
    tier,
  ) {
    return ListGround(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: const [
          NeupodRow(title: 'Harbour Lights', rating: 4),
          NeupodRow(
            title: 'Ember',
            rating: 2,
            selection: RowSelection.selected,
          ),
          NeupodRow(
            title: 'Carrier Wave',
            disabledReason: "AAC, can't play",
            reasonGlyph: NeupodGlyph.playOff,
          ),
          NeupodRow(
            title: 'Signal',
            subtitle: 'Beacon · Signal',
            disabledReason: 'Not downloaded',
            reasonGlyph: NeupodGlyph.cloudDownload,
            action: 'Download',
          ),
          NeupodRow(
            title: 'Orbital',
            subtitle: 'Aurora Lane · First Light',
            disabledReason: "AAC, can't play",
            reasonGlyph: NeupodGlyph.playOff,
          ),
          NeupodRow(
            title: 'Ember',
            subtitle: 'The Cinders · Now 7',
            disabledReason: 'Downloading',
            reasonGlyph: NeupodGlyph.cloudDownload,
            action: 'Download',
          ),
        ],
      ),
    );
  });

  // Two-line rows, with and without thumbnails.
  goldenSheet('rows_two_line', (t) => 6 * t.row2Height, (style, tier) {
    return ListGround(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const NeupodRow(
            title: 'Carrier Wave',
            subtitle: 'Beacon · Signal',
            disabledReason: "AAC, can't play",
          ),
          const NeupodRow(
            title: 'Café Del Mar',
            subtitle: 'Aurora Lane · First Light',
            selection: RowSelection.flash,
          ),
          const NeupodRow(title: 'Ember', subtitle: 'The Cinders · Now 7'),
          const NeupodRow(
            title: 'Harbour Lights',
            subtitle: 'Aurora Lane · First Light',
            selection: RowSelection.selected,
          ),
          NeupodRow(
            title: 'Orbital',
            subtitle: '1991 · 11 songs',
            chevron: true,
            thumbnail: const ArtFrame(
              size: 59,
              use: ArtUse.thumbnail,
              art: TestArt.harbour,
            ),
          ),
          NeupodRow(
            title: 'Orbital 2',
            subtitle: '1993 · 10 songs',
            chevron: true,
            selection: RowSelection.selected,
            thumbnail: const ArtFrame(
              size: 59,
              use: ArtUse.thumbnailSelected,
              art: TestArt.orbital,
            ),
          ),
        ],
      ),
    );
  });

  // Progress with its times, Home's small bar, Now Playing's volume bar, and
  // seek mode's bar.
  goldenSheet('bars', (t) => 330, (style, tier) {
    return Padding(
      padding: const EdgeInsets.all(19),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: const [
          NeupodProgressBar(value: 0.38),
          SizedBox(height: 8),
          ProgressTimes(
            elapsed: '1:32',
            remaining: '-2:29',
            middle: 'repeat 1',
          ),
          SizedBox(height: 24),
          Center(
            child: SizedBox(
              width: 220,
              child: NeupodProgressBar(value: 0.38, home: true),
            ),
          ),
          SizedBox(height: 24),
          NeupodVolumeBar(value: 0.62),
          SizedBox(height: 24),
          SeekBar(value: 0.55),
          SizedBox(height: 12),
          ProgressTimes(elapsed: '1:32', remaining: '-2:29'),
        ],
      ),
    );
  });

  // The centred panel over a list, with its scrim.
  goldenSheet('panel', (t) => 450 - t.statusBarHeight, (style, tier) {
    return Stack(
      fit: StackFit.expand,
      children: [
        ListGround(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: const [
              NeupodRow(title: 'Bluetooth', value: 'On'),
              NeupodRow(title: 'Audio output', value: 'Auto'),
              NeupodRow(title: 'Devices'),
            ],
          ),
        ),
        const NeupodPanelScrim(
          panel: NeupodPanel(
            title: 'Paired, but not saved',
            message:
                'The card is read-only, so this headset will be forgotten when '
                'the player switches off.',
          ),
        ),
      ],
    );
  });

  // Art frames (Home, Now Playing with Classic's reflection, a missing cover),
  // the album header, a disc heading and a section heading.
  goldenSheet(
    'art_and_header',
    (t) => 300 + AlbumHeader.height + 34 * t.k + 30 * t.k,
    (style, tier) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SizedBox(
            height: 300,
            child: Padding(
              padding: const EdgeInsets.fromLTRB(19, 22, 19, 0),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: const [
                  ArtFrame(
                    size: 210,
                    use: ArtUse.nowPlaying,
                    art: TestArt.harbour,
                    reflection: true,
                  ),
                  SizedBox(width: 30),
                  ArtFrame(size: 150, use: ArtUse.home, art: TestArt.orbital),
                  SizedBox(width: 30),
                  ArtFrame(size: 96, use: ArtUse.header),
                ],
              ),
            ),
          ),
          const AlbumHeader(
            artist: 'Orbital',
            details: ['1993', '10 songs', '1:08:42'],
            art: TestArt.orbital,
          ),
          const DiscHeading('Disc 1'),
          const SectionHeading('Albums'),
        ],
      );
    },
  );

  // The fast-scroll letter tile and index rail over a list.
  goldenSheet('fast_scroll', (t) => 450 - t.statusBarHeight, (style, tier) {
    return Stack(
      fit: StackFit.expand,
      children: [
        ListGround(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: const [
              NeupodRow(
                title: 'Hallowed Ground',
                subtitle: 'Aurora Lane · First Light',
              ),
              NeupodRow(
                title: 'Harbour Lights',
                subtitle: 'Aurora Lane · First Light',
                selection: RowSelection.selected,
              ),
              NeupodRow(title: 'Hit A', subtitle: 'Zed Zed · Hit A'),
            ],
          ),
        ),
        const FastScrollIndex(letter: 'H', rail: 'ACEGHKMOQSUWY'),
      ],
    );
  });

  // Rating stars, Search's field, the selection bar alone and the scroll bar.
  goldenSheet('small_parts', (t) => 60 * t.k + 2 * 40 * t.k + 120, (
    style,
    tier,
  ) {
    return Row(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const SearchFieldBox(text: 'orb'),
              const Padding(
                padding: EdgeInsets.symmetric(horizontal: 19, vertical: 8),
                child: Row(children: [RatingStars(rating: 3)]),
              ),
              const Padding(
                padding: EdgeInsets.symmetric(horizontal: 19, vertical: 8),
                child: Row(children: [RatingStars(rating: 0)]),
              ),
              Padding(
                padding: EdgeInsets.symmetric(horizontal: style.list.padX),
                child: const SizedBox(height: 56, child: SelectionBar()),
              ),
            ],
          ),
        ),
        const NeupodScrollBar(visible: 0.3, position: 0.4),
      ],
    );
  });

  // Search typed with the wheel (owner decision 12): field, live results,
  // letter ring, the firmware's six mode chips (owner's choice), hint.
  goldenSheet('search_entry', (t) => 400 * t.k, (style, tier) {
    return ListGround(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: const [
          SearchFieldBox(text: 'orb'),
          SearchResultRow(title: 'Orbital', label: 'Artist'),
          SearchResultRow(
            title: 'Orbital',
            label: 'Album · 1991',
            selected: true,
          ),
          SearchResultRow(title: 'Orbital 2', label: 'Album · 1993'),
          SizedBox(height: 4),
          LetterRing(glyphs: ['e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm']),
          ModeChips(
            labels: ['abc', 'ABC', '123', '#!?', 'space', 'del'],
            active: 0,
          ),
          ModeChips(
            labels: ['abc', 'ABC', '123', '#!?', 'space', 'del'],
            active: 2,
          ),
          SizedBox(height: 6),
          SearchHint('Select: type · Menu: delete · Play: results'),
        ],
      ),
    );
  });
}
