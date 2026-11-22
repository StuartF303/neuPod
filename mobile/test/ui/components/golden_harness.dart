// SPDX-License-Identifier: MPL-2.0

// Shared pieces of the component goldens: the two designed styles, the three
// text tiers, placeholder art like the mock-ups', and a sheet that renders a
// component on its style's ground at device pixel ratio 1.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/style/art_palette.dart';
import 'package:neupod/ui/style/backdrop.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/text_tier.dart';

/// The mock-ups' Now Playing art: `#16324f` and `#d9734e`.
const mockArt = ArtPalette(dominant: 0xd9734e, secondary: 0x16324f);

/// The two designed styles, Modern with the mock-ups' art colours.
final Map<String, NeupodStyle> designedStyles = {
  'classic_light': NeupodStyles.maybeOf(StyleFamily.classic, StyleMode.light)!,
  'modern_dark': NeupodStyles.maybeOf(
    StyleFamily.modern,
    StyleMode.dark,
    art: mockArt,
  )!,
};

/// A placeholder cover: two colours split on the diagonal, as the mock-ups'
/// `linear-gradient(135deg, a 50%, b 50%)`.
class TestArt extends StatelessWidget {
  const TestArt(this.a, this.b, {super.key});

  final Color a;
  final Color b;

  static const harbour = TestArt(Color(0xff16324f), Color(0xffd9734e));
  static const orbital = TestArt(Color(0xff5a3e1b), Color(0xffe9c46a));

  @override
  Widget build(BuildContext context) => CustomPaint(painter: _Split(a, b));
}

class _Split extends CustomPainter {
  _Split(this.a, this.b);

  final Color a;
  final Color b;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = b);
    canvas.drawPath(
      Path()
        ..moveTo(0, 0)
        ..lineTo(size.width, 0)
        ..lineTo(0, size.height)
        ..close(),
      Paint()..color = a,
    );
  }

  @override
  bool shouldRepaint(_Split old) => false;
}

/// Renders [build] for every designed style and text tier, [height] design px
/// tall (a function of the tier) and 600 wide, on the style's ground (and
/// Modern's backdrop), and compares it with `goldens/<name>.<style>.<tier>.png`.
void goldenSheet(
  String name,
  double Function(TextTier tier) height,
  Widget Function(NeupodStyle style, TextTier tier) build, {
  List<TextTier> tiers = TextTier.values,
}) {
  for (final MapEntry(key: styleName, value: style) in designedStyles.entries) {
    for (final tier in tiers) {
      testWidgets(
        '$name, $styleName, ${tier.name}',
        (tester) => withShadows(() async {
          final h = height(tier);
          tester.view.physicalSize = Size(600, h);
          tester.view.devicePixelRatio = 1;
          addTearDown(tester.view.reset);
          await tester.pumpWidget(
            Directionality(
              textDirection: TextDirection.ltr,
              child: MediaQuery(
                data: const MediaQueryData(size: Size(600, 0)),
                child: NeupodTheme(
                  style: style,
                  tier: tier,
                  child: DefaultTextStyle(
                    style: NeupodType(tier).body(style.tokens.ink),
                    child: RepaintBoundary(
                      key: const ValueKey('sheet'),
                      child: Stack(
                        fit: StackFit.expand,
                        children: [
                          if (style.drawsBackdrop)
                            ModernBackdrop(
                              ground: style.ground,
                              palette: style.palette,
                            )
                          else
                            ColoredBox(color: style.ground),
                          build(style, tier),
                        ],
                      ),
                    ),
                  ),
                ),
              ),
            ),
          );
          await expectGoldenWithShadows(
            tester,
            find.byKey(const ValueKey('sheet')),
            'goldens/$name.$styleName.${tier.name}.png',
          );
        }),
      );
    }
  }
}

/// Runs [body] with BoxShadows blurred as on a phone: flutter_test draws
/// them unblurred by default, and the design's soft shadows are part of the
/// look. The blur is deterministic. The flag is restored before the test
/// ends, as flutter_test requires.
Future<void> withShadows(Future<void> Function() body) async {
  debugDisableShadows = false;
  try {
    await body();
  } finally {
    debugDisableShadows = true;
  }
}

/// Compares with a golden, then turns the unblurred-shadow default back on
/// (for tests that turned blur on themselves before pumping).
Future<void> expectGoldenWithShadows(
  WidgetTester tester,
  Finder finder,
  String golden,
) async {
  try {
    await expectLater(finder, matchesGoldenFile(golden));
  } finally {
    debugDisableShadows = true;
  }
}
