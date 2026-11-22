// SPDX-License-Identifier: MPL-2.0

// The click wheel's touch state machine against the device view design,
// sections 4 (hit zones, press or turn) and 5 (detents, acceleration, holds,
// first touch owns the wheel).

import 'dart:math' as math;

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/firmware_constants.dart';
import 'package:neupod/domain/wheel/now_playing_control.dart';
import 'package:neupod/domain/wheel/wheel_accel.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/domain/wheel/wheel_input.dart';

/// The point at [deg] clockwise from the top, [r] in units of R.
(double, double) at(double deg, [double r = 0.75]) {
  final t = deg * math.pi / 180;
  return (r * math.sin(t), -r * math.cos(t));
}

extension on WheelInput {
  List<WheelEvent> downAt(double deg, int ms, {double r = 0.75, int p = 1}) {
    final (x, y) = at(deg, r);
    return down(p, x, y, ms);
  }

  List<WheelEvent> moveTo(double deg, int ms, {double r = 0.75, int p = 1}) {
    final (x, y) = at(deg, r);
    return move(p, x, y, ms);
  }

  /// Turns from [from] to [to] degrees in [n] samples, [dt] ms apart.
  List<WheelEvent> sweep(
    double from,
    double to,
    int n,
    int t0,
    int dt, {
    int p = 1,
  }) {
    final out = <WheelEvent>[];
    for (var i = 1; i <= n; i++) {
      out.addAll(moveTo(from + (to - from) * i / n, t0 + i * dt, p: p));
    }
    return out;
  }
}

/// A wheel with Now Playing's holds.
WheelInput np() => WheelInput(holdMs: Map.of(WheelInput.nowPlayingHoldMs));

int stepsIn(List<WheelEvent> events) =>
    events.whereType<WheelStep>().fold(0, (s, e) => s + e.steps);

void main() {
  group('zones', () {
    test('centre, dead band, ring and outside at the design radii', () {
      expect(WheelInput.zoneOf(0, 0), WheelZone.centre);
      expect(WheelInput.zoneOf(0.36, 0), WheelZone.centre);
      expect(WheelInput.zoneOf(0.37, 0), WheelZone.deadBand);
      expect(WheelInput.zoneOf(0, -0.43), WheelZone.deadBand);
      expect(WheelInput.zoneOf(0.44, 0), WheelZone.ring);
      expect(WheelInput.zoneOf(0, 1.0), WheelZone.ring);
      expect(WheelInput.zoneOf(1.06, 0), WheelZone.ring);
      expect(WheelInput.zoneOf(1.07, 0), WheelZone.outside);
    });

    test('angles run clockwise from the top', () {
      expect(WheelInput.angleOf(0, -1), closeTo(0, 1e-9));
      expect(WheelInput.angleOf(1, 0), closeTo(90, 1e-9));
      expect(WheelInput.angleOf(0, 1), closeTo(180, 1e-9));
      expect(WheelInput.angleOf(-1, 0), closeTo(270, 1e-9));
    });

    test('four 90 degree sectors centred on up, right, down, left', () {
      expect(WheelInput.sectorOf(0), WheelButton.menu);
      expect(WheelInput.sectorOf(315), WheelButton.menu);
      expect(WheelInput.sectorOf(44.9), WheelButton.menu);
      expect(WheelInput.sectorOf(45), WheelButton.next);
      expect(WheelInput.sectorOf(90), WheelButton.next);
      expect(WheelInput.sectorOf(134.9), WheelButton.next);
      expect(WheelInput.sectorOf(135), WheelButton.playPause);
      expect(WheelInput.sectorOf(224.9), WheelButton.playPause);
      expect(WheelInput.sectorOf(225), WheelButton.previous);
      expect(WheelInput.sectorOf(314.9), WheelButton.previous);
    });
  });

  group('presses', () {
    for (final (deg, button) in [
      (0.0, WheelButton.menu),
      (90.0, WheelButton.next),
      (180.0, WheelButton.playPause),
      (270.0, WheelButton.previous),
    ]) {
      test('a tap at $deg degrees presses ${button.name}', () {
        final w = WheelInput();
        expect(w.downAt(deg, 100), [WheelButtonDown(button)]);
        expect(w.visual, isA<WheelPressing>());
        expect(w.up(1, 180), [WheelButtonUp(button), WheelPress(button)]);
        expect(w.visual, isA<WheelIdle>());
        expect(w.owner, isNull);
      });
    }

    test('the centre is Select', () {
      final w = WheelInput();
      expect(w.down(1, 0.1, 0.1, 0), [
        const WheelButtonDown(WheelButton.centre),
      ]);
      expect((w.visual as WheelPressing).button, WheelButton.centre);
      expect(w.move(1, 0.3, 0.3, 10), isEmpty); // r = 0.42: still in
      expect(w.up(1, 50), [
        const WheelButtonUp(WheelButton.centre),
        const WheelPress(WheelButton.centre),
      ]);
    });

    test('sliding off the centre past 0.44 R cancels Select', () {
      final w = WheelInput();
      w.down(1, 0, 0, 0);
      expect(w.move(1, 0.45, 0, 10), [
        const WheelButtonCancelled(WheelButton.centre),
      ]);
      expect(w.visual, isA<WheelIdle>());
      expect(w.move(1, 0.1, 0, 20), isEmpty); // back in: still cancelled
      expect(w.up(1, 30), isEmpty);
    });

    test('a touch starting in the dead band or outside is ignored', () {
      final w = WheelInput();
      expect(w.down(1, 0.4, 0, 0), isEmpty);
      expect(w.owner, isNull);
      expect(w.down(2, 1.2, 0, 0), isEmpty);
      expect(w.owner, isNull);
      expect(w.up(1, 10), isEmpty);
      // and the wheel is free for the next touch
      expect(w.downAt(0, 20, p: 3), [const WheelButtonDown(WheelButton.menu)]);
    });

    test('movement under 7.5 degrees is still a press', () {
      final w = WheelInput();
      w.downAt(90, 0);
      expect(w.moveTo(97.4, 10), isEmpty);
      expect(w.moveTo(82.6, 20), isEmpty);
      expect(w.up(1, 30), [
        const WheelButtonUp(WheelButton.next),
        const WheelPress(WheelButton.next),
      ]);
    });

    test('radial movement on the ring is not a turn', () {
      final w = WheelInput();
      w.downAt(180, 0, r: 0.5);
      expect(w.moveTo(180, 10, r: 1.05), isEmpty);
      expect(w.moveTo(180, 20, r: 1.5), isEmpty); // even off the wheel
      expect(w.up(1, 30).last, const WheelPress(WheelButton.playPause));
    });
  });

  group('turns', () {
    test(
      '7.5 degrees from touch-down makes it a turn and cancels the press',
      () {
        final w = WheelInput();
        w.downAt(10, 0); // Menu's sector
        final out = w.moveTo(17.5, 5000);
        expect(out.first, const WheelButtonCancelled(WheelButton.menu));
        expect(w.visual, isA<WheelTouchingRing>());
        expect(
          (w.visual as WheelTouchingRing).angleDegrees,
          closeTo(17.5, 1e-9),
        );
        expect(w.up(1, 5100), isEmpty); // no press on release
      },
    );

    test('the threshold swallows nothing: 2 positions are fed at 7.5', () {
      final w = WheelInput();
      w.downAt(0, 0);
      w.moveTo(7.5, 1000);
      expect(w.accel.residue, 2); // 2 counts, under a step
      expect(w.moveTo(15, 2000), [const WheelStep(1, 1)]); // 4 counts
    });

    test('15 degrees is one list step at a slow speed; 24 a turn', () {
      final w = WheelInput();
      w.downAt(0, 0);
      // 360 degrees in 1-degree samples, 20 ms apart: 50 degrees a second
      final out = w.sweep(0, 360, 360, 0, 20);
      expect(stepsIn(out), 24);
      expect(
        out.whereType<WheelStep>().every((e) => e.multiplier == 1),
        isTrue,
      );
    });

    test('anticlockwise is negative', () {
      final w = WheelInput();
      w.downAt(200, 0);
      final out = w.sweep(200, 200 - 90, 90, 0, 20);
      expect(stepsIn(out), -6);
    });

    test('crossing the top (0/360) unwraps the angle', () {
      final w = WheelInput();
      w.downAt(350, 0);
      final out = w.sweep(350, 380, 30, 0, 20); // to 20 degrees
      expect(stepsIn(out), 2);
    });

    test('a fast spin accelerates as the firmware does', () {
      final w = WheelInput();
      final ref = WheelAccel();
      w.downAt(0, 0);
      // 7.5 degrees (two positions) every 8 ms, a 120 Hz touch panel:
      // about 940 degrees a second.
      var steps = 0;
      var refSteps = 0;
      final mults = <int>{};
      for (var i = 1; i <= 96; i++) {
        final out = w.moveTo(7.5 * i + 0.01, 8 * i);
        steps += stepsIn(out);
        mults.addAll(out.whereType<WheelStep>().map((e) => e.multiplier));
        refSteps += ref.feed(2, 8 * i);
      }
      expect(steps, refSteps);
      expect(steps, greaterThan(48)); // 2 turns unaccelerated = 48 steps
      expect(mults, contains(2)); // tool/wheel_vectors/accel_speeds.dart
    });

    test('positions round to the nearest, so the cells are even both ways', () {
      final w = WheelInput();
      w.downAt(90, 0);
      w.moveTo(90 + 7.5, 1000); // +2 positions
      w.moveTo(90 + 9.3, 2000); // 2.48: still 2
      expect(w.accel.residue, 2);
      w.moveTo(90 + 9.4, 3000); // 2.51: 3
      expect(w.accel.residue, 3);
      final b = WheelInput();
      b.downAt(90, 0);
      b.moveTo(90 - 7.5, 1000);
      expect(b.accel.residue, -2);
    });

    test('once turning, the angle is tracked over the centre and outside', () {
      final w = WheelInput();
      w.downAt(0, 0);
      w.moveTo(30, 1000); // turning, 8 positions = 2 steps
      // across the centre at 0.3 R and out beyond the ring
      expect(stepsIn(w.moveTo(60, 2000, r: 0.3)), 2);
      expect(stepsIn(w.moveTo(90, 3000, r: 1.6)), 2);
    });

    test('samples within 0.2 R of the centre are ignored', () {
      final w = WheelInput();
      w.downAt(0, 0);
      w.moveTo(30, 1000);
      expect(w.moveTo(180, 2000, r: 0.1), isEmpty);
      // the next good sample continues from 30 degrees, not from 180
      expect(stepsIn(w.moveTo(45, 3000)), 1);
    });

    test('a reversal drops the residue of the old direction', () {
      final w = WheelInput();
      w.downAt(0, 0);
      w.moveTo(26.25, 1000); // 7 positions: 1 step, residue 3
      expect(w.accel.residue, 3);
      final out = w.moveTo(22.5, 2000); // back one position
      expect(out, isEmpty);
      expect(w.accel.residue, -1);
    });

    test('a turn never becomes a press again', () {
      final w = WheelInput();
      w.downAt(90, 0);
      w.moveTo(100, 100);
      w.moveTo(90, 200); // back where it started
      expect(w.tick(100000), isEmpty);
      expect(w.up(1, 300), isEmpty);
    });
  });

  group('holds', () {
    test('by default no button holds: Previous and Next are plain presses, '
        'as on every firmware screen but Now Playing', () {
      for (final deg in [90.0, 270.0, 0.0]) {
        final w = WheelInput();
        w.downAt(deg, 0);
        expect(w.nextDeadlineMs, isNull);
        expect(w.tick(5000), isEmpty);
        expect(w.up(1, 5000).last, isA<WheelPress>());
      }
    });

    test(
      'Now Playing: Previous and Next hold after 600 ms and do not press',
      () {
        for (final (deg, b) in [
          (90.0, WheelButton.next),
          (270.0, WheelButton.previous),
        ]) {
          final w = np();
          w.downAt(deg, 1000);
          expect(w.nextDeadlineMs, 1000 + uiHoldMs);
          expect(w.tick(1000 + uiHoldMs - 1), isEmpty);
          expect(w.tick(1000 + uiHoldMs), [WheelHoldStart(b)]);
          expect(w.nextDeadlineMs, isNull);
          expect(w.tick(5000), isEmpty); // once
          expect(w.moveTo(deg + 40, 5100), isEmpty); // a hold stays a hold
          expect(w.up(1, 6000), [WheelButtonUp(b), WheelHoldEnd(b)]);
        }
      },
    );

    test('released before 600 ms: a press', () {
      final w = np();
      w.downAt(90, 0);
      expect(w.tick(uiHoldMs - 1), isEmpty);
      expect(w.up(1, uiHoldMs - 1).last, const WheelPress(WheelButton.next));
    });

    test('Menu and Play/Pause hold nothing on Now Playing; held still they '
        'stay undecided and press on release', () {
      for (final deg in [0.0, 180.0]) {
        final w = np();
        w.downAt(deg, 0);
        expect(w.nextDeadlineMs, isNull);
        expect(w.tick(10000), isEmpty);
        expect(w.up(1, 10000).last, isA<WheelPress>());
      }
    });

    test('held Menu that then turns is a turn', () {
      final w = WheelInput();
      w.downAt(0, 0);
      w.tick(5000);
      expect(
        w.moveTo(10, 5100).first,
        const WheelButtonCancelled(WheelButton.menu),
      );
    });

    test('the centre holds after 500 ms only when the screen asks', () {
      final plain = WheelInput();
      plain.down(1, 0, 0, 0);
      expect(plain.tick(10000), isEmpty);
      expect(plain.up(1, 10000).last, const WheelPress(WheelButton.centre));

      final n = np();
      n.down(1, 0, 0, 0);
      expect(n.tick(uiSelHoldMs), [const WheelHoldStart(WheelButton.centre)]);
      expect(n.up(1, 900), [
        const WheelButtonUp(WheelButton.centre),
        const WheelHoldEnd(WheelButton.centre),
      ]);
    });

    test('a hold whose tick came late is still a hold on release, never a '
        'press', () {
      final w = np();
      w.downAt(90, 0);
      expect(w.up(1, uiHoldMs + 50), [
        const WheelHoldStart(WheelButton.next),
        const WheelButtonUp(WheelButton.next),
        const WheelHoldEnd(WheelButton.next),
      ]);
    });

    test('Search: Menu held 600 ms (the screen adds it) holds', () {
      final w = WheelInput()..holdMs[WheelButton.menu] = 600;
      w.downAt(0, 0);
      expect(w.tick(600), [const WheelHoldStart(WheelButton.menu)]);
    });

    test('the hold deadline survives the clock wrap', () {
      final w = np();
      w.downAt(90, 0xFFFFFF00);
      expect(w.nextDeadlineMs, u32(0xFFFFFF00 + uiHoldMs));
      expect(w.tick(u32(0xFFFFFF00 + uiHoldMs - 1)), isEmpty);
      expect(w.tick(u32(0xFFFFFF00 + uiHoldMs)), hasLength(1));
    });
  });

  group('ownership and cancellation', () {
    test('the first touch owns the wheel; others are ignored', () {
      final w = WheelInput();
      w.downAt(90, 0, p: 1);
      expect(w.downAt(270, 10, p: 2), isEmpty);
      expect(w.moveTo(200, 20, p: 2), isEmpty);
      expect(w.up(2, 30), isEmpty);
      expect(w.owner, 1);
      expect(w.up(1, 40).last, const WheelPress(WheelButton.next));
      expect(w.downAt(270, 50, p: 2), isNotEmpty); // free again
    });

    test('a system cancel drops a press without acting and cancels a hold', () {
      final w = np();
      w.downAt(0, 0);
      expect(w.cancel(1, 10), [const WheelButtonCancelled(WheelButton.menu)]);
      w.downAt(90, 100);
      w.tick(100 + uiHoldMs);
      // Cancelled, then ended: what the hold started must not be applied.
      expect(w.cancel(1, 1000), [
        const WheelButtonCancelled(WheelButton.next),
        const WheelHoldEnd(WheelButton.next),
      ]);
      w.downAt(0, 2000);
      w.moveTo(40, 2100);
      expect(w.cancel(1, 2200), isEmpty);
      expect(w.owner, isNull);
    });

    test('reset and disabling cancel a held centre the same way', () {
      final w = np();
      w.down(1, 0, 0, 0);
      w.tick(uiSelHoldMs);
      expect(w.reset(), [
        const WheelButtonCancelled(WheelButton.centre),
        const WheelHoldEnd(WheelButton.centre),
      ]);
      w.down(1, 0, 0, 1000);
      w.tick(1000 + uiSelHoldMs);
      expect(w.setEnabled(false, 2000), [
        const WheelButtonCancelled(WheelButton.centre),
        const WheelHoldEnd(WheelButton.centre),
      ]);
    });

    test('reset drops the touch', () {
      final w = WheelInput();
      w.down(1, 0, 0, 0);
      expect(w.reset(), [const WheelButtonCancelled(WheelButton.centre)]);
      expect(w.up(1, 10), isEmpty);
    });

    test('a disabled wheel ignores touches; disabling cancels one', () {
      final w = WheelInput();
      w.downAt(0, 0);
      expect(w.setEnabled(false, 5), [
        const WheelButtonCancelled(WheelButton.menu),
      ]);
      expect(w.downAt(0, 10), isEmpty);
      expect(w.setEnabled(false, 15), isEmpty);
      expect(w.setEnabled(true, 20), isEmpty);
      expect(w.downAt(0, 30), isNotEmpty);
    });
  });

  test('a scan cancelled by the system is not applied (wheel events driving '
      'NowPlayingControl as lib/domain/wheel/README.md says)', () {
    const track = NpcTrack(
      seekable: true,
      posFrames: 44100 * 60,
      totalFrames: 44100 * 240,
      rate: 44100,
    );
    final w = np();
    final c = NowPlayingControl();
    var commits = 0;
    void feed(List<WheelEvent> events, int ms) {
      for (final e in events) {
        switch (e) {
          case WheelButtonDown(button: WheelButton.next):
            c.keyDown(1, track, ms);
          case WheelButtonUp(button: WheelButton.next):
            if (c.keyUp(1, ms) & NpcEvent.commit != 0) commits++;
          case WheelButtonCancelled(button: WheelButton.next):
            c.cancelHold();
          case _:
            break;
        }
      }
      if (c.tick(track, ms) & NpcEvent.commit != 0) commits++;
    }

    feed(w.downAt(90, 0), 0);
    for (var ms = 100; ms <= 3000; ms += 100) {
      feed(w.tick(ms), ms);
    }
    expect(c.pending, isTrue); // scanning
    feed(w.cancel(1, 3100), 3100);
    expect(c.pending, isFalse);
    for (var ms = 3200; ms <= 40000; ms += 100) {
      feed(const [], ms);
    }
    expect(commits, 0); // nothing applied, not at the end nor after 30 s
  });
}
