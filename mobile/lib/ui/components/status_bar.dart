// SPDX-License-Identifier: MPL-2.0

/// The status bar, the same on every screen (UI styles design, "Status bar"):
/// play state then context at the left, the title in the middle, hardware at
/// the right. The title gives way to a message or to the volume bar.
///
/// No Wi-Fi icon: the app makes no network connection in R1 (device view
/// design, section 10), so the right-hand group is, in the design's order,
/// Bluetooth, output, lock and battery. The output icon follows the phone's
/// route (owner decision 13): the speaker shows none, USB and wired the jack
/// plug, AirPlay and other wireless a generic speaker.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/style/icons/neupod_glyphs.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

enum PlayState { stopped, playing, paused }

enum RepeatState { off, all, one }

/// Bluetooth: no headset saved, saved but not connected (drawn dim, UI styles
/// Open question 4), connected or in use (blue). Colour is not the only cue:
/// saved is the plain Bluetooth rune, connected is the same rune with a short
/// bar either side (Lucide `bluetooth-connected`).
enum BluetoothIcon { none, saved, active }

/// The output icon: none, headphones (a plug in the jack), the jack plug
/// (output set to Line, USB and other wired outputs) or the generic external
/// speaker (AirPlay and other wireless, non-Bluetooth outputs).
enum OutputIcon { none, headphones, line, external }

/// Where the phone's audio goes, as the platform reports it (device view
/// Open question 7, owner decision 13).
enum OutputRoute { phoneSpeaker, wired, usb, wireless, bluetooth }

/// The status bar's output icon for [route]: the phone's own speaker shows
/// none, USB and other wired outputs the jack plug, AirPlay and other
/// wireless outputs the generic speaker. A Bluetooth route is shown by the
/// Bluetooth icon, not here. Never Apple's AirPlay glyph.
OutputIcon outputIconFor(OutputRoute route) => switch (route) {
  OutputRoute.phoneSpeaker || OutputRoute.bluetooth => OutputIcon.none,
  OutputRoute.wired || OutputRoute.usb => OutputIcon.line,
  OutputRoute.wireless => OutputIcon.external,
};

@immutable
final class BatteryState {
  const BatteryState({required this.level, this.charging = false})
    : assert(level >= 0 && level <= 1, 'level is 0 to 1');

  final double level;
  final bool charging;

  int get percent => (level * 100).round();

  /// As built: red below 15 % (unless charging).
  bool get low => level < 0.15 && !charging;

  /// As built: the percentage shows below 20 % or while charging, so the
  /// state is not told by colour alone.
  bool get showsPercent => level < 0.2 || charging;
}

class StatusBar extends StatelessWidget {
  const StatusBar({
    super.key,
    required this.title,
    this.playState = PlayState.stopped,
    this.shuffle = false,
    this.repeat = RepeatState.off,
    this.context,
    this.contextLabel,
    this.message,
    this.volume,
    this.bluetooth = BluetoothIcon.none,
    this.output = OutputIcon.none,
    this.locked = false,
    this.battery = const BatteryState(level: 1),
  });

  final String title;
  final PlayState playState;
  final bool shuffle;
  final RepeatState repeat;

  /// The screen's context text (`3 of 12`, `1,204`); Home and menus have none.
  final String? context;

  /// What a screen reader says for [context] (`track 3 of 12`); defaults to
  /// [context].
  final String? contextLabel;

  /// A message in the title's place for about two seconds (`Headphones
  /// unplugged`). Volume wins over a message.
  final String? message;

  /// The volume, 0 to 1, while it changes on a screen other than Now Playing.
  final double? volume;
  final BluetoothIcon bluetooth;
  final OutputIcon output;
  final bool locked;
  final BatteryState battery;

  /// The status bar as one screen-reader element (device view section 9).
  String get semanticsLabel {
    final parts = <String>[
      switch (playState) {
        PlayState.stopped => 'Stopped',
        PlayState.playing => 'Playing',
        PlayState.paused => 'Paused',
      },
      if (shuffle) 'shuffle on',
      if (repeat == RepeatState.all) 'repeat all',
      if (repeat == RepeatState.one) 'repeat one',
      ?(contextLabel ?? context),
      if (volume != null)
        'volume ${(volume! * 100).round()} percent'
      else
        ?message,
      if (output == OutputIcon.headphones) 'headphones',
      if (output == OutputIcon.line) 'line out',
      if (output == OutputIcon.external) 'external speaker',
      if (bluetooth == BluetoothIcon.active) 'Bluetooth connected',
      if (bluetooth == BluetoothIcon.saved) 'Bluetooth not connected',
      if (locked) 'locked',
      'battery ${battery.percent} percent${battery.charging ? ', charging' : ''}',
    ];
    return parts.join(', ');
  }

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = theme.style.statusBar;
    final type = NeupodType(theme.tier);
    final k = theme.tier.k;

    final left = PlayStateGlyphs(
      playState: playState,
      shuffle: shuffle,
      repeat: repeat,
      context: message != null && volume == null ? null : this.context,
    );
    final Widget centre = volume != null
        ? StatusVolume(value: volume!)
        : NeupodText(
            message ?? title,
            type.body(skin.title),
            align: TextAlign.center,
          );

    final right = Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        if (bluetooth != BluetoothIcon.none)
          _hardware(
            bluetooth == BluetoothIcon.active
                ? NeupodGlyph.bluetoothConnected
                : NeupodGlyph.bluetooth,
            bluetooth == BluetoothIcon.active ? skin.bluetoothActive : skin.dim,
            bluetooth == BluetoothIcon.active ? 2.75 : 2.5,
            skin,
            k,
          ),
        if (output != OutputIcon.none)
          _hardware(
            switch (output) {
              OutputIcon.headphones => NeupodGlyph.headphones,
              OutputIcon.external => NeupodGlyph.speaker,
              _ => NeupodGlyph.jackPlug,
            },
            skin.glyph,
            2.5,
            skin,
            k,
          ),
        if (locked) _hardware(NeupodGlyph.lock, skin.glyph, 2.5, skin, k),
        if (battery.showsPercent)
          Padding(
            padding: const EdgeInsets.only(right: 6),
            child: Text('${battery.percent}%', style: type.small(skin.context)),
          ),
        Battery(state: battery, skin: skin.battery, k: k),
      ],
    );

    return Semantics(
      container: true,
      label: semanticsLabel,
      // A message swapped into the title is announced as it appears.
      liveRegion: message != null && volume == null,
      child: ExcludeSemantics(
        child: SkinBox(
          skin: skin.ground,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 19),
            child: CustomMultiChildLayout(
              delegate: _StatusLayout(
                centreOnScreen: message == null || volume != null,
              ),
              children: [
                LayoutId(id: _Slot.left, child: left),
                LayoutId(id: _Slot.centre, child: centre),
                LayoutId(id: _Slot.right, child: right),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _hardware(
    NeupodGlyph g,
    Color c,
    double stroke,
    StatusBarSkin skin,
    double k,
  ) => Padding(
    padding: EdgeInsets.only(right: skin.iconGap),
    child: NeupodIcon(g, size: 22 * k, color: c, strokeWidth: stroke),
  );
}

enum _Slot { left, centre, right }

/// CSS `grid-template-columns: 1fr auto 1fr` (the title centred on the bar)
/// or, while a message shows, `auto 1fr auto` with a 14 px gap (the message
/// centred between the two groups), as the mock-ups draw them.
class _StatusLayout extends MultiChildLayoutDelegate {
  _StatusLayout({required this.centreOnScreen});

  final bool centreOnScreen;
  static const double gap = 14;

  @override
  void performLayout(Size size) {
    final loose = BoxConstraints.loose(Size(size.width * 0.45, size.height));
    final r = layoutChild(_Slot.right, loose);
    final l = layoutChild(
      _Slot.left,
      BoxConstraints.loose(Size(size.width - r.width - gap, size.height)),
    );
    positionChild(_Slot.left, Offset(0, (size.height - l.height) / 2));
    positionChild(
      _Slot.right,
      Offset(size.width - r.width, (size.height - r.height) / 2),
    );

    final double from, to;
    if (centreOnScreen) {
      final side = l.width > r.width ? l.width : r.width;
      from = side + gap;
      to = size.width - side - gap;
    } else {
      from = l.width + gap;
      to = size.width - r.width - gap;
    }
    final avail = (to - from).clamp(0.0, size.width);
    final c = layoutChild(
      _Slot.centre,
      BoxConstraints.loose(Size(avail, size.height)),
    );
    positionChild(
      _Slot.centre,
      Offset(from + (avail - c.width) / 2, (size.height - c.height) / 2),
    );
  }

  @override
  bool shouldRelayout(_StatusLayout old) =>
      old.centreOnScreen != centreOnScreen;
}

/// The left group: the transport glyph, then shuffle and repeat, then the
/// context text (owner decisions 7 and 8). The mode glyphs stay while paused.
class PlayStateGlyphs extends StatelessWidget {
  const PlayStateGlyphs({
    super.key,
    required this.playState,
    this.shuffle = false,
    this.repeat = RepeatState.off,
    this.context,
  });

  final PlayState playState;
  final bool shuffle;
  final RepeatState repeat;
  final String? context;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = theme.style.statusBar;
    final k = theme.tier.k;
    final glyphs = <Widget>[
      if (playState != PlayState.stopped)
        NeupodIcon(
          playState == PlayState.playing ? skin.play : skin.pause,
          size: 18 * k,
          color: skin.glyph,
          paint: playState == PlayState.playing
              ? skin.playPaint
              : GlyphPaint.fill,
        ),
      if (shuffle)
        NeupodIcon(
          NeupodGlyph.shuffle,
          size: 20 * k,
          color: skin.glyph,
          strokeWidth: 2.5,
        ),
      if (repeat != RepeatState.off)
        NeupodIcon(
          repeat == RepeatState.one
              ? NeupodGlyph.repeatOne
              : NeupodGlyph.repeat,
          size: 20 * k,
          color: skin.glyph,
          strokeWidth: 2.5,
        ),
    ];
    final children = <Widget>[];
    for (final g in glyphs) {
      if (children.isNotEmpty) children.add(const SizedBox(width: 6));
      children.add(g);
    }
    if (this.context != null) {
      if (children.isNotEmpty) children.add(const SizedBox(width: 8));
      children.add(
        Flexible(
          child: NeupodText(
            this.context!,
            NeupodType(theme.tier).small(skin.context),
          ),
        ),
      );
    }
    return Row(mainAxisSize: MainAxisSize.min, children: children);
  }
}

/// The battery: the style's skin, filled to the level; red below 15 %.
class Battery extends StatelessWidget {
  const Battery({
    super.key,
    required this.state,
    required this.skin,
    this.k = 1,
  });

  final BatteryState state;
  final BatterySkin skin;
  final double k;

  @override
  Widget build(BuildContext context) {
    final w = BatterySkin.body.width * k;
    final h = BatterySkin.body.height * k;
    const pad = BatterySkin.padding + BatterySkin.outlineWidth;
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: w,
          height: h,
          padding: const EdgeInsets.all(pad),
          decoration: BoxDecoration(
            color: skin.inside,
            border: Border.all(
              color: skin.outline,
              width: BatterySkin.outlineWidth,
            ),
            borderRadius: BorderRadius.circular(skin.radius),
          ),
          alignment: Alignment.centerLeft,
          child: FractionallySizedBox(
            widthFactor: state.level,
            heightFactor: 1,
            child: DecoratedBox(
              decoration: BoxDecoration(
                gradient: state.low ? null : skin.fill,
                color: state.low ? skin.low : null,
                borderRadius: BorderRadius.circular(skin.fillRadius),
              ),
            ),
          ),
        ),
        SizedBox(width: skin.terminalGap),
        Container(
          width: skin.terminal.width * k,
          height: skin.terminal.height * k,
          decoration: BoxDecoration(
            color: skin.outline,
            borderRadius: const BorderRadius.horizontal(
              right: Radius.circular(2),
            ),
          ),
        ),
      ],
    );
  }
}

/// The volume bar that takes the title's place for about a second when the
/// volume changes on a screen other than Now Playing (owner decision 12).
class StatusVolume extends StatelessWidget {
  const StatusVolume({super.key, required this.value});

  final double value;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = theme.style.statusBar;
    final k = theme.tier.k;
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        SpeakerGlyph(size: 20 * k, color: skin.glyph, loud: false),
        const SizedBox(width: 8),
        SizedBox(
          width: 170,
          child: BarView(skin: skin.volume, value: value, k: k),
        ),
        const SizedBox(width: 8),
        SpeakerGlyph(size: 20 * k, color: skin.glyph, loud: true),
      ],
    );
  }
}

/// The quiet (no waves) or loud speaker, its body filled as in the mock-ups.
class SpeakerGlyph extends StatelessWidget {
  const SpeakerGlyph({
    super.key,
    required this.size,
    required this.color,
    required this.loud,
  });

  final double size;
  final Color color;
  final bool loud;

  @override
  Widget build(BuildContext context) {
    final body = NeupodIcon(
      NeupodGlyph.volume,
      size: size,
      color: color,
      paint: GlyphPaint.fillAndStroke,
    );
    if (!loud) return body;
    return Stack(
      children: [
        body,
        NeupodIcon(NeupodGlyph.volumeLoud, size: size, color: color),
      ],
    );
  }
}

/// A track with a fill to [value] (0 to 1), from a [BarSkin]; the height is the
/// skin's times [k].
class BarView extends StatelessWidget {
  const BarView({
    super.key,
    required this.skin,
    required this.value,
    this.k = 1,
  });

  final BarSkin skin;
  final double value;
  final double k;

  @override
  Widget build(BuildContext context) {
    final h = skin.height * k;
    final bordered = skin.track.border != null;
    final inner = bordered ? 1.0 : 0.0;
    return SizedBox(
      height: h,
      child: SkinBox(
        skin: skin.track.copyWith(radius: skin.radius * k),
        child: Padding(
          padding: EdgeInsets.all(inner),
          child: ClipRRect(
            borderRadius: BorderRadius.circular(
              (skin.radius * k - inner).clamp(0, 100),
            ),
            child: Align(
              alignment: Alignment.centerLeft,
              child: FractionallySizedBox(
                widthFactor: value.clamp(0.0, 1.0),
                heightFactor: 1,
                child: SkinBox(
                  skin: skin.fill.copyWith(radius: skin.fillRadius * k),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
