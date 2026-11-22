// SPDX-License-Identifier: MPL-2.0

import 'dart:math' as math;
import 'dart:ui' show Offset, Rect;

/// Where and how large the device is drawn in a window (see
/// [DeviceGeometry.plan]).
class FitPlan {
  const FitPlan._({
    required this.empty,
    required this.scale,
    required this.virtualWidth,
    required this.virtualHeight,
    required this.offsetX,
    this.safeTop,
    this.safeBottom,
  });

  /// The window is degenerate: draw only the body.
  final bool empty;

  /// Uniform scale of the laid-out device; 1 for a phone.
  final double scale;

  /// The width the device is laid out at, before [scale].
  final double virtualWidth;

  /// The height the device is laid out at, before [scale].
  final double virtualHeight;

  /// Left edge of the drawn device in the window.
  final double offsetX;

  /// Insets in virtual points when scaled; null: use the window's.
  final double? safeTop;

  /// See [safeTop].
  final double? safeBottom;

  /// Drawn exactly as a phone: full window, no scaling.
  bool get isPlain => !empty && scale == 1 && offsetX == 0;
}

/// The device's layout on a phone, in points, from the ratios of the device
/// view design (docs/design/2026-11-21-app-device-view, sections 1 and 2).
/// Every value is a ratio of W, the phone's width; `render.py` in that folder
/// is the reference implementation and the tests compare against it.
class DeviceGeometry {
  DeviceGeometry._({
    required this.width,
    required this.height,
    required this.screen,
    required this.frameWidth,
    required this.wheelCentre,
    required this.wheelDiameter,
    required this.body,
    required this.wordmarkCentreY,
    required this.wordmarkSize,
    required this.showWordmark,
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

    final body = Rect.fromLTWH(0, 0, w, height);
    final scrTop = safeTop + math.max(8, 0.03 * w) + bz;
    final floor = height - math.max(safeBottom, 0.03 * w);
    final scrBottom = scrTop + sh;
    final frameBottom = scrBottom + bz;
    final space = floor - frameBottom;
    if (space < d + 24) {
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
      screen: screen,
      frameWidth: bz,
      wheelCentre: Offset(w / 2, cy),
      wheelDiameter: d,
      body: body,
      wordmarkCentreY: frameBottom + (cy - d / 2 - frameBottom) / 2,
      wordmarkSize: 0.036 * w,
      showWordmark: gap > minWordmarkGap,
    );
  }

  /// How the device sits in a window [width] x [height] (the body fills the phone). A window that is tall and wide enough gets
  /// the device at full width, scale 1. A short one is narrowed (laid out at
  /// the largest width whose height fits, not below [minPhoneWidth]); one
  /// narrower than that, or too short even then (split screen, landscape, a
  /// tablet), is scaled down uniformly to fit and centred: the device is laid
  /// out in a virtual window of [FitPlan.virtualWidth] x
  /// [FitPlan.virtualHeight] and drawn at [FitPlan.scale]. A window under
  /// 1 pt either way (or not finite) is [FitPlan.empty].
  static FitPlan plan({
    required double width,
    required double height,
    required double safeTop,
    required double safeBottom,
  }) {
    if (!width.isFinite || !height.isFinite || width < 1 || height < 1) {
      return const FitPlan._(
        empty: true,
        scale: 1,
        virtualWidth: 0,
        virtualHeight: 0,
        offsetX: 0,
      );
    }
    // The least height at which fit A shows the screen and a wheel of its
    // minimum size, for a phone of width v.
    double needed(double v, double top, double bottom) {
      final g = DeviceGeometry.compute(
        width: v,
        height: 100000,
        safeTop: top,
        safeBottom: bottom,
      );
      return g.screen.bottom +
          g.frameWidth +
          minWheelRatio * v +
          math.max(bottom, 0.03 * v);
    }

    if (width >= minPhoneWidth &&
        height >= needed(width, safeTop, safeBottom)) {
      // Full width when it fits; if the window is wide enough but short,
      // the largest width that does.
      return FitPlan._(
        empty: false,
        scale: 1,
        virtualWidth: width,
        virtualHeight: height,
        offsetX: 0,
      );
    }
    final hn = needed(minPhoneWidth, safeTop, safeBottom);
    var v = minPhoneWidth;
    var k = 1.0;
    if (width >= minPhoneWidth && height >= hn) {
      // Shrink the phone's width (insets as they are) until the height fits.
      var lo = minPhoneWidth;
      var hi = width;
      for (var i = 0; i < 40; i++) {
        final mid = (lo + hi) / 2;
        if (height >= needed(mid, safeTop, safeBottom)) {
          lo = mid;
        } else {
          hi = mid;
        }
      }
      v = lo;
    } else {
      // Below the narrowest supported phone: lay out at 344 pt and scale.
      k = math.min(1.0, math.min(width / minPhoneWidth, height / hn));
    }
    return FitPlan._(
      empty: false,
      scale: k,
      virtualWidth: v,
      virtualHeight: height / k,
      offsetX: (width - v * k) / 2,
      safeTop: safeTop / k,
      safeBottom: safeBottom / k,
    );
  }

  /// The phone's width, W.
  final double width;

  /// The phone's height.
  final double height;

  /// The 4:3 screen.
  final Rect screen;

  /// The frame's width round the screen.
  final double frameWidth;

  /// The wheel's centre.
  final Offset wheelCentre;

  /// The wheel's diameter, D.
  final double wheelDiameter;

  /// The body: the whole phone (fit A, the owner's choice).
  final Rect body;

  /// The wordmark's vertical centre.
  final double wordmarkCentreY;

  /// The wordmark's type size.
  final double wordmarkSize;

  /// The gap between the screen and the wheel is wide enough for the
  /// wordmark.
  final bool showWordmark;

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

  /// The phone is narrower than the design supports.
  bool get tooNarrow => width < minPhoneWidth;
}
