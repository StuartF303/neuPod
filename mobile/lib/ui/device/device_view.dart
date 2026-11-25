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

/// The design gate's switches are in the build unless it is made with
/// `--dart-define=NEUPOD_GATE=false` (store builds).
const bool gateSwitchesEnabled = bool.fromEnvironment(
  'NEUPOD_GATE',
  defaultValue: true,
);

class _DeviceViewState extends ConsumerState<DeviceView> {
  bool _gateOpen = false;

  @override
  void initState() {
    super.initState();
    // A system UI change is a side effect: made once now, then on each
    // change of the option, never from build.
    _applyStatusBar(ref.read(deviceViewOptionsProvider).showStatusBar);
    ref.listenManual(
      deviceViewOptionsProvider.select((o) => o.showStatusBar),
      (_, show) => _applyStatusBar(show),
    );
  }

  @override
  void dispose() {
    _applyStatusBar(true); // leave the system UI as the app found it
    super.dispose();
  }

  static void _applyStatusBar(bool show) {
    // Edge to edge: the body is drawn under the status bar and the gesture
    // bar. Hidden: only the status bar goes; the notch space stays reserved
    // (on a phone without a notch the device moves up by the bar's height).
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

    final media = MediaQuery.of(context);
    final size = media.size;
    // viewPadding, not padding: the device never moves (Search is typed with
    // the wheel, owner decision 12, so no phone keyboard comes up).
    final pad = media.viewPadding;
    final plan = DeviceGeometry.plan(
      width: size.width,
      height: size.height,
      safeTop: pad.top,
      safeBottom: pad.bottom,
    );
    final g = DeviceGeometry.compute(
      width: plan.empty ? 1 : plan.virtualWidth,
      height: plan.empty ? 1 : plan.virtualHeight,
      safeTop: plan.safeTop ?? pad.top,
      safeBottom: plan.safeBottom ?? pad.bottom,
    );
    final darkText = finish.darkStatusText;
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

    final deviceStack = Stack(
      children: [
        // The body, which also takes the long press for the gate switches.
        Positioned.fill(
          // The gate's long press is for sighted testers only: hidden from
          // the screen reader, which would otherwise offer an unlabelled
          // long-press action on the body.
          child: ExcludeSemantics(
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onLongPress: gateSwitchesEnabled
                  ? () => setState(() => _gateOpen = true)
                  : null,
              child: DecoratedBox(
                decoration: BoxDecoration(gradient: finish.bodyGradient),
              ),
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
            child: DeviceScreenHost(scale: g.scale, child: widget.screen),
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
                    // Always 0.036 W: the wordmark is part of the body, and
                    // the 44 pt gap rule assumes its size.
                    textScaler: TextScaler.noScaling,
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

    final Widget device;
    if (plan.empty) {
      // A window under 1 pt (Android's first frame can be 0x0): only the
      // empty, correctly coloured body, no screen host or wheel.
      device = DecoratedBox(
        decoration: BoxDecoration(gradient: finish.bodyGradient),
      );
    } else {
      // The same tree for a phone and for a short or narrow window, so a
      // plan change (rotation, split screen, a resize near the threshold)
      // keeps the screen stack's state instead of remounting it. A phone is
      // scale 1 at offset 0; a window too short or narrow for the device at
      // full width gets the whole device scaled down uniformly and centred,
      // the body finish filling the rest.
      device = Stack(
        children: [
          Positioned.fill(
            child: DecoratedBox(
              decoration: BoxDecoration(gradient: finish.bodyGradient),
            ),
          ),
          Positioned(
            left: plan.offsetX,
            top: 0,
            width: plan.virtualWidth,
            height: plan.virtualHeight,
            child: Transform.scale(
              scale: plan.scale,
              alignment: Alignment.topLeft,
              child: deviceStack,
            ),
          ),
        ],
      );
    }

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
        // While the gate panel is open its own controls take the keys.
        child: WheelKeyboard(enabled: !_gateOpen, child: device),
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
