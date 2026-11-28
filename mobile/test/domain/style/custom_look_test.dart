// SPDX-License-Identifier: MPL-2.0

import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/custom_look.dart';

void main() {
  group('starters', () {
    test('are Harbour, Sea glass and Clear case, Harbour in use', () {
      final s = CustomLooks.starters;
      expect(s.looks.map((l) => l.name), [
        'Harbour',
        'Sea glass',
        'Clear case',
      ]);
      expect(s.inUse?.name, 'Harbour');
      expect(s.looks[1].body.transparency, 55);
      expect(s.looks[2].body.transparency, 85);
      expect(s.looks[2].wheel.transparency, 60);
      expect(s.looks[2].board, 'copper');
      expect(s.looks.every((l) => l.screen == LookScreen.auto), isTrue);
    });
  });

  group('JSON', () {
    test('round-trips through encode and decode', () {
      final json = jsonDecode(jsonEncode(CustomLooks.starters.toJson()));
      expect(CustomLooks.tryFromJson(json), CustomLooks.starters);
    });

    test('reads the design example, field for field', () {
      final looks = CustomLooks.tryFromJson({
        'looks': [
          {
            'id': 'look-2',
            'name': 'Sea glass',
            'body': {
              'hue': 165,
              'saturation': 35,
              'brightness': 70,
              'transparency': 55,
            },
            'wheel': {
              'hue': 165,
              'saturation': 10,
              'brightness': 96,
              'transparency': 0,
            },
            'centre': {
              'hue': 165,
              'saturation': 40,
              'brightness': 60,
              'transparency': 0,
            },
            'board': 'main-top',
            'screen': 'auto',
          },
        ],
        'lookInUse': 'look-2',
      })!;
      expect(looks.inUse, CustomLooks.starters.looks[1].copyWith(id: 'look-2'));
    });

    test('clamps every number into its range', () {
      final look = CustomLook.tryFromJson({
        'id': 'a',
        'name': 'x',
        'body': {
          'hue': 999,
          'saturation': -4,
          'brightness': 101,
          'transparency': 99,
        },
        'wheel': {'hue': -1, 'saturation': 1000, 'transparency': 70},
        'centre': {'transparency': 61.4, 'hue': 12.6},
      }, fallbackId: 'z')!;
      expect(
        look.body,
        const PartPaint(
          hue: 359,
          saturation: 0,
          brightness: 100,
          transparency: 85,
        ),
      );
      expect(
        look.wheel,
        const PartPaint(
          hue: 0,
          saturation: 100,
          brightness: 0,
          transparency: 60,
        ),
      );
      expect(
        look.centre,
        const PartPaint(
          hue: 13,
          saturation: 0,
          brightness: 0,
          transparency: 60,
        ),
      );
    });

    test('never throws on a bad value', () {
      final look = CustomLook.tryFromJson({
        'id': 7,
        'name': ['no'],
        'body': 'blue',
        'wheel': {'hue': 'red', 'saturation': null, 'brightness': double.nan},
        'board': 'attic',
        'screen': 3,
      }, fallbackId: 'look-9')!;
      expect(look.id, 'look-9');
      expect(look.name, 'Look');
      expect(look.body, const PartPaint(hue: 0, saturation: 0, brightness: 0));
      expect(look.wheel, const PartPaint(hue: 0, saturation: 0, brightness: 0));
      expect(look.board, 'main-top');
      expect(look.screen, LookScreen.auto);
    });

    test('keeps a name to 16 characters', () {
      final look = CustomLook.tryFromJson({
        'id': 'a',
        'name': 'A very long look name indeed',
      }, fallbackId: 'z')!;
      expect(look.name, 'A very long look');
      expect(look.name.length, 16);
    });

    test('truncates a name by characters, not UTF-16 units', () {
      final look = CustomLook.tryFromJson({
        'id': 'a',
        'name': '${'a' * 15}\u{1F600}\u{1F600}',
      }, fallbackId: 'z')!;
      expect(look.name, '${'a' * 15}\u{1F600}');
    });

    test('drops a seventh look and beyond, and repeated ids', () {
      Map<String, Object?> one(String id) => {'id': id, 'name': id};
      final looks = CustomLooks.tryFromJson({
        'looks': [for (var i = 0; i < 9; i++) one('l$i'), one('l0')],
        'lookInUse': 'l8',
      })!;
      expect(looks.looks.map((l) => l.id), [
        'l0',
        'l1',
        'l2',
        'l3',
        'l4',
        'l5',
      ]);
      expect(looks.lookInUse, isNull, reason: 'l8 was dropped');
    });

    test('an empty list stays empty; a wrong shape is null', () {
      expect(CustomLooks.tryFromJson({'looks': <Object?>[]})!.looks, isEmpty);
      expect(CustomLooks.tryFromJson('looks'), isNull);
      expect(CustomLooks.tryFromJson({'looks': 'x'}), isNull);
      expect(CustomLooks.tryFromJson(null), isNull);
    });
  });

  test('mode names', () {
    expect(modeSettingFromName('custom'), ModeSetting.custom);
    expect(modeSettingFromName('dark'), ModeSetting.dark);
    expect(modeSettingFromName('nope'), ModeSetting.auto);
    expect(modeSettingFromName(null), ModeSetting.auto);
  });
}
