// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/look_edit.dart';

CustomLook _look(String id, String name) => CustomLook(
  id: id,
  name: name,
  body: const PartPaint(hue: 1, saturation: 2, brightness: 3),
  wheel: const PartPaint(hue: 1, saturation: 2, brightness: 3),
  centre: const PartPaint(hue: 1, saturation: 2, brightness: 3),
);

void main() {
  group('names and ids', () {
    test('Look n is the lowest unused number', () {
      expect(nextLookName(const []), 'Look 1');
      expect(
        nextLookName([_look('a', 'Look 1'), _look('b', 'Look 3')]),
        'Look 2',
      );
      expect(nextLookName(CustomLooks.starters.looks), 'Look 1');
    });

    test('an id number is above every one made and every one in the list', () {
      expect(nextLookIdNumber(0, CustomLooks.starters.looks), 4);
      expect(nextLookIdNumber(9, CustomLooks.starters.looks), 10);
      expect(
        nextLookIdNumber(0, [_look('look-12', 'x'), _look('odd', 'y')]),
        13,
      );
      expect(lookIdNumber('look-7'), 7);
      expect(lookIdNumber('Harbour'), 0);
    });
  });

  group('the list', () {
    final three = CustomLooks.starters;

    test('six at most: a seventh is refused', () {
      var l = three;
      for (var i = 4; i <= 6; i++) {
        l = l.added(_look('look-$i', 'Look $i'));
      }
      expect(l.looks.length, 6);
      expect(l.full, isTrue);
      expect(l.added(_look('look-7', 'Look 7')), same(l));
    });

    test('a new look is in use', () {
      expect(three.added(_look('look-4', 'Look 4')).lookInUse, 'look-4');
    });

    test('deleting the look in use makes the one above it in use', () {
      expect(three.using('look-2').deleted('look-2').lookInUse, 'look-1');
      expect(three.using('look-3').deleted('look-3').lookInUse, 'look-2');
    });

    test('deleting the first makes the new first in use', () {
      final l = three.deleted('look-1');
      expect(l.looks.map((x) => x.id), ['look-2', 'look-3']);
      expect(l.lookInUse, 'look-2');
    });

    test('deleting one not in use keeps the one in use', () {
      expect(three.deleted('look-3').lookInUse, 'look-1');
    });

    test('deleting the last leaves none in use', () {
      var l = three;
      for (final id in ['look-1', 'look-2', 'look-3']) {
        l = l.deleted(id);
      }
      expect(l.looks, isEmpty);
      expect(l.lookInUse, isNull);
      expect(l.inUse, isNull);
    });
  });

  group('reset', () {
    test(
      'the three parts go to the default for the screen; the rest stays',
      () {
        final harbour = CustomLooks.starters.looks.first.copyWith(
          screen: LookScreen.dark,
          board: 'copper',
        );
        final r = resetLook(harbour, phoneIsLight: true);
        expect(
          r.body,
          const PartPaint(hue: 225, saturation: 20, brightness: 8),
        );
        expect(
          r.wheel,
          const PartPaint(hue: 223, saturation: 12, brightness: 14),
        );
        expect(
          r.centre,
          const PartPaint(hue: 228, saturation: 19, brightness: 6),
        );
        expect(r.name, 'Harbour');
        expect(r.board, 'copper');
        expect(r.screen, LookScreen.dark);
      },
    );

    test('Auto follows the phone; the defaults are solid', () {
      final auto = _look('a', 'A');
      expect(
        resetLook(auto, phoneIsLight: true).body,
        const PartPaint(hue: 218, saturation: 4, brightness: 88),
      );
      expect(resetLook(auto, phoneIsLight: false).body.transparency, 0);
    });
  });

  group('the wheel steps', () {
    const p = PartPaint(hue: 205, saturation: 62, brightness: 44);

    test('Hue 5, Saturation 4, Brightness 4, Transparency 5', () {
      expect(stepped(p, 0, 1, LookPart.body).hue, 210);
      expect(stepped(p, 1, 1, LookPart.body).saturation, 64);
      expect(stepped(p, 2, -2, LookPart.body).brightness, 36);
      expect(stepped(p, 3, 3, LookPart.body).transparency, 15);
    });

    test('hue wraps both ways', () {
      const a = PartPaint(hue: 355, saturation: 0, brightness: 0);
      expect(stepped(a, 0, 1, LookPart.body).hue, 0);
      expect(stepped(a.copyWith(hue: 0), 0, -1, LookPart.body).hue, 355);
      expect(stepped(a.copyWith(hue: 359), 0, 1, LookPart.body).hue, 0);
    });

    test('the others stop at their ends', () {
      const a = PartPaint(hue: 0, saturation: 100, brightness: 0);
      expect(stepped(a, 1, 5, LookPart.body), a);
      expect(stepped(a, 2, -1, LookPart.body), a);
      expect(stepped(a, 3, -1, LookPart.body), a);
    });

    test('transparency stops at the most: 85 body, 60 wheel and centre', () {
      const a = PartPaint(
        hue: 0,
        saturation: 0,
        brightness: 0,
        transparency: 55,
      );
      expect(stepped(a, 3, 20, LookPart.body).transparency, 85);
      expect(stepped(a, 3, 20, LookPart.wheel).transparency, 60);
      expect(stepped(a, 3, 20, LookPart.centre).transparency, 60);
    });

    test('a value off the grid first lands on the grid', () {
      const a = PartPaint(hue: 218, saturation: 4, brightness: 88);
      expect(stepped(a, 0, -1, LookPart.body).hue, 215);
      expect(stepped(a, 0, 1, LookPart.body).hue, 220);
      expect(stepped(a, 0, -3, LookPart.body).hue, 205);
      const b = PartPaint(hue: 0, saturation: 98, brightness: 0);
      expect(stepped(b, 1, 1, LookPart.body).saturation, 100);
    });

    test('a full range at rest speed: 72 detents of hue, 25 of saturation', () {
      var a = const PartPaint(hue: 0, saturation: 0, brightness: 0);
      for (var i = 0; i < 72; i++) {
        a = stepped(a, 0, 1, LookPart.body);
      }
      expect(a.hue, 0);
      for (var i = 0; i < 25; i++) {
        a = stepped(a, 1, 1, LookPart.body);
      }
      expect(a.saturation, 100);
    });
  });
}
