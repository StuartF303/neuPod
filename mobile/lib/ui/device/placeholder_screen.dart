// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/device_options.dart';
import 'package:neupod/ui/device/screen_host.dart';
import 'package:neupod/ui/device/wheel_bus.dart';

/// A stand-in for the device's screens until the real ones land (UI
/// engineer 1 builds them at 600x450 from the UI styles design). It shows
/// the specification's Home items, moves a selection bar with the wheel,
/// gives the detent and end-of-list haptics, takes row taps in direct-touch
/// candidate 2, and keeps the wheel's screen-reader value current, so the
/// whole wheel path can be tried on a phone. Its look is deliberately plain.
class PlaceholderScreen extends ConsumerStatefulWidget {
  /// The placeholder.
  const PlaceholderScreen({super.key});

  /// The Home items of the specification.
  static const items = [
    'Music',
    'Playlists',
    'Settings',
    'Shuffle Songs',
    'Now Playing',
  ];

  @override
  ConsumerState<PlaceholderScreen> createState() => _PlaceholderScreenState();
}

class _PlaceholderScreenState extends ConsumerState<PlaceholderScreen> {
  late final WheelBus _bus = ref.read(wheelBusProvider);
  StreamSubscription<WheelEvent>? _sub;
  int _sel = 0;
  bool _atEnd = false;
  String _last = '';

  static const _rowH = 56.0;
  static const _barH = 45.0;

  @override
  void initState() {
    super.initState();
    _sub = _bus.events.listen(_onEvent);
    WidgetsBinding.instance.addPostFrameCallback((_) => _describe());
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  void _describe() {
    if (!mounted) return;
    final n = PlaceholderScreen.items.length;
    String row(int i) => '${PlaceholderScreen.items[i]}, ${i + 1} of $n';
    _bus.atRoot.value = true;
    _bus.semantics.value = WheelSemantics(
      value: row(_sel),
      increasedValue: _sel < n - 1 ? row(_sel + 1) : '',
      decreasedValue: _sel > 0 ? row(_sel - 1) : '',
    );
  }

  void _onEvent(WheelEvent e) {
    switch (e) {
      case WheelStep(:final steps):
        _move(steps);
      case WheelPress(:final button):
        setState(() => _last = button.name);
      case WheelHoldStart(:final button):
        setState(() => _last = 'hold ${button.name}');
      case _:
        break;
    }
  }

  void _move(int steps) {
    final n = PlaceholderScreen.items.length;
    final to = (_sel + steps).clamp(0, n - 1);
    final feedback = ref.read(deviceFeedbackProvider);
    if (to == _sel) {
      if (!_atEnd) feedback.endReached(); // once per push past the end
      _atEnd = true;
      return;
    }
    _atEnd = false;
    feedback.detent();
    setState(() => _sel = to);
    _describe();
  }

  @override
  Widget build(BuildContext context) {
    final summary = ref.watch(librarySummaryProvider);
    final touch =
        DeviceScreenScope.of(context).directTouch == DirectTouch.wheelAndScreen;
    const ink = Color(0xFF181818);
    const style = TextStyle(fontSize: 26, color: ink, height: 1.1);
    // The real screens apply the design's text tiers (section 8); this
    // stand-in does not scale its text at all.
    return MediaQuery.withNoTextScaling(
      child: ColoredBox(
        color: const Color(0xFFFFFFFF),
        child: DefaultTextStyle(
          style: style,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Semantics(
                header: true,
                child: Container(
                  height: _barH,
                  alignment: Alignment.center,
                  decoration: const BoxDecoration(
                    gradient: LinearGradient(
                      begin: Alignment.topCenter,
                      end: Alignment.bottomCenter,
                      colors: [Color(0xFFFDFDFD), Color(0xFFC6CAD0)],
                    ),
                    border: Border(
                      bottom: BorderSide(color: Color(0xFF8C9199)),
                    ),
                  ),
                  child: const Text('neuPod'),
                ),
              ),
              Expanded(
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    SizedBox(
                      width: 336,
                      child: Column(
                        children: [
                          for (
                            var i = 0;
                            i < PlaceholderScreen.items.length;
                            i++
                          )
                            _row(i, touch),
                        ],
                      ),
                    ),
                    Expanded(
                      child: ExcludeSemantics(
                        child: Container(
                          color: const Color(0xFFE9EBEE),
                          padding: const EdgeInsets.all(16),
                          child: Column(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              Text(summary.songCountLabel),
                              const SizedBox(height: 12),
                              Text(
                                _last.isEmpty ? 'Placeholder screen' : _last,
                                style: const TextStyle(
                                  fontSize: 20,
                                  color: Color(0xFF606060),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _row(int i, bool touch) {
    final n = PlaceholderScreen.items.length;
    final selected = i == _sel;
    return Semantics(
      label: '${PlaceholderScreen.items[i]}, ${i + 1} of $n',
      selected: selected,
      button: true,
      onTap: () => _activate(i),
      excludeSemantics: true,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: touch ? () => _activate(i) : null,
        child: Container(
          height: _rowH,
          padding: const EdgeInsets.symmetric(horizontal: 19),
          alignment: Alignment.centerLeft,
          decoration: selected
              ? const BoxDecoration(
                  gradient: LinearGradient(
                    begin: Alignment.topCenter,
                    end: Alignment.bottomCenter,
                    colors: [Color(0xFF6DB3F5), Color(0xFF1858C8)],
                  ),
                )
              : null,
          child: Text(
            PlaceholderScreen.items[i],
            style: TextStyle(
              color: selected
                  ? const Color(0xFFFFFFFF)
                  : const Color(0xFF181818),
            ),
          ),
        ),
      ),
    );
  }

  void _activate(int i) {
    setState(() {
      _sel = i;
      _last = 'open ${PlaceholderScreen.items[i]}';
    });
    _describe();
  }
}
