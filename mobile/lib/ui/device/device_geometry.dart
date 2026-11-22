// SPDX-License-Identifier: MPL-2.0

import 'dart:math' as math;
import 'dart:ui' show Offset, Rect;

/// How the device sits on the phone (design section 2; Open question 1).
enum DeviceFit {
  /// A: the body fills the phone, edge to edge (recommended, drawn).
  fillsPhone,

  /// B: the body at fixed proportions (1.675 W high) on a dark surround;
  /// falls back to A when the body is taller than the safe height.
  fixedProportions,
}

/// The device's layout on a phone, in points, from the ratios of the device
/// view design (docs/design/2026-11-21-app-device-view, sections 1 and 2).
/// Every value is a ratio of W, the phone's width; `render.py` in that folder
/// is the reference implementation and the tests compare against it.
class DeviceGeometry {
  DeviceGeometry._({
    required this.width,
    required this.height,
    required this.requestedFit,
    required this.fit,
    required this.screen,
    required this.frameWidth,
    required this.wheelCentre,
    required this.wheelDiameter,
    required this.body,
    required this.wordmarkCentreY,
    required this.wordmarkSize,
    required this.showWordmark,
    required this.canLayOut,
  });

  /// Screen width as a ratio of W.
  static const double screenRatio = 0.88;

  /// Frame (black glass) round the screen, as a ratio of W.
  static const double frameRatio = 0.012;

  /// Outer corner radius of the frame, as a ratio of W.
  static const double frameRadiusRatio = 0.02;

  /// Corner radius of the screen, as a ratio of W.
  static const double screenRadiusRatio = 0.008;

  /// Wheel diameter as a ratio of W.
  static const double wheelRatio = 0.64;

  /// The smallest the wheel may shrink to in fit A, as a ratio of W.
  static const double minWheelRatio = 0.56;

  /// Fit B: body height as a ratio of W.
  static const double bodyAspect = 1.675;

  /// Fit B: body corner radius as a ratio of W.
  static const double bodyRadiusRatio = 0.08;

  /// The screens are designed at this size and scaled uniformly.
  static const double designWidth = 600;

  /// The screens' design height.
  static const double designHeight = 450;

  /// The screen is never narrower than this.
  static const double minScreenWidth = 320;

  /// The side margin may shrink to this to keep the minimum screen.
  static const double minSideMargin = 12;

  /// The wordmark is omitted when its gap is under this.
  static const double minWordmarkGap = 44;

  /// The narrowest phone the design supports (320 + 2 x 12).
  static const double minPhoneWidth = minScreenWidth + 2 * minSideMargin;

  /// Lays the device out on a phone [width] x [height] points with the given
  /// safe-area insets.
  factory DeviceGeometry.compute({
    required double width,
    required double height,
    required double safeTop,
    required double safeBottom,
    DeviceFit fit = DeviceFit.fillsPhone,
  }) {
    final w = width;
    var sw = screenRatio * w;
    if (sw < minScreenWidth) {
      // Narrow phone: the screen stays 320 and the margin shrinks to 12.
      sw = math.min(minScreenWidth, w - 2 * minSideMargin);
    }
    // A zero or tiny first frame (Android may lay the window out at 0x0):
    // never a negative size.
    sw = math.max(0, sw);
    final sh = sw * designHeight / designWidth;
    final bz = frameRatio * w;
    var d = wheelRatio * w;

    var used = fit;
    final bodyHeight = bodyAspect * w;
    if (fit == DeviceFit.fixedProportions &&
        bodyHeight > height - safeTop - safeBottom) {
      used = DeviceFit.fillsPhone; // B does not fit: fall back to A
    }

    double scrTop;
    double floor;
    Rect body;
    if (used == DeviceFit.fillsPhone) {
      body = Rect.fromLTWH(0, 0, w, height);
      scrTop = safeTop + math.max(8, 0.03 * w) + bz;
      floor = height - math.max(safeBottom, 0.03 * w);
    } else {
      final bodyBottom = safeBottom > 0 ? height - safeBottom : height;
      body = Rect.fromLTRB(0, bodyBottom - bodyHeight, w, bodyBottom);
      scrTop = body.top + 0.075 * w + bz;
      floor = bodyBottom - 0.03 * w;
    }
    final scrBottom = scrTop + sh;
    final frameBottom = scrBottom + bz;
    final space = floor - frameBottom;
    if (used == DeviceFit.fillsPhone && space < d + 24) {
      d = math.max(minWheelRatio * w, space - 24);
    }
    d = math.max(0, d);
    final cy = frameBottom + space / 2;
    final screen = Rect.fromLTWH((w - sw) / 2, scrTop, sw, sh);
    // As render.py: the gap measured from the screen's bottom edge, the mark
    // centred between the frame and the wheel.
    final gap = cy - d / 2 - scrBottom;
    return DeviceGeometry._(
      width: w,
      height: height,
      requestedFit: fit,
      fit: used,
      screen: screen,
      frameWidth: bz,
      wheelCentre: Offset(w / 2, cy),
      wheelDiameter: d,
      body: body,
      wordmarkCentreY: frameBottom + (cy - d / 2 - frameBottom) / 2,
      wordmarkSize: 0.036 * w,
      showWordmark: gap > minWordmarkGap,
      canLayOut:
          w >= minPhoneWidth &&
          height > 0 &&
          (used == DeviceFit.fixedProportions || space >= minWheelRatio * w),
    );
  }

  /// The phone's width, W.
  final double width;

  /// The phone's height.
  final double height;

  /// The fit asked for.
  final DeviceFit requestedFit;

  /// The fit used (B falls back to A when it does not fit).
  final DeviceFit fit;

  /// The 4:3 screen.
  final Rect screen;

  /// The frame's width round the screen.
  final double frameWidth;

  /// The wheel's centre.
  final Offset wheelCentre;

  /// The wheel's diameter, D.
  final double wheelDiameter;

  /// The body: the whole phone in fit A, the fixed body in fit B.
  final Rect body;

  /// The wordmark's vertical centre.
  final double wordmarkCentreY;

  /// The wordmark's type size.
  final double wordmarkSize;

  /// The gap between the screen and the wheel is wide enough for the
  /// wordmark.
  final bool showWordmark;

  /// The phone is wide enough (at least [minPhoneWidth]) and tall enough
  /// (the wheel at its minimum fits below the screen) to draw the device.
  /// When false the view draws only the empty body.
  final bool canLayOut;

  /// The scale from the 600x450 design: s = screen width / 600.
  double get scale => screen.width / designWidth;

  /// The frame's outer rectangle.
  Rect get frame => screen.inflate(frameWidth);

  /// The frame's outer corner radius.
  double get frameRadius => frameRadiusRatio * width;

  /// The screen's corner radius.
  double get screenRadius => screenRadiusRatio * width;

  /// The wheel's radius, R.
  double get wheelRadius => wheelDiameter / 2;

  /// The wheel's drawn square.
  Rect get wheelRect =>
      Rect.fromCircle(center: wheelCentre, radius: wheelRadius);

  /// Fit B: the body's corner radius.
  double get bodyRadius => bodyRadiusRatio * width;

  /// The phone is narrower than the design supports.
  bool get tooNarrow => width < minPhoneWidth;
}
