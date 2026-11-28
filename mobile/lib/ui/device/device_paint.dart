// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

/// Everything the device's painters read: the one paint type for the silver
/// and black finishes and for Custom mode. A [DeviceFinish] and a custom look
/// each turn into one of these; the painters never see either.
///
/// Gradients run top to bottom. The opacities and the cap, badge and board
/// flags are for see-through parts (Custom mode, transparency); a solid paint
/// has opacity 1 and no cap, badge or board.
@immutable
class DevicePaint {
  const DevicePaint({
    required this.body,
    required this.bodyStops,
    required this.ring,
    required this.ringEdge,
    required this.groove,
    required this.centre,
    required this.centreEdge,
    required this.glyph,
    required this.wordmark,
    required this.press,
    required this.centrePress,
    required this.touch,
    required this.darkStatusText,
    this.frameShadow = const [
      BoxShadow(color: Color(0x59FFFFFF), offset: Offset(0, 1)),
    ],
    this.bodyOpacity = 1,
    this.ringOpacity = 1,
    this.centreOpacity = 1,
    this.hasCap = false,
    this.hasBadge = false,
    this.hasBoard = false,
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

  /// The centre button's edge.
  final Color centreEdge;

  /// Glyphs and `MENU` (the legend ink).
  final Color glyph;

  /// The `neuPod` wordmark.
  final Color wordmark;

  /// Tint of a pressed button sector.
  final Color press;

  /// Tint of the pressed centre button.
  final Color centrePress;

  /// Tint of the wedge under a turning finger.
  final Color touch;

  /// The phone's status bar text is dark on this paint.
  final bool darkStatusText;

  /// The shadow that draws the frame's outer 1 px edge.
  final List<BoxShadow> frameShadow;

  /// Opacity of the body, the ring and the centre (1 = solid).
  final double bodyOpacity;
  final double ringOpacity;
  final double centreOpacity;

  /// A solid cap under the status bar (see-through body).
  final bool hasCap;

  /// The wordmark sits on a badge (see-through body).
  final bool hasBadge;

  /// A board picture shows through.
  final bool hasBoard;

  /// The body as a gradient.
  LinearGradient get bodyGradient => LinearGradient(
    begin: Alignment.topCenter,
    end: Alignment.bottomCenter,
    colors: body,
    stops: bodyStops,
  );

  @override
  bool operator ==(Object other) =>
      other is DevicePaint &&
      listEquals(other.body, body) &&
      listEquals(other.bodyStops, bodyStops) &&
      listEquals(other.ring, ring) &&
      other.ringEdge == ringEdge &&
      other.groove == groove &&
      listEquals(other.centre, centre) &&
      other.centreEdge == centreEdge &&
      other.glyph == glyph &&
      other.wordmark == wordmark &&
      other.press == press &&
      other.centrePress == centrePress &&
      other.touch == touch &&
      other.darkStatusText == darkStatusText &&
      listEquals(other.frameShadow, frameShadow) &&
      other.bodyOpacity == bodyOpacity &&
      other.ringOpacity == ringOpacity &&
      other.centreOpacity == centreOpacity &&
      other.hasCap == hasCap &&
      other.hasBadge == hasBadge &&
      other.hasBoard == hasBoard;

  @override
  int get hashCode => Object.hash(
    Object.hashAll(body),
    Object.hashAll(bodyStops),
    Object.hashAll(ring),
    ringEdge,
    groove,
    Object.hashAll(centre),
    centreEdge,
    glyph,
    wordmark,
    press,
    centrePress,
    touch,
    darkStatusText,
    Object.hashAll(frameShadow),
    bodyOpacity,
    ringOpacity,
    centreOpacity,
    Object.hash(hasCap, hasBadge, hasBoard),
  );
}
