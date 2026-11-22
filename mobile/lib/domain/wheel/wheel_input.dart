// SPDX-License-Identifier: MPL-2.0

import 'dart:math' as math;

import 'package:neupod/domain/wheel/firmware_constants.dart';
import 'package:neupod/domain/wheel/wheel_accel.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';

/// The wheel's geometry and thresholds, from the device view design
/// (docs/design/2026-11-21-app-device-view, sections 4 and 5). Radii are in
/// units of R, the drawn wheel's radius.
abstract final class WheelZones {
  /// Centre button: 0 to 0.36 R.
  static const double centre = 0.36;

  /// Dead band: 0.36 R to 0.44 R; the ring starts here.
  static const double ringInner = 0.44;

  /// The ring's touch area ends 6 % beyond the drawn edge.
  static const double ringOuter = 1.06;

  /// Angle samples closer than this to the centre are ignored.
  static const double angleDeadRadius = 0.2;

  /// A ring touch that moves this far round the centre is a turn.
  static const double turnDegrees = 7.5;

  /// Finger travel per wheel position: 360 / 96 = 3.75 degrees.
  static const double degreesPerPosition = 360 / halWheelPositions;
}

/// Where a point falls, in units of R from the wheel's centre.
enum WheelZone {
  /// Select.
  centre,

  /// Between the centre and the ring: a touch starting here is ignored.
  deadBand,

  /// Turning and the four buttons.
  ring,

  /// Outside the wheel.
  outside,
}

/// What the wheel looks like now (section 4's states, except Disabled, which
/// the view draws from its own flag).
sealed class WheelVisual {
  const WheelVisual();
}

/// Nothing touched.
final class WheelIdle extends WheelVisual {
  /// The idle state.
  const WheelIdle();
}

/// A finger turning on the ring: the 50 degree wedge follows it.
final class WheelTouchingRing extends WheelVisual {
  /// The ring state at [angleDegrees] clockwise from the top.
  const WheelTouchingRing(this.angleDegrees);

  /// Clockwise from the top, 0 to 360.
  final double angleDegrees;
}

/// A button or the centre is pressed: its sector (or the centre) is tinted.
final class WheelPressing extends WheelVisual {
  /// The pressed state.
  const WheelPressing(this.button);

  /// Which control.
  final WheelButton button;
}

/// The click wheel's touch state machine: turns finger samples into
/// [WheelEvent]s. Pure Dart; the widget feeds it pointer samples and the
/// time, and calls [tick] at [nextDeadlineMs] for the holds.
///
/// Coordinates are in units of R from the wheel's centre, x to the right and
/// y down. Times are milliseconds from a monotonic clock (unsigned 32-bit, as
/// the firmware's tick).
///
/// Rules (design section 4, Press or turn, and section 5):
///
/// - The first touch that lands on the wheel owns it until it lifts; others
///   are ignored. A touch starting in the dead band or outside 1.06 R is
///   ignored and does not own the wheel, so another finger may then take it.
/// - A touch on the centre is a Select press while it stays within 0.44 R;
///   sliding out cancels it.
/// - A touch on the ring is undecided. Its button (the 90 degree sector under
///   touch-down) is shown and clicked at once ([WheelButtonDown]). Once the
///   finger's angle about the centre has moved 7.5 degrees from touch-down it
///   is a turn until it lifts: the press is cancelled, and the positions
///   counted from touch-down (3.75 degrees each, whole positions only, the
///   fraction kept) feed the port of `ui_accel_feed()`.
/// - Lifted before that: a press. Held for the button's hold time
///   ([holdMs]) before that: a hold, which then stays a hold until it lifts.
///   A button without a hold time stays undecided while held still.
/// - While turning, the angle is tracked wherever the finger goes; samples
///   closer than 0.2 R to the centre are ignored.
class WheelInput {
  /// A wheel with the given hold times.
  WheelInput({Map<WheelButton, int>? holdMs})
    : holdMs = holdMs ?? Map.of(defaultHoldMs);

  /// No holds by default: every press is a plain press, as on the firmware,
  /// where Previous and Next skip on every screen but Now Playing. Screens
  /// add the holds they act on:
  ///
  /// - Now Playing: Previous and Next at [uiHoldMs] (600 ms, scan) and the
  ///   centre at [uiSelHoldMs] (500 ms, seek mode), as [nowPlayingHoldMs];
  ///   it leaves the centre out while in seek mode, where Select is "go";
  /// - Search (the letter ring): Menu at 600 ms clears the text
  ///   (`CLEAR_HOLD_MS` in `app/ui/input/ui_input.c`);
  /// - Play/Pause holds nothing in R1 (the design's Open question 4).
  static const Map<WheelButton, int> defaultHoldMs = {};

  /// The holds of Now Playing (`UI_HOLD_MS`, `UI_SELHOLD_MS`).
  static const Map<WheelButton, int> nowPlayingHoldMs = {
    WheelButton.previous: uiHoldMs,
    WheelButton.next: uiHoldMs,
    WheelButton.centre: uiSelHoldMs,
  };

  /// The hold time of each button that has one, in ms. Screens change it
  /// (see [defaultHoldMs]).
  final Map<WheelButton, int> holdMs;

  /// The acceleration port the turn feeds.
  final WheelAccel accel = WheelAccel();

  /// False for any moment the wheel cannot act (the design's Disabled state;
  /// no R1 screen uses it, since Search is typed with the wheel, owner
  /// decision 12): touches are ignored. Turning it off cancels a touch in progress (see
  /// [setEnabled]).
  bool get enabled => _enabled;
  bool _enabled = true;

  int? _pointer;
  _Kind _kind = _Kind.none;
  WheelButton? _button;
  int _downMs = 0;
  double _lastAngle = 0;
  double _travel = 0; // degrees from touch-down, unwrapped
  int _fedPositions = 0;
  double _angle = 0; // the finger's current angle, for the wedge

  /// The pointer that owns the wheel, or null.
  int? get owner => _pointer;

  /// What to draw.
  WheelVisual get visual => switch (_kind) {
    _Kind.none || _Kind.dead => const WheelIdle(),
    _Kind.turning => WheelTouchingRing(_angle),
    _Kind.pressing || _Kind.holding => WheelPressing(_button!),
  };

  /// When [tick] must next be called (a hold may take effect), or null.
  int? get nextDeadlineMs {
    if (_kind != _Kind.pressing) return null;
    final hold = holdMs[_button];
    return hold == null ? null : u32(_downMs + hold);
  }

  /// The zone of the point ([x], [y]) in units of R.
  static WheelZone zoneOf(double x, double y) {
    final r = math.sqrt(x * x + y * y);
    if (r <= WheelZones.centre) return WheelZone.centre;
    if (r < WheelZones.ringInner) return WheelZone.deadBand;
    if (r <= WheelZones.ringOuter) return WheelZone.ring;
    return WheelZone.outside;
  }

  /// Clockwise from the top, in degrees, 0 (inclusive) to 360 (exclusive).
  static double angleOf(double x, double y) {
    final a = math.atan2(x, -y) * 180 / math.pi;
    return a < 0 ? a + 360 : a;
  }

  /// The ring button whose 90 degree sector holds [angleDegrees]: Menu from
  /// 315 to 45, Next from 45 to 135, Play/Pause from 135 to 225, Previous
  /// from 225 to 315 (each sector includes its anticlockwise edge).
  static WheelButton sectorOf(double angleDegrees) {
    final a = (angleDegrees + 45) % 360;
    if (a < 90) return WheelButton.menu;
    if (a < 180) return WheelButton.next;
    if (a < 270) return WheelButton.playPause;
    return WheelButton.previous;
  }

  /// Turns the wheel off or on. Turning it off mid-touch cancels the touch.
  List<WheelEvent> setEnabled(bool value, int nowMs) {
    if (value == _enabled) return const [];
    final out = value ? const <WheelEvent>[] : _abandon();
    _enabled = value;
    return out;
  }

  /// A finger went down at ([x], [y]).
  List<WheelEvent> down(int pointer, double x, double y, int nowMs) {
    if (!_enabled || _pointer != null) return const [];
    final zone = zoneOf(x, y);
    if (zone == WheelZone.deadBand || zone == WheelZone.outside) {
      return const [];
    }
    _pointer = pointer;
    _downMs = u32(nowMs);
    if (zone == WheelZone.centre) {
      _kind = _Kind.pressing;
      _button = WheelButton.centre;
      return const [WheelButtonDown(WheelButton.centre)];
    }
    _kind = _Kind.pressing;
    _angle = _lastAngle = angleOf(x, y);
    _travel = 0;
    _fedPositions = 0;
    _button = sectorOf(_angle);
    return [WheelButtonDown(_button!)];
  }

  /// The owning finger moved to ([x], [y]).
  List<WheelEvent> move(int pointer, double x, double y, int nowMs) {
    if (pointer != _pointer) return const [];
    switch (_kind) {
      case _Kind.none || _Kind.dead || _Kind.holding:
        return const [];
      case _Kind.pressing when _button == WheelButton.centre:
        if (math.sqrt(x * x + y * y) > WheelZones.ringInner) {
          _kind = _Kind.dead; // slid off: cancelled; owns the wheel until up
          return const [WheelButtonCancelled(WheelButton.centre)];
        }
        return const [];
      case _Kind.pressing || _Kind.turning:
        if (math.sqrt(x * x + y * y) < WheelZones.angleDeadRadius) {
          return const []; // the angle is unstable near the centre
        }
        final a = angleOf(x, y);
        var d = a - _lastAngle;
        if (d > 180) d -= 360;
        if (d <= -180) d += 360;
        _lastAngle = a;
        _angle = a;
        _travel += d;
        final out = <WheelEvent>[];
        if (_kind == _Kind.pressing) {
          // (less a hair, so 7.5 degrees in floating point counts)
          if (_travel.abs() < WheelZones.turnDegrees - 1e-9) return const [];
          _kind = _Kind.turning;
          out.add(WheelButtonCancelled(_button!));
          _button = null;
        }
        final step = _feed(nowMs);
        if (step != null) out.add(step);
        return out;
    }
  }

  /// The owning finger lifted.
  List<WheelEvent> up(int pointer, int nowMs) {
    if (pointer != _pointer) return const [];
    final kind = _kind;
    final button = _button;
    final hold = holdMs[button];
    _clear();
    switch (kind) {
      case _Kind.pressing
          when hold != null && u32(u32(nowMs) - _downMs) >= hold:
        // Held long enough but the tick came late: still a hold, never a
        // press.
        return [
          WheelHoldStart(button!),
          WheelButtonUp(button),
          WheelHoldEnd(button),
        ];
      case _Kind.pressing:
        return [WheelButtonUp(button!), WheelPress(button)];
      case _Kind.holding:
        return [WheelButtonUp(button!), WheelHoldEnd(button)];
      case _Kind.none || _Kind.dead || _Kind.turning:
        return const [];
    }
  }

  /// The system took the owning touch (a system gesture, the app going to the
  /// background): a press is cancelled without acting. A hold is cancelled
  /// too ([WheelButtonCancelled], then [WheelHoldEnd]): it did not end with
  /// a release, so nothing it started may be applied (the firmware's
  /// `ui_npc_cancel_hold()`).
  List<WheelEvent> cancel(int pointer, int nowMs) {
    if (pointer != _pointer) return const [];
    return _abandon();
  }

  /// Drops any touch in progress, as [cancel] (a screen change).
  List<WheelEvent> reset() => _abandon();

  /// Lets a hold take effect: call at [nextDeadlineMs] (calling more often is
  /// harmless).
  List<WheelEvent> tick(int nowMs) {
    if (_kind != _Kind.pressing) return const [];
    final hold = holdMs[_button];
    if (hold == null || u32(u32(nowMs) - _downMs) < hold) return const [];
    _kind = _Kind.holding;
    return [WheelHoldStart(_button!)];
  }

  WheelStep? _feed(int nowMs) {
    // Positions counted from touch-down, rounded to the nearest whole one, so
    // the cells are 3.75 degrees everywhere and the same in both directions.
    final positions = (_travel / WheelZones.degreesPerPosition + 0.5).floor();
    final counts = positions - _fedPositions;
    if (counts == 0) return null;
    _fedPositions = positions;
    final steps = accel.feed(counts, nowMs);
    return steps == 0 ? null : WheelStep(steps, accel.multiplier);
  }

  List<WheelEvent> _abandon() {
    final kind = _kind;
    final button = _button;
    _clear();
    return switch (kind) {
      _Kind.pressing => [WheelButtonCancelled(button!)],
      _Kind.holding => [WheelButtonCancelled(button!), WheelHoldEnd(button)],
      _ => const [],
    };
  }

  void _clear() {
    _pointer = null;
    _kind = _Kind.none;
    _button = null;
  }
}

enum _Kind { none, pressing, holding, turning, dead }
