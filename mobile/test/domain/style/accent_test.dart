// SPDX-License-Identifier: MPL-2.0

import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/accent.dart';
import 'package:neupod/domain/style/art_palette.dart';
import 'package:neupod/domain/style/colour_math.dart';

double _onWhite(int rgb) => contrastRatio(rgb, white);

Uint8List _image(List<(int, int)> colourCounts) {
  final out = <int>[];
  for (final (rgb, n) in colourCounts) {
    final (r, g, b) = channels(rgb);
    for (var i = 0; i < n; i++) {
      out.addAll([r, g, b, 255]);
    }
  }
  return Uint8List.fromList(out);
}

void main() {
  group('contrast', () {
    test('matches WCAG reference values', () {
      expect(contrastRatio(0x000000, 0xffffff), closeTo(21, 1e-9));
      expect(contrastRatio(0xffffff, 0xffffff), closeTo(1, 1e-9));
      // #767676 on white is the classic 4.54:1 grey.
      expect(contrastRatio(0x767676, 0xffffff), closeTo(4.54, 0.01));
    });

    test('HSL round-trips', () {
      for (final c in [0xd9734e, 0xe9c46a, 0x16324f, 0x808080, 0x1858c8]) {
        expect(Hsl.fromRgb(c).toRgb(), c, reason: hexOf(c));
      }
    });
  });

  group('the design\'s worked examples', () {
    // UI styles design, "Modern, Dark: the values": "#d9734e becomes #b8502c
    // (7:1) and #e9c46a becomes #8a5a12 (5.9:1)".
    test('the inputs fail and the drawn outputs pass 4.5:1', () {
      expect(_onWhite(0xd9734e), lessThan(4.5));
      expect(_onWhite(0xe9c46a), lessThan(4.5));
      expect(_onWhite(0xb8502c), greaterThanOrEqualTo(4.5));
      expect(_onWhite(0x8a5a12), greaterThanOrEqualTo(4.5));
    });

    test('the drawn ratios: #8a5a12 is 5.9:1, #b8502c is 5.0:1 (not 7:1)', () {
      expect(_onWhite(0x8a5a12), closeTo(5.91, 0.01));
      expect(_onWhite(0xb8502c), closeTo(4.97, 0.01));
    });

    test('the drawn outputs are fixed points of the rule', () {
      expect(clampAccentForWhiteText(0xb8502c), 0xb8502c);
      expect(clampAccentForWhiteText(0x8a5a12), 0x8a5a12);
    });

    test('the literal rule clamps to the lightest passing colour', () {
      // Hue and saturation kept, lightness lowered just enough. The design's
      // hand-picked colours are darker than this (see accent.dart).
      expect(hexOf(clampAccentForWhiteText(0xd9734e)), '#c5532a');
      expect(hexOf(clampAccentForWhiteText(0xe9c46a)), '#967016');
    });

    test('the clamped colours keep the hue and reach 4.5:1 by a hair', () {
      for (final c in [0xd9734e, 0xe9c46a]) {
        final out = clampAccentForWhiteText(c);
        expect(_onWhite(out), inInclusiveRange(4.5, 4.6), reason: hexOf(out));
        expect(Hsl.fromRgb(out).hue, closeTo(Hsl.fromRgb(c).hue, 1.0));
        expect(Hsl.fromRgb(out).lightness, lessThan(Hsl.fromRgb(c).lightness));
      }
    });
  });

  group('clampAccentForWhiteText', () {
    test('every result reaches 4.5:1 with white', () {
      for (var c = 0; c <= 0xffffff; c += 0x0f0f0f ~/ 3 + 7919) {
        final out = clampAccentForWhiteText(c);
        expect(_onWhite(out), greaterThanOrEqualTo(4.5), reason: hexOf(c));
      }
    });

    test('white and pale colours become a dark grey of the same hue', () {
      final out = clampAccentForWhiteText(0xffffff);
      expect(_onWhite(out), greaterThanOrEqualTo(4.5));
      final (r, g, b) = channels(out);
      expect(r == g && g == b, isTrue);
    });

    test('a dark colour is left alone', () {
      expect(clampAccentForWhiteText(0x1858c8), 0x1858c8);
      expect(clampAccentForWhiteText(0x101010), 0x101010);
    });
  });

  test('progress tint is lighter than the art colour, same hue', () {
    final t = progressTint(0xd9734e);
    expect(hexOf(t), '#e7a790');
    expect(Hsl.fromRgb(t).lightness, closeTo(0.737, 0.01));
    expect(Hsl.fromRgb(t).hue, closeTo(16, 1));
  });

  group('ArtPalette.fromRgba', () {
    test('no opaque pixel gives null', () {
      expect(ArtPalette.fromRgba(Uint8List(0)), isNull);
      expect(ArtPalette.fromRgba(Uint8List.fromList([9, 9, 9, 0])), isNull);
    });

    test('the mock-ups\' placeholder art gives the mock-ups\' colours', () {
      // Half #16324f, half #d9734e: the Now Playing mock-ups wash in #d9734e
      // and use it for the accent.
      final p = ArtPalette.fromRgba(_image([(0x16324f, 50), (0xd9734e, 50)]))!;
      expect(p.dominant, 0xd9734e);
      expect(p.secondary, 0x16324f);
      expect(p.accent, clampAccentForWhiteText(0xd9734e));
      // The album mock-up's art: #5a3e1b and #e9c46a.
      final q = ArtPalette.fromRgba(_image([(0x5a3e1b, 50), (0xe9c46a, 50)]))!;
      expect(q.dominant, 0xe9c46a);
      expect(q.secondary, 0x5a3e1b);
    });

    test('the second wash is the secondary colour brightened 1.35 times', () {
      final p = ArtPalette.fromRgba(_image([(0x16324f, 50), (0xd9734e, 50)]))!;
      expect(
        hexOf(p.washSecondary),
        '#1e446b',
      ); // rgb(30,68,107), mock-up 30,70,110
    });

    test('a larger area wins over a slightly more colourful one', () {
      final p = ArtPalette.fromRgba(_image([(0x2060a0, 80), (0xe03020, 20)]))!;
      expect(p.dominant, 0x2060a0);
    });

    test('near-black and near-white are ignored when there is colour', () {
      final p = ArtPalette.fromRgba(
        _image([(0x000000, 70), (0xffffff, 20), (0x40a040, 10)]),
      )!;
      expect(p.dominant, 0x40a040);
    });

    test('an all-black cover still gives a palette', () {
      final p = ArtPalette.fromRgba(_image([(0x020202, 10)]))!;
      expect(p.dominant, 0x020202);
      expect(_onWhite(p.accent), greaterThanOrEqualTo(4.5));
    });

    test('a single-colour cover gets a darker secondary', () {
      final p = ArtPalette.fromRgba(_image([(0xd9734e, 10)]))!;
      expect(
        Hsl.fromRgb(p.secondary).lightness,
        closeTo(Hsl.fromRgb(0xd9734e).lightness / 2, 0.01),
      );
    });
  });
}
