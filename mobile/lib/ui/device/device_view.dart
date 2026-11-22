// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/device/click_wheel.dart';
import 'package:neupod/ui/device/device_finish.dart';
import 'package:neupod/ui/device/device_geometry.dart';
import 'package:neupod/ui/device/device_options.dart';
import 'package:neupod/ui/device/gate_panel.dart';
import 'package:neupod/ui/device/screen_host.dart';
import 'package:neupod/ui/device/wheel_bus.dart';

/// The app is the device (owner decision 9): the phone shows the front of a
/// neuPod, drawn fresh, with the 4:3 screen hosting [screen] above the click
/// wheel. Layout from the device view design
/// (docs/design/2026-11-21-app-device-view): geometry from the phone's
/// width, fit A or B, the silver or black body, the phone's status bar.
///
/// The system back gesture is Menu (section 5): Android's back, and on iOS a
/// swipe from the left edge (a switch, Open question 9). At Home Android's
/// back leaves the app; on iOS the swipe does nothing.
///
/// A long press on the body (outside the screen and the wheel) opens the
/// design gate's switches ([GatePanel]): fit, body finish, direct touch,
/// status bar, wordmark, progress-bar seek, haptics.
class DeviceView extends ConsumerStatefulWidget {
  /// The device showing [screen], which is laid out at 600x450.
  const DeviceView({required this.screen, this.brightness, super.key});

  /// The screen inside the device.
  final Widget screen;

  /// The mode, for the Automatic body; defaults to the platform's.
  final Brightness? brightness;

  @override
  ConsumerState<DeviceView> createState() => _DeviceViewState();
}

class _DeviceViewState extends ConsumerState<DeviceView> {
  bool _gateOpen = false;
  bool? _statusBarShown;

  void _applyStatusBar(bool show) {
    if (_statusBarShown == show) return;
    _statusBarShown = show;
    // Edge to edge: the body is drawn under the status bar and the gesture
    // bar. Hidden: only the status bar goes; the notch space stays reserved.
    SystemChrome.setEnabledSystemUIMode(
      show ? SystemUiMode.edgeToEdge : SystemUiMode.manual,
      overlays: show ? null : const [SystemUiOverlay.bottom],
    );
  }

  @override
  Widget build(BuildContext context) {
    final options = ref.watch(deviceViewOptionsProvider);
    final bus = ref.watch(wheelBusProvider);
    final brightness =
        widget.brightness ?? MediaQuery.platformBrightnessOf(context);
    final finish = options.finishFor(brightness);
    _applyStatusBar(options.showStatusBar);

    final media = MediaQuery.of(context);
    final size = media.size;
    // viewPadding, not padding: the device never moves (Search is typed with
    // the wheel, owner decision 12, so no phone keyboard comes up).
    final pad = media.viewPadding;
    final g = DeviceGeometry.compute(
      width: size.width,
      height: size.height,
      safeTop: pad.top,
      safeBottom: pad.bottom,
      fit: options.fit,
    );
    final fitA = g.fit == DeviceFit.fillsPhone;
    final darkText = fitA && finish.darkStatusText;
    final overlay = SystemUiOverlayStyle(
      statusBarColor: const Color(0x00000000),
      systemNavigationBarColor: const Color(0x00000000),
      systemNavigationBarContrastEnforced: false,
      statusBarIconBrightness: darkText ? Brightness.dark : Brightness.light,
      // iOS: the brightness of what is behind the bar.
      statusBarBrightness: darkText ? Brightness.light : Brightness.dark,
      systemNavigationBarIconBrightness: darkText
          ? Brightness.dark
          : Brightness.light,
    );

    final extent = ClickWheel.extentFor(g.wheelDiameter);
    final iosSwipe =
        options.leftEdgeSwipeIsMenu &&
        defaultTargetPlatform == TargetPlatform.iOS;

    final device = Stack(
      children: [
        // The body, which also takes the long press for the gate switches.
        Positioned.fill(
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onLongPress: () => setState(() => _gateOpen = true),
            child: fitA
                ? DecoratedBox(
                    decoration: BoxDecoration(gradient: finish.bodyGradient),
                  )
                : ColoredBox(
                    color: deviceSurround,
                    child: Stack(children: [_fixedBody(g, finish)]),
                  ),
          ),
        ),
        Positioned.fromRect(
          rect: g.frame,
          child: IgnorePointer(
            child: DecoratedBox(
              decoration: BoxDecoration(
                color: deviceFrame,
                borderRadius: BorderRadius.circular(g.frameRadius),
                border: Border.all(color: deviceFrameEdge, width: 1),
                boxShadow: const [
                  BoxShadow(color: Color(0x59FFFFFF), offset: Offset(0, 1)),
                ],
              ),
            ),
          ),
        ),
        Positioned.fromRect(
          rect: g.screen,
          child: ClipRRect(
            borderRadius: BorderRadius.circular(g.screenRadius),
            child: DeviceScreenHost(
              scale: g.scale,
              directTouch: options.directTouch,
              dragProgressToSeek: options.dragProgressToSeek,
              child: widget.screen,
            ),
          ),
        ),
        if (options.showWordmark && g.showWordmark)
          Positioned(
            left: 0,
            right: 0,
            top: g.wordmarkCentreY - g.wordmarkSize,
            height: g.wordmarkSize * 2,
            child: IgnorePointer(
              child: ExcludeSemantics(
                child: Center(
                  child: Text(
                    'neuPod',
                    textDirection: TextDirection.ltr,
                    style: TextStyle(
                      fontFamily: 'Montserrat',
                      fontWeight: FontWeight.w500,
                      fontSize: g.wordmarkSize,
                      letterSpacing: 0.04 * g.wordmarkSize,
                      color: finish.wordmark,
                      decoration: TextDecoration.none,
                      height: 1,
                    ),
                  ),
                ),
              ),
            ),
          ),
        Positioned(
          left: g.wheelCentre.dx - extent / 2,
          top: g.wheelCentre.dy - extent / 2,
          width: extent,
          height: extent,
          child: ClickWheel(diameter: g.wheelDiameter, finish: finish),
        ),
        if (iosSwipe)
          Positioned(
            left: 0,
            top: 0,
            bottom: 0,
            width: 20,
            child: _EdgeSwipe(
              onSwipe: () {
                if (!bus.atRoot.value) bus.press(WheelButton.menu);
              },
            ),
          ),
        if (_gateOpen)
          Positioned.fill(
            child: GatePanel(onClose: () => setState(() => _gateOpen = false)),
          ),
      ],
    );

    return AnnotatedRegion<SystemUiOverlayStyle>(
      value: overlay,
      child: ValueListenableBuilder<bool>(
        valueListenable: bus.atRoot,
        builder: (context, atRoot, child) => PopScope(
          canPop: atRoot,
          onPopInvokedWithResult: (didPop, _) {
            if (!didPop) bus.press(WheelButton.menu);
          },
          child: child!,
        ),
        child: WheelKeyboard(child: device),
      ),
    );
  }

  Widget _fixedBody(DeviceGeometry g, DeviceFinish finish) {
    return Positioned.fromRect(
      rect: g.body,
      child: DecoratedBox(
        decoration: BoxDecoration(
          gradient: finish.bodyGradient,
          borderRadius: BorderRadius.circular(g.bodyRadius),
          boxShadow: [
            if (finish == DeviceFinish.black)
              const BoxShadow(color: Color(0x24FFFFFF), spreadRadius: 1),
            const BoxShadow(
              color: Color(0x99000000),
              offset: Offset(0, 18),
              blurRadius: 40,
            ),
          ],
        ),
      ),
    );
  }
}

/// A rightward swipe from the left edge (iOS): Menu.
class _EdgeSwipe extends StatefulWidget {
  const _EdgeSwipe({required this.onSwipe});

  final VoidCallback onSwipe;

  @override
  State<_EdgeSwipe> createState() => _EdgeSwipeState();
}

class _EdgeSwipeState extends State<_EdgeSwipe> {
  double _dx = 0;
  bool _fired = false;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.translucent,
      onHorizontalDragStart: (_) {
        _dx = 0;
        _fired = false;
      },
      onHorizontalDragUpdate: (d) {
        _dx += d.delta.dx;
        if (!_fired && _dx > 40) {
          _fired = true;
          widget.onSwipe();
        }
      },
    );
  }
}
