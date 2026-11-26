// SPDX-License-Identifier: MPL-2.0

// Spec suite: the touch click wheel's hit zones and press-or-turn rule, from
// the device view design (docs/design/2026-11-21-app-device-view/README.md,
// sections 4 and 5) and owner decision 9.
//
// - Zones, R = D / 2 from the wheel's centre: centre 0 to 0.36 R (Select),
//   dead band 0.36 R to 0.44 R (a touch that starts here is ignored), ring
//   0.44 R to 1.06 R (turning and the four buttons).
// - Buttons: 90 degree sectors of the ring centred on up (Menu), right
//   (Next), down (Play/Pause) and left (Previous).
// - A touch that starts on the ring is undecided until its angle about the
//   centre has moved 7.5 degrees from touch-down: then it is a turn until the
//   finger lifts, and no button fires. Otherwise it is a press of the sector
//   under the touch-down point; the press shows and clicks at touch-down and
//   is cancelled without acting if the touch becomes a turn.
// - Once turning, the angle is tracked wherever the finger goes, except
//   samples closer than 0.2 R to the centre.
// - A touch that starts on the centre is a Select press while it stays within
//   0.44 R; sliding out cancels it.
// - One position is 3.75 degrees (96 a turn), counted from the touch-down
//   angle so the 7.5 degree threshold swallows nothing; 4 positions (15
//   degrees) are one list step. Clockwise is positive.
// - The first touch that lands on the wheel owns it until it lifts.
// - Holds: Previous and Next after 600 ms (scan); the centre on Now Playing
//   after 500 ms (seek mode); nothing for the centre elsewhere in R1.
//   Holding Play/Pause is the design's Open question 4 and holding Menu is
//   open too (see the holds group): neither is asserted.
//
// Coordinates in this suite are points relative to the wheel's centre, x to
// the right and y down (screen coordinates), so clockwise on screen is an
// increasing atan2(y, x). Every test uses R = 100.
//
// WIRING: wiredWheelGeometry and wiredWheelTouch below adapt the app's wheel
// (lib/domain/wheel/wheel_input.dart) to the small types declared here.
//
// The app sets holds per screen, as the firmware does (Previous and Next
// skip on every screen but Now Playing): the adapter gives the wheel
// Now Playing's holds when [centreHolds] is true, and otherwise the scan
// holds of Previous and Next alone, which is what this suite asserts.
@Tags(['spec'])
library;

import 'dart:math' as math;

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/firmware_constants.dart';
import 'package:neupod/domain/wheel/wheel_events.dart' as app;
import 'package:neupod/domain/wheel/wheel_input.dart' as app;

enum WheelZone { centre, deadBand, ring, outside }

enum WheelButton { menu, next, playPause, previous, centre }

/// Hit testing of a wheel of radius R.
abstract interface class WheelGeometry {
  /// The zone of a point ([dx], [dy] from the wheel's centre, y down).
  WheelZone zoneAt(double dx, double dy);

  /// The ring button whose 90 degree sector holds the point's angle (any
  /// radius; the caller checks the zone).
  WheelButton sectorAt(double dx, double dy);
}

typedef WheelGeometryFactory = WheelGeometry Function(double radius);

/// What the wheel reports. Events compare by value.
sealed class WheelEvent {
  const WheelEvent();
}

/// A button or the centre went down: show it pressed and click.
final class PressDown extends WheelEvent {
  const PressDown(this.button);
  final WheelButton button;
  @override
  bool operator ==(Object other) =>
      other is PressDown && other.button == button;
  @override
  int get hashCode => Object.hash('down', button);
  @override
  String toString() => 'PressDown($button)';
}

/// The press ended without acting (the touch became a turn, or slid off the
/// centre).
final class PressCancelled extends WheelEvent {
  const PressCancelled(this.button);
  final WheelButton button;
  @override
  bool operator ==(Object other) =>
      other is PressCancelled && other.button == button;
  @override
  int get hashCode => Object.hash('cancel', button);
  @override
  String toString() => 'PressCancelled($button)';
}

/// The press completed: act on it (Menu, Select, skip, play or pause).
final class Pressed extends WheelEvent {
  const Pressed(this.button);
  final WheelButton button;
  @override
  bool operator ==(Object other) => other is Pressed && other.button == button;
  @override
  int get hashCode => Object.hash('pressed', button);
  @override
  String toString() => 'Pressed($button)';
}

/// A hold took effect (scan starts, seek mode entered).
final class HoldStarted extends WheelEvent {
  const HoldStarted(this.button);
  final WheelButton button;
  @override
  bool operator ==(Object other) =>
      other is HoldStarted && other.button == button;
  @override
  int get hashCode => Object.hash('hold', button);
  @override
  String toString() => 'HoldStarted($button)';
}

/// The finger lifted after a hold had taken effect (no press follows).
final class HoldEnded extends WheelEvent {
  const HoldEnded(this.button);
  final WheelButton button;
  @override
  bool operator ==(Object other) =>
      other is HoldEnded && other.button == button;
  @override
  int get hashCode => Object.hash('holdEnd', button);
  @override
  String toString() => 'HoldEnded($button)';
}

/// The wheel turned by [positions] (3.75 degrees each, fraction kept,
/// clockwise positive) since the last Turned event, or since touch-down for
/// the first.
final class Turned extends WheelEvent {
  const Turned(this.positions);
  final double positions;
  @override
  bool operator ==(Object other) =>
      other is Turned && (other.positions - positions).abs() < 1e-9;
  @override
  int get hashCode => Object.hash('turn', positions.toStringAsFixed(6));
  @override
  String toString() => 'Turned($positions)';
}

/// The press-or-turn state machine. Times are monotonic milliseconds.
abstract interface class WheelTouch {
  List<WheelEvent> down(int pointer, double dx, double dy, int ms);
  List<WheelEvent> move(int pointer, double dx, double dy, int ms);
  List<WheelEvent> up(int pointer, int ms);

  /// Time passing with no pointer event (for holds).
  List<WheelEvent> tick(int ms);
}

/// [centreHolds] is true on Now Playing, where holding the centre enters seek
/// mode, and false elsewhere.
typedef WheelTouchFactory = WheelTouch Function({
  required double radius,
  required bool centreHolds,
});

/// The app's hit testing, in units of R.
final class _Geo implements WheelGeometry {
  _Geo(this.radius);
  final double radius;

  @override
  WheelZone zoneAt(double dx, double dy) => WheelZone.values.byName(
    app.WheelInput.zoneOf(dx / radius, dy / radius).name,
  );

  @override
  WheelButton sectorAt(double dx, double dy) => _button(
    app.WheelInput.sectorOf(app.WheelInput.angleOf(dx / radius, dy / radius)),
  );
}

WheelButton _button(app.WheelButton b) => WheelButton.values.byName(b.name);

/// The app's press-or-turn machine. Its events map to this suite's:
/// [app.WheelButtonUp] is the release half of a press or hold and has no
/// counterpart here; [app.WheelStep]s (accelerated list steps) are left out
/// and the turn is reported as the positions the app counts them from
/// ([app.WheelInput.turnedPositions]), since the last report.
final class _Touch implements WheelTouch {
  _Touch(this.radius, {required bool centreHolds})
    : _w = app.WheelInput(
        holdMs: centreHolds
            ? Map.of(app.WheelInput.nowPlayingHoldMs)
            : {
                app.WheelButton.previous: uiHoldMs,
                app.WheelButton.next: uiHoldMs,
              },
      );

  final double radius;
  final app.WheelInput _w;
  double _reported = 0;

  List<WheelEvent> _map(List<app.WheelEvent> events) {
    final out = <WheelEvent>[
      for (final e in events)
        ?switch (e) {
          app.WheelButtonDown(:final button) => PressDown(_button(button)),
          app.WheelButtonCancelled(:final button) => PressCancelled(
            _button(button),
          ),
          app.WheelPress(:final button) => Pressed(_button(button)),
          app.WheelHoldStart(:final button) => HoldStarted(_button(button)),
          app.WheelHoldEnd(:final button) => HoldEnded(_button(button)),
          app.WheelButtonUp() || app.WheelStep() => null,
        },
    ];
    final turned = _w.turnedPositions;
    if (_w.owner == null) {
      _reported = 0;
    } else if (turned != _reported) {
      out.add(Turned(turned - _reported));
      _reported = turned;
    }
    return out;
  }

  @override
  List<WheelEvent> down(int pointer, double dx, double dy, int ms) =>
      _map(_w.down(pointer, dx / radius, dy / radius, ms));

  @override
  List<WheelEvent> move(int pointer, double dx, double dy, int ms) =>
      _map(_w.move(pointer, dx / radius, dy / radius, ms));

  @override
  List<WheelEvent> up(int pointer, int ms) => _map(_w.up(pointer, ms));

  @override
  List<WheelEvent> tick(int ms) => _map(_w.tick(ms));
}

final WheelGeometryFactory wiredWheelGeometry = _Geo.new;
WheelTouch wiredWheelTouch({
  required double radius,
  required bool centreHolds,
}) => _Touch(radius, centreHolds: centreHolds);

const double _r = 100;

/// A point at [radius] and [degrees] clockwise from straight up.
(double, double) _at(double radius, double degrees) {
  final a = (degrees - 90) * math.pi / 180;
  return (radius * math.cos(a), radius * math.sin(a));
}

WheelTouch _touch({bool centreHolds = false}) =>
    wiredWheelTouch(radius: _r, centreHolds: centreHolds);

double _positions(Iterable<WheelEvent> events) =>
    events.whereType<Turned>().fold(0.0, (s, e) => s + e.positions);

List<WheelEvent> _notTurns(Iterable<WheelEvent> events) =>
    events.where((e) => e is! Turned).toList();

/// Touches down at [fromDeg] on the ring (radius 80) and moves in [stepDeg]
/// steps to [toDeg], each sample [dtMs] apart. Returns every event.
List<WheelEvent> _sweep(
  WheelTouch w, {
  required double fromDeg,
  required double toDeg,
  double stepDeg = 0.5,
  double radius = 80,
  int pointer = 1,
  int startMs = 0,
  int dtMs = 16,
  bool lift = true,
}) {
  final out = <WheelEvent>[];
  var (x, y) = _at(radius, fromDeg);
  out.addAll(w.down(pointer, x, y, startMs));
  var t = startMs;
  final dir = toDeg >= fromDeg ? 1 : -1;
  var d = fromDeg;
  while ((toDeg - d) * dir > 1e-9) {
    d += dir * math.min(stepDeg, (toDeg - d).abs());
    t += dtMs;
    (x, y) = _at(radius, d);
    out.addAll(w.move(pointer, x, y, t));
  }
  if (lift) out.addAll(w.up(pointer, t + dtMs));
  return out;
}

void main() {
  group('wheel hit zones', () {
    WheelGeometry geo() => wiredWheelGeometry(_r);

    final zones = <(double, double, WheelZone)>[
      (0, 0, WheelZone.centre),
      (35.9, 0, WheelZone.centre),
      (0, -35.9, WheelZone.centre),
      (25, 25, WheelZone.centre), // 35.4
      (36.1, 0, WheelZone.deadBand),
      (0, 43.9, WheelZone.deadBand),
      (30, 30, WheelZone.deadBand), // 42.4
      (44.1, 0, WheelZone.ring),
      (-70, 70, WheelZone.ring), // 99.0
      (0, 100, WheelZone.ring), // the drawn edge
      (-105.9, 0, WheelZone.ring), // 6 % beyond the drawn edge
      (75, -74, WheelZone.ring), // 105.4
      (106.1, 0, WheelZone.outside),
      (80, 80, WheelZone.outside), // 113.1
      (0, -200, WheelZone.outside),
    ];
    for (final (x, y, zone) in zones) {
      final r = math.sqrt(x * x + y * y) / _r;
      test('(${x.toStringAsFixed(1)}, ${y.toStringAsFixed(1)}), '
          '${r.toStringAsFixed(3)} R, is ${zone.name}', () {
        expect(geo().zoneAt(x, y), zone);
      });
    }

    final sectors = <(double, WheelButton)>[
      (0, WheelButton.menu),
      (44, WheelButton.menu),
      (-44, WheelButton.menu),
      (46, WheelButton.next),
      (90, WheelButton.next),
      (134, WheelButton.next),
      (136, WheelButton.playPause),
      (180, WheelButton.playPause),
      (224, WheelButton.playPause),
      (226, WheelButton.previous),
      (270, WheelButton.previous),
      (314, WheelButton.previous),
      (316, WheelButton.menu),
    ];
    for (final (deg, button) in sectors) {
      test('${deg.toStringAsFixed(0)} degrees clockwise from up is '
          '${button.name}', () {
        for (final radius in [50.0, 80.0, 105.0]) {
          final (x, y) = _at(radius, deg);
          expect(geo().sectorAt(x, y), button, reason: 'at $radius');
        }
      });
    }
  });

  group('wheel press or turn', () {
    group('presses', () {
      for (final (deg, button) in [
        (0.0, WheelButton.menu),
        (90.0, WheelButton.next),
        (180.0, WheelButton.playPause),
        (270.0, WheelButton.previous),
      ]) {
        test(
          'a tap on ${button.name} shows at touch-down and acts on lift',
          () {
            final w = _touch();
            final (x, y) = _at(80, deg);
            expect(w.down(1, x, y, 0), [PressDown(button)]);
            expect(w.up(1, 100), [Pressed(button)]);
          },
        );
      }

      test('moving 7 degrees is still a press of the touch-down sector', () {
        final w = _touch();
        final events = _sweep(w, fromDeg: 40, toDeg: 47);
        expect(events, [
          const PressDown(WheelButton.menu),
          const Pressed(WheelButton.menu),
        ], reason: 'the sector under touch-down, not under the lift');
      });

      test('there and back under 7.5 degrees is a press', () {
        final w = _touch();
        final events = _sweep(w, fromDeg: 90, toDeg: 97, lift: false);
        for (var d = 96.0; d >= 90; d -= 1) {
          final (x, y) = _at(80, d);
          events.addAll(w.move(1, x, y, 300 + (97 - d).round() * 16));
        }
        events.addAll(w.up(1, 500));
        expect(_positions(events), 0);
        expect(_notTurns(events), [
          const PressDown(WheelButton.next),
          const Pressed(WheelButton.next),
        ]);
      });

      test('a radial move along the ring is not a turn', () {
        final w = _touch();
        final (x0, y0) = _at(95, 180);
        final (x1, y1) = _at(50, 180);
        expect(w.down(1, x0, y0, 0), [const PressDown(WheelButton.playPause)]);
        expect(w.move(1, x1, y1, 50), isEmpty);
        expect(w.up(1, 100), [const Pressed(WheelButton.playPause)]);
      });
    });

    group('turns', () {
      test(
        '7.5 degrees makes a turn: the press is cancelled, nothing on lift',
        () {
          final w = _touch();
          final events = _sweep(w, fromDeg: 0, toDeg: 8);
          expect(_notTurns(events), [
            const PressDown(WheelButton.menu),
            const PressCancelled(WheelButton.menu),
          ]);
        },
      );

      test('no positions before the threshold, then all of them', () {
        final w = _touch();
        final before = _sweep(w, fromDeg: 0, toDeg: 7, lift: false);
        expect(_positions(before), 0);
        final (x, y) = _at(80, 7.6);
        final after = w.move(1, x, y, 500);
        expect(
          _positions(after),
          closeTo(7.6 / 3.75, 1e-6),
          reason: 'counted from touch-down: the threshold swallows nothing',
        );
      });

      test('15 degrees clockwise is 4 positions (one list step)', () {
        final w = _touch();
        expect(_positions(_sweep(w, fromDeg: 10, toDeg: 25)), closeTo(4, 1e-6));
      });

      test('counter-clockwise is negative', () {
        final w = _touch();
        expect(
          _positions(_sweep(w, fromDeg: 100, toDeg: 70)),
          closeTo(-8, 1e-6),
        );
      });

      test('a full turn is 96 positions', () {
        final w = _touch();
        expect(
          _positions(_sweep(w, fromDeg: 0, toDeg: 360, stepDeg: 3.75)),
          closeTo(96, 1e-6),
        );
      });

      test('crossing the left side (atan2 wrap) does not jump', () {
        final w = _touch();
        // 260 to 280 degrees from up passes straight left (180 in atan2).
        expect(
          _positions(_sweep(w, fromDeg: 260, toDeg: 280)),
          closeTo(20 / 3.75, 1e-6),
        );
        final w2 = _touch();
        expect(
          _positions(_sweep(w2, fromDeg: 280, toDeg: 260)),
          closeTo(-20 / 3.75, 1e-6),
        );
      });

      test('once turning, the angle is tracked outside the ring', () {
        final w = _touch();
        final events = _sweep(w, fromDeg: 0, toDeg: 10, lift: false);
        final (x, y) = _at(150, 40);
        events.addAll(w.move(1, x, y, 400));
        events.addAll(w.up(1, 420));
        expect(_positions(events), closeTo(40 / 3.75, 1e-6));
      });

      test('once turning, the angle is tracked over the centre (0.3 R)', () {
        final w = _touch();
        final events = _sweep(w, fromDeg: 0, toDeg: 10, lift: false);
        final (x, y) = _at(30, 30);
        events.addAll(w.move(1, x, y, 400));
        events.addAll(w.up(1, 420));
        expect(_positions(events), closeTo(30 / 3.75, 1e-6));
      });

      test('samples within 0.2 R of the centre are ignored', () {
        final w = _touch();
        final events = _sweep(w, fromDeg: 0, toDeg: 10, lift: false);
        final (x0, y0) = _at(15, 200); // 0.15 R, a wild angle
        expect(_positions(w.move(1, x0, y0, 400)), 0);
        final (x1, y1) = _at(80, 20);
        events.addAll(w.move(1, x1, y1, 450));
        events.addAll(w.up(1, 470));
        expect(_positions(events), closeTo(20 / 3.75, 1e-6));
      });

      test('turning never fires a button, whatever the lift point', () {
        final w = _touch();
        final events = _sweep(w, fromDeg: 0, toDeg: 180);
        expect(events.whereType<Pressed>(), isEmpty);
      });
    });

    group('centre', () {
      test('a tap on the centre is Select', () {
        final w = _touch();
        expect(w.down(1, 0, 0, 0), [const PressDown(WheelButton.centre)]);
        expect(w.up(1, 80), [const Pressed(WheelButton.centre)]);
      });

      test('sliding within 0.44 R keeps the press', () {
        final w = _touch();
        w.down(1, 5, 0, 0);
        expect(w.move(1, 43, 0, 30), isEmpty);
        expect(w.up(1, 60), [const Pressed(WheelButton.centre)]);
      });

      test('sliding beyond 0.44 R cancels it', () {
        final w = _touch();
        w.down(1, 5, 0, 0);
        expect(w.move(1, 50, 0, 30), [
          const PressCancelled(WheelButton.centre),
        ]);
        expect(w.up(1, 60), isEmpty);
      });

      test('circling on the centre does not turn the wheel', () {
        final w = _touch();
        final events = <WheelEvent>[...w.down(1, 20, 0, 0)];
        for (var deg = 0; deg <= 180; deg += 10) {
          final (x, y) = _at(20, deg.toDouble());
          events.addAll(w.move(1, x, y, deg));
        }
        events.addAll(w.up(1, 400));
        expect(_positions(events), 0);
        expect(events, [
          const PressDown(WheelButton.centre),
          const Pressed(WheelButton.centre),
        ]);
      });
    });

    group('dead band and outside', () {
      test('a touch that starts in the dead band is ignored', () {
        final w = _touch();
        expect(w.down(1, 40, 0, 0), isEmpty);
        final (x, y) = _at(80, 45);
        expect(w.move(1, x, y, 50), isEmpty);
        expect(w.up(1, 100), isEmpty);
      });

      test('a touch that starts outside 1.06 R is not the wheel\'s', () {
        final w = _touch();
        expect(w.down(1, 110, 0, 0), isEmpty);
        final (x, y) = _at(80, 45);
        expect(w.move(1, x, y, 50), isEmpty);
        expect(w.up(1, 100), isEmpty);
      });
    });

    group('the first touch owns the wheel', () {
      test('a second touch on a button is ignored', () {
        final w = _touch();
        final (mx, my) = _at(80, 0);
        final (nx, ny) = _at(80, 90);
        expect(w.down(1, mx, my, 0), [const PressDown(WheelButton.menu)]);
        expect(w.down(2, nx, ny, 10), isEmpty);
        expect(w.up(2, 20), isEmpty);
        expect(w.up(1, 30), [const Pressed(WheelButton.menu)]);
      });

      test('a second finger moving does not turn the wheel', () {
        final w = _touch();
        final events = _sweep(w, fromDeg: 0, toDeg: 20, lift: false);
        final (x0, y0) = _at(80, 180);
        events.addAll(w.down(2, x0, y0, 400));
        for (var d = 180; d <= 270; d += 5) {
          final (x, y) = _at(80, d.toDouble());
          events.addAll(w.move(2, x, y, 400 + d));
        }
        events.addAll(w.up(2, 700));
        events.addAll(w.up(1, 710));
        expect(_positions(events), closeTo(20 / 3.75, 1e-6));
      });

      test('after the owner lifts, the next touch owns it', () {
        final w = _touch();
        final (mx, my) = _at(80, 0);
        final (nx, ny) = _at(80, 90);
        w.down(1, mx, my, 0);
        w.up(1, 50);
        expect(w.down(2, nx, ny, 100), [const PressDown(WheelButton.next)]);
        expect(w.up(2, 150), [const Pressed(WheelButton.next)]);
      });
    });

    group('holds', () {
      for (final (deg, button) in [
        (90.0, WheelButton.next),
        (270.0, WheelButton.previous),
      ]) {
        test(
          '${button.name} held 600 ms starts a scan; lift ends it, no skip',
          () {
            final w = _touch();
            final (x, y) = _at(80, deg);
            expect(w.down(1, x, y, 1000), [PressDown(button)]);
            expect(w.tick(1599), isEmpty);
            expect(w.tick(1600), [HoldStarted(button)]);
            expect(w.tick(2500), isEmpty);
            expect(w.up(1, 2600), [HoldEnded(button)]);
          },
        );

        test('${button.name} lifted at 599 ms is a short press', () {
          final w = _touch();
          final (x, y) = _at(80, deg);
          w.down(1, x, y, 1000);
          expect(w.tick(1599), isEmpty);
          expect(w.up(1, 1599), [Pressed(button)]);
        });
      }

      test('a turn before 600 ms means no hold', () {
        final w = _touch();
        final events = _sweep(
          w,
          fromDeg: 90,
          toDeg: 110,
          startMs: 1000,
          dtMs: 10,
          lift: false,
        );
        events.addAll(w.tick(1700));
        events.addAll(w.up(1, 1800));
        expect(events.whereType<HoldStarted>(), isEmpty);
        expect(events.whereType<Pressed>(), isEmpty);
      });

      test('the centre on Now Playing held 500 ms enters seek mode', () {
        final w = _touch(centreHolds: true);
        expect(w.down(1, 0, 0, 0), [const PressDown(WheelButton.centre)]);
        expect(w.tick(499), isEmpty);
        expect(w.tick(500), [const HoldStarted(WheelButton.centre)]);
        expect(w.up(1, 900), [const HoldEnded(WheelButton.centre)]);
      });

      test('the centre on Now Playing lifted at 499 ms is a short press', () {
        final w = _touch(centreHolds: true);
        w.down(1, 0, 0, 0);
        expect(w.tick(499), isEmpty);
        expect(w.up(1, 499), [const Pressed(WheelButton.centre)]);
      });

      test('the centre elsewhere has no hold', () {
        final w = _touch();
        w.down(1, 0, 0, 0);
        expect(w.tick(3000), isEmpty);
      });

      // Not asserted: holding Menu (the device view says nothing in R1;
      // Search by the wheel, design PR #107, clears the field on a held Menu)
      // and holding Play/Pause (design Open question 4 is the owner's).
    });
  });
}
