// SPDX-License-Identifier: MPL-2.0

import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/art_palette.dart';
import 'package:neupod/domain/style/colour_math.dart';
import 'package:neupod/ui/style/modern_dark.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/text_tier.dart';

int _rgb(Color c) => c.toARGB32() & 0xffffff;

/// [fg] composited over the opaque [bg].
int _over(Color fg, int bg) {
  final a = fg.a;
  final (br, bgc, bb) = channels(bg);
  return rgbOf(
    fg.r * 255 * a + br * (1 - a),
    fg.g * 255 * a + bgc * (1 - a),
    fg.b * 255 * a + bb * (1 - a),
  );
}

void main() {
  group('designed combinations', () {
    test('Classic Light and Modern Dark only (Open question 1)', () {
      expect(
        NeupodStyles.isDesigned(StyleFamily.classic, StyleMode.light),
        isTrue,
      );
      expect(
        NeupodStyles.isDesigned(StyleFamily.modern, StyleMode.dark),
        isTrue,
      );
      expect(
        NeupodStyles.isDesigned(StyleFamily.classic, StyleMode.dark),
        isFalse,
      );
      expect(
        NeupodStyles.isDesigned(StyleFamily.modern, StyleMode.light),
        isFalse,
      );
      expect(NeupodStyles.maybeOf(StyleFamily.classic, StyleMode.dark), isNull);
    });

    test('an undesigned mode resolves to the family\'s designed one', () {
      final s = NeupodStyles.resolve(StyleFamily.classic, StyleMode.dark);
      expect((s.family, s.mode), (StyleFamily.classic, StyleMode.light));
      final m = NeupodStyles.resolve(StyleFamily.modern, StyleMode.light);
      expect((m.family, m.mode), (StyleFamily.modern, StyleMode.dark));
    });

    test('mode follows the phone', () {
      expect(StyleMode.fromBrightness(Brightness.dark), StyleMode.dark);
      expect(StyleMode.fromBrightness(Brightness.light), StyleMode.light);
    });

    test('the same inputs give the same instance', () {
      const art = ArtPalette(dominant: 0xd9734e, secondary: 0x16324f);
      expect(
        identical(
          NeupodStyles.maybeOf(StyleFamily.modern, StyleMode.dark, art: art),
          NeupodStyles.maybeOf(StyleFamily.modern, StyleMode.dark, art: art),
        ),
        isTrue,
      );
    });
  });

  group('contrast: text at least 4.5:1', () {
    test('Classic Light: ink, secondary, tertiary and the reason on white', () {
      final s = NeupodStyles.maybeOf(StyleFamily.classic, StyleMode.light)!;
      final ground = _rgb(s.ground);
      for (final c in [
        s.tokens.ink,
        s.tokens.ink2,
        s.tokens.ink3,
        s.list.reason,
        s.list.number,
      ]) {
        expect(
          contrastRatio(_rgb(c), ground),
          greaterThanOrEqualTo(4.5),
          reason: hexOf(_rgb(c)),
        );
      }
      // The panel's text on white, the flash's text on its lightest stop.
      expect(
        contrastRatio(_rgb(s.panel.body), 0xffffff),
        greaterThanOrEqualTo(4.5),
      );
      expect(
        contrastRatio(_rgb(s.selection.flashText), 0xd6eafd),
        greaterThanOrEqualTo(4.5),
      );
      // White on the selection bar's lower half (its upper half carries the
      // design's 1 px text shadow; see the PR notes).
      expect(contrastRatio(0xffffff, 0x1b62cf), greaterThanOrEqualTo(4.5));
    });

    test('Modern Dark: inks on the list ground over the darkest backdrop', () {
      final s = NeupodStyles.maybeOf(StyleFamily.modern, StyleMode.dark)!;
      final ground = _over(s.list.ground!.color!, _rgb(s.ground));
      for (final c in [
        s.tokens.ink,
        s.tokens.ink2,
        s.tokens.ink3,
        s.list.reason,
        s.list.number,
      ]) {
        expect(
          contrastRatio(_rgb(c), ground),
          greaterThanOrEqualTo(4.5),
          reason: hexOf(_rgb(c)),
        );
      }
    });

    test('Modern Dark: white on the accent, with and without art', () {
      expect(
        contrastRatio(0xffffff, _rgb(modernAccentWithoutArt)),
        greaterThanOrEqualTo(4.5),
      );
      for (final art in [0xd9734e, 0xe9c46a, 0xffffff, 0x40ff40, 0x00e0ff]) {
        final s = NeupodStyles.maybeOf(
          StyleFamily.modern,
          StyleMode.dark,
          art: ArtPalette(dominant: art, secondary: 0x101010),
        )!;
        expect(
          contrastRatio(0xffffff, _rgb(s.tokens.accent)),
          greaterThanOrEqualTo(4.5),
        );
      }
    });
  });

  group('text tiers', () {
    test('the design\'s table', () {
      expect(
        [
          for (final t in TextTier.values)
            (t.rowsOneLine, t.rowsTwoLine, t.statusBarHeight),
        ],
        [(7, 5, 45.0), (6, 4, 52.0), (5, 3, 63.0)],
      );
      expect(
        [for (final t in TextTier.values) (t.rowHeight, t.row2Height)],
        [(56.0, 75.0), (66.0, 99.0), (77.0, 129.0)],
      );
      expect(TextTier.large.k, closeTo(7 / 6, 1e-9));
      expect(TextTier.largest.k, closeTo(7 / 5, 1e-9));
    });

    test('rows fill the screen below the status bar', () {
      for (final t in TextTier.values) {
        expect(
          t.statusBarHeight + t.rowsOneLine * t.rowHeight,
          lessThanOrEqualTo(450),
        );
        expect(
          t.statusBarHeight + t.rowsTwoLine * t.row2Height,
          lessThanOrEqualTo(450),
        );
      }
    });

    test('Android font scale thresholds', () {
      expect(TextTier.fromTextScale(1.0, ios: false), TextTier.standard);
      expect(TextTier.fromTextScale(1.149, ios: false), TextTier.standard);
      expect(TextTier.fromTextScale(1.15, ios: false), TextTier.large);
      expect(TextTier.fromTextScale(1.3, ios: false), TextTier.largest);
      expect(TextTier.fromTextScale(2.0, ios: false), TextTier.largest);
    });

    test('iOS text sizes', () {
      expect(
        TextTier.fromTextScale(1.0, ios: true),
        TextTier.standard,
      ); // Large
      expect(TextTier.fromTextScale(19 / 17, ios: true), TextTier.large); // XL
      expect(TextTier.fromTextScale(21 / 17, ios: true), TextTier.large); // XXL
      expect(
        TextTier.fromTextScale(23 / 17, ios: true),
        TextTier.largest,
      ); // XXXL
      expect(TextTier.fromTextScale(3.1, ios: true), TextTier.largest); // AX5
    });
  });
}
