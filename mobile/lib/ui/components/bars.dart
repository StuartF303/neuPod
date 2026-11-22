// SPDX-License-Identifier: MPL-2.0

/// Now Playing's bars: progress with its times, the volume bar that replaces
/// it while the wheel turns (owner decisions 10 and 11), and seek mode's bar.
library;

import 'package:flutter/gestures.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

/// The progress bar. Classic 12 px, Modern 6 px (times k).
///
/// The design says the bar is not draggable (seek mode is the way to seek,
/// device view section 6) and the specification lists drag to seek; both are
/// to be shown at the design gate, so dragging is behind [seekable]. While
/// seekable, a tap or a horizontal drag reports the fraction to [onSeek].
class NeupodProgressBar extends StatelessWidget {
  const NeupodProgressBar({
    super.key,
    required this.value,
    this.home = false,
    this.seekable = false,
    this.onSeek,
    this.semanticsValue,
    this.step = 1 / 60,
  });

  /// Elapsed as a fraction of the track, 0 to 1.
  final double value;

  /// The smaller bar of Home's right pane (Classic 8 px, Modern 5 px).
  final bool home;
  final bool seekable;
  final ValueChanged<double>? onSeek;

  /// What a screen reader reads (`1 minute 32 seconds of 4 minutes 1`).
  final String? semanticsValue;

  /// One screen-reader increment or decrement while seekable, as a fraction
  /// of the track (about seek mode's 1/60 a step).
  final double step;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = home ? theme.style.homeProgress : theme.style.progress;
    final view = BarView(skin: skin, value: value, k: theme.tier.k);
    final canSeek = seekable && onSeek != null;
    void seekTo(double v) => onSeek!(v.clamp(0.0, 1.0));
    String pct(double v) => '${(v.clamp(0.0, 1.0) * 100).round()} percent';
    final semantics = Semantics(
      label: 'Progress',
      value: semanticsValue ?? pct(value),
      slider: canSeek,
      increasedValue: canSeek ? pct(value + step) : null,
      decreasedValue: canSeek ? pct(value - step) : null,
      onIncrease: canSeek ? () => seekTo(value + step) : null,
      onDecrease: canSeek ? () => seekTo(value - step) : null,
      child: ExcludeSemantics(child: view),
    );
    // Outermost, so its touch area can reach [_SeekArea.slop] above and below
    // the bar without changing its layout height (a render box above it
    // would reject touches outside its own size).
    return canSeek ? _SeekArea(onSeek: seekTo, child: semantics) : semantics;
  }
}

/// Takes taps and horizontal drags within the bar and [_SeekArea.slop] above
/// and below it, reporting the fraction across. The extra area does not take
/// layout space.
class _SeekArea extends SingleChildRenderObjectWidget {
  const _SeekArea({required this.onSeek, required super.child});

  final ValueChanged<double> onSeek;

  static const double slop = 12;

  @override
  _RenderSeekArea createRenderObject(BuildContext context) =>
      _RenderSeekArea(onSeek);

  @override
  void updateRenderObject(BuildContext context, _RenderSeekArea r) =>
      r.onSeek = onSeek;
}

class _RenderSeekArea extends RenderProxyBox {
  _RenderSeekArea(this.onSeek) {
    _tap = TapGestureRecognizer()..onTapDown = (d) => _report(d.localPosition);
    _drag = HorizontalDragGestureRecognizer()
      ..onUpdate = (d) => _report(d.localPosition);
  }

  ValueChanged<double> onSeek;
  late final TapGestureRecognizer _tap;
  late final HorizontalDragGestureRecognizer _drag;

  void _report(Offset local) {
    if (size.width > 0) onSeek(local.dx / size.width);
  }

  @override
  bool hitTest(BoxHitTestResult result, {required Offset position}) {
    final area = (Offset.zero & size).inflate(_SeekArea.slop);
    if (!area.contains(position)) return false;
    result.add(BoxHitTestEntry(this, position));
    return true;
  }

  @override
  void handleEvent(PointerEvent event, BoxHitTestEntry entry) {
    if (event is PointerDownEvent) {
      _tap.addPointer(event);
      _drag.addPointer(event);
    }
  }

  @override
  void dispose() {
    _tap.dispose();
    _drag.dispose();
    super.dispose();
  }
}

/// Elapsed, the repeat or shuffle word, and remaining, under the progress bar.
class ProgressTimes extends StatelessWidget {
  const ProgressTimes({
    super.key,
    required this.elapsed,
    required this.remaining,
    this.middle,
  });

  final String elapsed;
  final String remaining;
  final String? middle;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final type = NeupodType(theme.tier);
    final ink = theme.style.tokens.ink;
    return Row(
      children: [
        Expanded(child: NeupodText(elapsed, type.small(ink))),
        if (middle != null)
          NeupodText(middle!, type.small(theme.style.list.secondary)),
        Expanded(
          child: NeupodText(remaining, type.small(ink), align: TextAlign.right),
        ),
      ],
    );
  }
}

/// Now Playing's volume bar, with a quiet speaker at one end and a loud one at
/// the other; no dB figure (owner decision 11). Modern floats it in a capsule.
class NeupodVolumeBar extends StatelessWidget {
  const NeupodVolumeBar({super.key, required this.value});

  final double value;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = theme.style.volume;
    final k = theme.tier.k;
    Widget row = Row(
      children: [
        SpeakerGlyph(size: skin.glyphSize * k, color: skin.glyph, loud: false),
        SizedBox(width: skin.gap),
        Expanded(
          child: BarView(skin: skin.bar, value: value, k: k),
        ),
        SizedBox(width: skin.gap),
        SpeakerGlyph(size: skin.glyphSize * k, color: skin.glyph, loud: true),
      ],
    );
    if (skin.capsule != null) {
      row = SizedBox(
        height: skin.capsuleHeight * k,
        child: SkinBox(
          skin: skin.capsule!.copyWith(radius: skin.capsule!.radius * k),
          child: Padding(
            padding: EdgeInsets.symmetric(horizontal: skin.capsulePadding),
            child: row,
          ),
        ),
      );
    }
    return Semantics(
      label: 'Volume',
      value: '${(value * 100).round()} percent',
      child: ExcludeSemantics(child: row),
    );
  }
}

/// Seek mode's bar (device view section 7): a 6 px amber bar on a soft amber
/// track with a 26 px knob.
class SeekBar extends StatelessWidget {
  const SeekBar({super.key, required this.value, this.semanticsValue});

  final double value;

  /// What a screen reader reads (`2 minutes 14 seconds of 4 minutes 1`);
  /// defaults to a percentage.
  final String? semanticsValue;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = theme.style.seek;
    final k = theme.tier.k;
    final knob = 26 * k;
    final bar = 6 * k;
    return Semantics(
      label: 'Seek',
      value:
          semanticsValue ?? '${(value.clamp(0.0, 1.0) * 100).round()} percent',
      child: ExcludeSemantics(
        child: SizedBox(
          height: knob,
          child: LayoutBuilder(
            builder: (context, c) {
              final x = c.maxWidth * value.clamp(0.0, 1.0);
              final top = (knob - bar) / 2;
              final r = BorderRadius.circular(bar / 2);
              return Stack(
                clipBehavior: Clip.none,
                children: [
                  Positioned(
                    left: 0,
                    right: 0,
                    top: top,
                    height: bar,
                    child: DecoratedBox(
                      decoration: BoxDecoration(
                        color: skin.track,
                        borderRadius: r,
                      ),
                    ),
                  ),
                  Positioned(
                    left: 0,
                    width: x,
                    top: top,
                    height: bar,
                    child: DecoratedBox(
                      decoration: BoxDecoration(
                        color: skin.amber,
                        borderRadius: r,
                      ),
                    ),
                  ),
                  Positioned(
                    left: x - knob / 2,
                    top: 0,
                    width: knob,
                    height: knob,
                    child: DecoratedBox(
                      decoration: BoxDecoration(
                        color: skin.amber,
                        shape: BoxShape.circle,
                        boxShadow: const [
                          BoxShadow(
                            color: Color(0x66000000),
                            offset: Offset(0, 1),
                            blurRadius: 4,
                          ),
                        ],
                      ),
                    ),
                  ),
                ],
              );
            },
          ),
        ),
      ),
    );
  }
}
