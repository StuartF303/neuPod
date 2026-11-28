// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter/semantics.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/domain/wheel/wheel_input.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/device_paint.dart';
import 'package:neupod/ui/device/hit_gate.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/device/wheel_painter.dart';

/// The click wheel: draws it, turns touches into [WheelEvent]s on the
/// [wheelBusProvider], gives the button haptics, and is one adjustable
/// element for the screen reader (design section 9).
///
/// The widget is [WheelZones.ringOuter] x 2 R square (the ring takes touches
/// 6 % beyond its drawn edge) with the wheel of [diameter] drawn in the
/// middle; touches outside 1.06 R pass through to what is behind it.
class ClickWheel extends ConsumerStatefulWidget {
  /// A wheel [diameter] points across, in [paint].
  const ClickWheel({required this.diameter, required this.paint, super.key});

  /// D, the drawn diameter.
  final double diameter;

  /// What to paint with.
  final DevicePaint paint;

  /// The widget's side: the touch area, 1.06 D.
  static double extentFor(double diameter) => diameter * WheelZones.ringOuter;

  @override
  ConsumerState<ClickWheel> createState() => _ClickWheelState();
}

/// Screen-reader action names (design section 9), in order.
abstract final class WheelActionLabels {
  /// Select.
  static const select = 'Select';

  /// Menu.
  static const menu = 'Menu';

  /// Previous track.
  static const previous = 'Previous track';

  /// Next track.
  static const next = 'Next track';

  /// Play.
  static const play = 'Play';

  /// Pause.
  static const pause = 'Pause';
}

class _ClickWheelState extends ConsumerState<ClickWheel> {
  late final WheelBus _bus = ref.read(wheelBusProvider);
  late final WheelInput _input = WheelInput(holdMs: _bus.holdMs);
  Timer? _holdTimer;

  @override
  void initState() {
    super.initState();
    _bus.enabled.addListener(_enabledChanged);
  }

  @override
  void dispose() {
    _bus.enabled.removeListener(_enabledChanged);
    _holdTimer?.cancel();
    super.dispose();
  }

  int get _now => ref.read(wheelClockProvider)();

  void _enabledChanged() {
    _handle(_input.setEnabled(_bus.enabled.value, _now));
    setState(() {});
  }

  void _handle(List<WheelEvent> events) {
    if (events.isEmpty) return;
    final feedback = ref.read(deviceFeedbackProvider);
    for (final e in events) {
      switch (e) {
        case WheelButtonDown():
          feedback.buttonDown();
        case WheelButtonUp():
          feedback.buttonUp();
        case WheelHoldStart(:final button)
            when !_bus.screenGivesHoldHaptic.contains(button):
          feedback.holdTookEffect();
        case _:
          break;
      }
      _bus.emit(e);
    }
  }

  Offset _units(Offset local) {
    final r = widget.diameter / 2;
    final c = ClickWheel.extentFor(widget.diameter) / 2;
    return Offset((local.dx - c) / r, (local.dy - c) / r);
  }

  void _scheduleHold() {
    _holdTimer?.cancel();
    _holdTimer = null;
    final deadline = _input.nextDeadlineMs;
    if (deadline == null) return;
    final wait = (deadline - _now) & 0xFFFFFFFF;
    _holdTimer = Timer(
      Duration(milliseconds: wait > 0x7FFFFFFF ? 0 : wait),
      () {
        _holdTimer = null;
        _after(_input.tick(_now));
      },
    );
  }

  void _after(List<WheelEvent> events) {
    _handle(events);
    _scheduleHold();
    setState(() {});
  }

  void _down(PointerDownEvent e) {
    final p = _units(e.localPosition);
    final before = _input.owner;
    final out = _input.down(e.pointer, p.dx, p.dy, _now);
    if (before == null && _input.owner != null) {
      ref.read(deviceFeedbackProvider).prepare();
    }
    _after(out);
  }

  void _move(PointerMoveEvent e) {
    if (e.pointer != _input.owner) return;
    final p = _units(e.localPosition);
    _after(_input.move(e.pointer, p.dx, p.dy, _now));
  }

  void _up(PointerUpEvent e) => _after(_input.up(e.pointer, _now));

  void _cancel(PointerCancelEvent e) => _after(_input.cancel(e.pointer, _now));

  void _step(int steps) => _bus.emit(WheelStep(steps, 1));

  @override
  Widget build(BuildContext context) {
    final extent = ClickWheel.extentFor(widget.diameter);
    final enabled = _bus.enabled.value;
    return ValueListenableBuilder<WheelSemantics>(
      valueListenable: _bus.semantics,
      builder: (context, s, child) => Semantics(
        container: true,
        label: s.label,
        value: s.value,
        // Flutter needs both or neither: at an end the value stays.
        increasedValue: s.increasedValue.isEmpty ? s.value : s.increasedValue,
        decreasedValue: s.decreasedValue.isEmpty ? s.value : s.decreasedValue,
        enabled: enabled,
        onIncrease: enabled ? () => _step(1) : null,
        onDecrease: enabled ? () => _step(-1) : null,
        customSemanticsActions: enabled
            ? {
                const CustomSemanticsAction(
                  label: WheelActionLabels.select,
                ): () =>
                    _bus.press(WheelButton.centre),
                const CustomSemanticsAction(
                  label: WheelActionLabels.menu,
                ): () =>
                    _bus.press(WheelButton.menu),
                const CustomSemanticsAction(
                  label: WheelActionLabels.previous,
                ): () =>
                    _bus.press(WheelButton.previous),
                const CustomSemanticsAction(
                  label: WheelActionLabels.next,
                ): () =>
                    _bus.press(WheelButton.next),
                CustomSemanticsAction(
                  label: s.playing
                      ? WheelActionLabels.pause
                      : WheelActionLabels.play,
                ): () =>
                    _bus.press(WheelButton.playPause),
              }
            : null,
        excludeSemantics: true,
        child: child,
      ),
      child: HitGate(
        accepts: (local, size) {
          final r = widget.diameter / 2;
          final d = local - size.center(Offset.zero);
          return d.distance <= r * WheelZones.ringOuter;
        },
        child: Listener(
          behavior: HitTestBehavior.opaque,
          onPointerDown: _down,
          onPointerMove: _move,
          onPointerUp: _up,
          onPointerCancel: _cancel,
          child: SizedBox.square(
            dimension: extent,
            child: Center(
              child: CustomPaint(
                size: Size.square(widget.diameter),
                painter: WheelPainter(
                  devicePaint: widget.paint,
                  visual: _input.visual,
                  disabled: !enabled,
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// Keyboard and switch control (design section 9): arrows up and down move
/// the selection, Return is Select, Escape is Menu, Space is Play/Pause, left
/// and right arrows are Previous and Next.
class WheelKeyboard extends ConsumerWidget {
  /// Routes keys pressed while [child] has focus to the wheel bus.
  const WheelKeyboard({required this.child, this.enabled = true, super.key});

  /// The device view.
  final Widget child;

  /// False while something else (the gate panel) must get the keys.
  final bool enabled;

  /// The key map.
  static WheelButton? buttonFor(LogicalKeyboardKey key) => switch (key) {
    LogicalKeyboardKey.enter ||
    LogicalKeyboardKey.numpadEnter ||
    LogicalKeyboardKey.select => WheelButton.centre,
    LogicalKeyboardKey.escape => WheelButton.menu,
    LogicalKeyboardKey.space => WheelButton.playPause,
    LogicalKeyboardKey.arrowLeft => WheelButton.previous,
    LogicalKeyboardKey.arrowRight => WheelButton.next,
    _ => null,
  };

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Focus(
      autofocus: true,
      onKeyEvent: (node, event) {
        final bus = ref.read(wheelBusProvider);
        if (!enabled || !bus.enabled.value) return KeyEventResult.ignored;
        final key = event.logicalKey;
        if (event is KeyDownEvent || event is KeyRepeatEvent) {
          if (key == LogicalKeyboardKey.arrowDown) {
            bus.emit(const WheelStep(1, 1));
            return KeyEventResult.handled;
          }
          if (key == LogicalKeyboardKey.arrowUp) {
            bus.emit(const WheelStep(-1, 1));
            return KeyEventResult.handled;
          }
        }
        final button = buttonFor(key);
        if (button == null) return KeyEventResult.ignored;
        if (event is KeyDownEvent) {
          ref.read(deviceFeedbackProvider).buttonDown();
          bus.emit(WheelButtonDown(button));
        } else if (event is KeyUpEvent) {
          ref.read(deviceFeedbackProvider).buttonUp();
          bus.emit(WheelButtonUp(button));
          bus.emit(WheelPress(button));
        }
        return KeyEventResult.handled;
      },
      child: child,
    );
  }
}
