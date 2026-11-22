// SPDX-License-Identifier: MPL-2.0

// Spec suite: the device screen's scale and the text-size tiers, from the
// device view design (docs/design/2026-11-21-app-device-view/README.md,
// sections 1 and 8) and docs/app/README.md (Accessibility: the app honours
// the system text size up to the largest standard setting without clipping).
//
// - The 600x450 screen is scaled by s = 0.88 W / 600 (W the phone's width in
//   points); the screen is never narrower than 320 pt.
// - Three tiers multiply type and bars by k and show fewer rows:
//
//   | Tier     | k   | iOS text size                       | Android scale  | 1-line | 2-line | Status | Row px   |
//   | Standard | 1   | up to Large (the default)           | under 1.15     | 7      | 5      | 45     | 56 / 75  |
//   | Large    | 7/6 | Extra Large, Extra Extra Large      | 1.15 to < 1.3  | 6      | 4      | 52     | 66 / 99  |
//   | Largest  | 7/5 | Extra Extra Extra Large and the AX  | 1.3 and above  | 5      | 3      | 63     | 77 / 129 |
//
// - On-phone sizes at 393 pt: Body 15.0, 17.5 and 21.0 pt; a one-line row 32,
//   38 and 44 pt. Accessibility sizes beyond the largest tier are capped.
//
// WIRING: set the wired* values below to adapters over the app's code; each
// group's skip lifts by itself.
@Tags(['spec'])
library;

import 'package:flutter_test/flutter_test.dart';

enum TextTier { standard, large, largest }

/// iOS Dynamic Type sizes (UIContentSizeCategory), smallest first.
enum IosTextSize {
  extraSmall,
  small,
  medium,
  large,
  extraLarge,
  extraExtraLarge,
  extraExtraExtraLarge,
  accessibilityMedium,
  accessibilityLarge,
  accessibilityExtraLarge,
  accessibilityExtraExtraLarge,
  accessibilityExtraExtraExtraLarge,
}

typedef IosTier = TextTier Function(IosTextSize size);

/// Android's font scale (Configuration.fontScale).
typedef AndroidTier = TextTier Function(double fontScale);

/// A tier's layout values, in design px of the 600x450 screen.
typedef TierMetrics = ({
  double k,
  int rowsOneLine,
  int rowsTwoLines,
  int statusBar,
  int rowOneLine,
  int rowTwoLines,
});

typedef MetricsOf = TierMetrics Function(TextTier tier);

/// The screen's scale s for a phone [width] in points.
typedef ScreenScale = double Function(double width);

// WIRE: replace null with adapters over the app's code.
final IosTier? wiredIosTier = null;
final AndroidTier? wiredAndroidTier = null;
final MetricsOf? wiredTierMetrics = null;
final ScreenScale? wiredScreenScale = null;

Object _skipIf(Object? wired, String item) =>
    wired == null ? 'awaiting implementation of $item' : false;

/// Body type at 600x450 (docs/ui.md, the 600x450 profile: 26 px).
const _bodyPx = 26;

void main() {
  group(
    'iOS text size to tier',
    skip: _skipIf(wiredIosTier, 'the iOS text-size tiers (device view 8)'),
    () {
      const expected = {
        IosTextSize.extraSmall: TextTier.standard,
        IosTextSize.small: TextTier.standard,
        IosTextSize.medium: TextTier.standard,
        IosTextSize.large: TextTier.standard,
        IosTextSize.extraLarge: TextTier.large,
        IosTextSize.extraExtraLarge: TextTier.large,
        IosTextSize.extraExtraExtraLarge: TextTier.largest,
        IosTextSize.accessibilityMedium: TextTier.largest,
        IosTextSize.accessibilityLarge: TextTier.largest,
        IosTextSize.accessibilityExtraLarge: TextTier.largest,
        IosTextSize.accessibilityExtraExtraLarge: TextTier.largest,
        IosTextSize.accessibilityExtraExtraExtraLarge: TextTier.largest,
      };
      for (final MapEntry(key: size, value: tier) in expected.entries) {
        test('${size.name} is ${tier.name}', () {
          expect(wiredIosTier!(size), tier);
        });
      }
    },
  );

  group(
    'Android font scale to tier',
    skip: _skipIf(
      wiredAndroidTier,
      'the Android font-scale tiers '
      '(device view 8)',
    ),
    () {
      const expected = <(double, TextTier)>[
        (0.85, TextTier.standard),
        (1.0, TextTier.standard),
        (1.1, TextTier.standard),
        (1.149, TextTier.standard),
        (1.15, TextTier.large),
        (1.2, TextTier.large),
        (1.299, TextTier.large),
        (1.3, TextTier.largest),
        (1.5, TextTier.largest),
        (2.0, TextTier.largest),
      ];
      for (final (scale, tier) in expected) {
        test('$scale is ${tier.name}', () {
          expect(wiredAndroidTier!(scale), tier);
        });
      }
    },
  );

  group(
    'tier metrics',
    skip: _skipIf(wiredTierMetrics, 'the text-tier metrics (device view 8)'),
    () {
      const table = <TextTier, TierMetrics>{
        TextTier.standard: (
          k: 1,
          rowsOneLine: 7,
          rowsTwoLines: 5,
          statusBar: 45,
          rowOneLine: 56,
          rowTwoLines: 75,
        ),
        TextTier.large: (
          k: 7 / 6,
          rowsOneLine: 6,
          rowsTwoLines: 4,
          statusBar: 52,
          rowOneLine: 66,
          rowTwoLines: 99,
        ),
        TextTier.largest: (
          k: 7 / 5,
          rowsOneLine: 5,
          rowsTwoLines: 3,
          statusBar: 63,
          rowOneLine: 77,
          rowTwoLines: 129,
        ),
      };
      for (final MapEntry(key: tier, value: m) in table.entries) {
        test('${tier.name}: k, rows, status bar and row heights', () {
          final got = wiredTierMetrics!(tier);
          expect(got.k, closeTo(m.k, 1e-9));
          expect(got.rowsOneLine, m.rowsOneLine);
          expect(got.rowsTwoLines, m.rowsTwoLines);
          expect(got.statusBar, m.statusBar);
          expect(got.rowOneLine, m.rowOneLine);
          expect(got.rowTwoLines, m.rowTwoLines);
        });

        test('${tier.name}: the rows and the status bar fit 450 px', () {
          final got = wiredTierMetrics!(tier);
          expect(
            got.statusBar + got.rowsOneLine * got.rowOneLine,
            lessThanOrEqualTo(450),
          );
          expect(
            got.statusBar + got.rowsTwoLines * got.rowTwoLines,
            lessThanOrEqualTo(450),
          );
        });
      }
    },
  );

  group(
    'screen scale',
    skip: _skipIf(wiredScreenScale, 'the device screen scale (device view 1)'),
    () {
      final cases = <(double, double)>[
        (375, 0.550),
        (393, 0.5764),
        (440, 0.6453),
        (364, 0.5339),
        (360, 320 / 600), // the screen stays 320 pt
        (344, 320 / 600), // the narrowest supported phone
      ];
      for (final (w, s) in cases) {
        test('$w pt wide: s = ${s.toStringAsFixed(4)}', () {
          expect(wiredScreenScale!(w), closeTo(s, 5e-5));
        });
      }
    },
  );

  group(
    'on-phone sizes at 393 pt',
    skip: wiredScreenScale == null || wiredTierMetrics == null
        ? 'awaiting implementation of the screen scale and the tier metrics'
        : false,
    () {
      final expected = <TextTier, (double, double)>{
        TextTier.standard: (15.0, 32),
        TextTier.large: (17.5, 38),
        TextTier.largest: (21.0, 44),
      };
      for (final MapEntry(key: tier, value: (body, row)) in expected.entries) {
        test('${tier.name}: Body $body pt, one-line row about $row pt', () {
          final s = wiredScreenScale!(393);
          final m = wiredTierMetrics!(tier);
          expect(_bodyPx * m.k * s, closeTo(body, 0.05));
          expect(m.rowOneLine * s, closeTo(row, 0.5));
        });
      }
      test('the largest tier reaches the 44 pt touch target', () {
        final s = wiredScreenScale!(393);
        expect(
          wiredTierMetrics!(TextTier.largest).rowOneLine * s,
          greaterThanOrEqualTo(44),
        );
      });
    },
  );
}
