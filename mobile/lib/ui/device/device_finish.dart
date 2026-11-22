// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';

/// The body finishes (design section 3). Colours exactly as the finish table;
/// gradients run top to bottom.
enum DeviceFinish {
  /// Silver: the Light modes.
  silver(
    body: [Color(0xFFECEEF1), Color(0xFFD9DCE1), Color(0xFFC3C7CE)],
    bodyStops: [0, 0.45, 1],
    ring: [Color(0xFFFBFBFC), Color(0xFFE1E4E8)],
    ringEdge: Color(0xFFA9AEB6),
    groove: Color(0xFFB4B9C0),
    centre: [Color(0xFFE6E9ED), Color(0xFFC9CDD4)],
    glyph: Color(0xFF5A5F67),
    wordmark: Color(0xFF8D929A),
    press: Color(0x21000000), // rgba(0,0,0,.13)
    touch: Color(0x12000000), // rgba(0,0,0,.07)
    darkStatusText: true,
  ),

  /// Black: the Dark modes.
  black(
    body: [Color(0xFF1B1C20), Color(0xFF101114), Color(0xFF08090A)],
    bodyStops: [0, 0.5, 1],
    ring: [Color(0xFF2B2D32), Color(0xFF1D1F23)],
    ringEdge: Color(0xFF3A3C42),
    groove: Color(0xFF050506),
    centre: [Color(0xFF15161A), Color(0xFF0B0C0E)],
    glyph: Color(0xFFCFCFCF),
    wordmark: Color(0xFF5C5F66),
    press: Color(0x24FFFFFF), // rgba(255,255,255,.14)
    touch: Color(0x14FFFFFF), // rgba(255,255,255,.08)
    darkStatusText: false,
  );

  const DeviceFinish({
    required this.body,
    required this.bodyStops,
    required this.ring,
    required this.ringEdge,
    required this.groove,
    required this.centre,
    required this.glyph,
    required this.wordmark,
    required this.press,
    required this.touch,
    required this.darkStatusText,
  });

  /// Body gradient colours, top to bottom.
  final List<Color> body;

  /// Where each body colour sits.
  final List<double> bodyStops;

  /// Wheel ring gradient, top to bottom.
  final List<Color> ring;

  /// The ring's 1 px edge.
  final Color ringEdge;

  /// The groove round the centre button.
  final Color groove;

  /// Centre button gradient, top to bottom.
  final List<Color> centre;

  /// Glyphs and `MENU`.
  final Color glyph;

  /// The `neuPod` wordmark.
  final Color wordmark;

  /// Tint of a pressed button sector or the centre.
  final Color press;

  /// Tint of the wedge under a turning finger.
  final Color touch;

  /// The phone's status bar text is dark on this finish (fit A).
  final bool darkStatusText;

  /// The body as a gradient.
  LinearGradient get bodyGradient => LinearGradient(
    begin: Alignment.topCenter,
    end: Alignment.bottomCenter,
    colors: body,
    stops: bodyStops,
  );

  /// The finish for a brightness when the Body setting is Automatic: silver
  /// in Light, black in Dark (the mode, not the style, decides).
  static DeviceFinish forBrightness(Brightness b) =>
      b == Brightness.dark ? black : silver;
}

/// The surround round the body in fit B, in every style and mode.
const Color deviceSurround = Color(0xFF0B0C0E);

/// The screen frame (black glass), both finishes.
const Color deviceFrame = Color(0xFF050608);

/// The frame's 1 px inner edge.
const Color deviceFrameEdge = Color(0x14FFFFFF); // rgba(255,255,255,.08)
