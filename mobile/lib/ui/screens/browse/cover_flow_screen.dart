// SPDX-License-Identifier: MPL-2.0

/// Cover Flow: albums by artist then year. The wheel moves the covers one
/// album a step; Select, or a tap on the centred cover, opens the album; a
/// horizontal drag moves the covers (direct-touch candidate 2).
///
/// - **Classic**: the angled version on black with reflections: the centre
///   cover 200 px, the sides 156 px turned 58 degrees (UI styles design; its
///   Open question 8 asks for a performance test first).
/// - **Modern**: five flat rounded covers (220 / 150 / 100 px, opacity
///   1 / .85 / .6) over a backdrop washed in the centred album's colours.
///
/// The title block names the centred album: title, artist and, in Modern,
/// `1993 · 10 songs`. (UI styles Open question 11: the rule for long names is
/// not drawn; they end in an ellipsis here.)
library;

import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/art_frame.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/album_screen.dart';
import 'package:neupod/ui/screens/shell/cover_art.dart';
import 'package:neupod/ui/screens/shell/screen_scope.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/style/backdrop.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

/// Cover Flow.
class CoverFlowScreen extends ConsumerStatefulWidget {
  /// Cover Flow over every album.
  const CoverFlowScreen({super.key});

  @override
  ConsumerState<CoverFlowScreen> createState() => _CoverFlowScreenState();
}

class _CoverFlowScreenState extends ConsumerState<CoverFlowScreen> {
  static const _slide = Duration(milliseconds: 80);

  late final WheelBus _bus = ref.read(wheelBusProvider);
  ScreenHandle? _handle;
  List<Album>? _albums;

  /// The centre, in albums (fractional while dragging).
  double _pos = 0;
  bool _dragging = false;
  bool _atEnd = false;

  @override
  void initState() {
    super.initState();
    unawaited(_load());
  }

  Future<void> _load() async {
    final library = await ref.read(libraryViewProvider.future);
    final albums = await library.albumsForCoverFlow();
    if (mounted) {
      setState(() => _albums = albums);
      _describe();
    }
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final handle = ScreenScope.maybeOf(context);
    if (handle != _handle) {
      _handle?.isTop.removeListener(_describe);
      if (_handle?.wheel == _onWheel) _handle?.wheel = null;
      _handle = handle;
      handle?.wheel = _onWheel;
      handle?.isTop.addListener(_describe);
    }
  }

  @override
  void dispose() {
    _handle?.isTop.removeListener(_describe);
    if (_handle?.wheel == _onWheel) _handle?.wheel = null;
    super.dispose();
  }

  int get _index => _pos.round();

  String _say(int i) {
    final a = _albums![i];
    return '${a.title}, ${a.artist}, ${i + 1} of ${_albums!.length}';
  }

  void _describe() {
    final albums = _albums;
    if (!mounted || albums == null || albums.isEmpty) return;
    if (_handle != null && !_handle!.isTop.value) return;
    final i = _index;
    // After the frame: the shell's route change may be mid-build.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) return;
      _bus.semantics.value = WheelSemantics(
        value: _say(i),
        increasedValue: i < albums.length - 1 ? _say(i + 1) : '',
        decreasedValue: i > 0 ? _say(i - 1) : '',
        playing: ref.read(playerStateProvider).value?.playWhenReady ?? false,
      );
    });
  }

  bool _onWheel(WheelEvent e) {
    switch (e) {
      case WheelStep(:final steps):
        _step(steps);
        return true;
      case WheelPress(button: WheelButton.centre):
        _open();
        return true;
      case _:
        return false;
    }
  }

  void _step(int steps) {
    final albums = _albums;
    if (albums == null || albums.isEmpty) return;
    final to = (_index + steps).clamp(0, albums.length - 1);
    final feedback = ref.read(deviceFeedbackProvider);
    if (to == _index) {
      if (!_atEnd) feedback.endReached();
      _atEnd = true;
      return;
    }
    _atEnd = false;
    feedback.detent();
    setState(() => _pos = to.toDouble());
    _describe();
  }

  /// A tap at [x] (design px) on a side cover centres that cover, however far
  /// from the middle it sits (owner decision 13): the nearest drawn side
  /// cover to [x] on that side.
  void _centre(double x, bool modern, int count) {
    final sign = x < 300 ? -1 : 1;
    final offsets = modern ? _modernOffsets : _classicOffsets;
    var steps = 1;
    var best = double.infinity;
    for (var k = 1; k <= 3; k++) {
      final i = _index + sign * k;
      if (i < 0 || i >= count) break;
      final dx = (x - (300 + sign * offsets[k])).abs();
      if (dx < best) {
        best = dx;
        steps = k;
      }
    }
    _step(sign * steps);
  }

  static const _classicOffsets = [0.0, 153.0, 236.0, 319.0];
  static const _modernOffsets = [0.0, 180.0, 284.0, 350.0];

  void _open() {
    final albums = _albums;
    if (albums == null || albums.isEmpty) return;
    final a = albums[_index];
    pushScreen(ref, a.title, (_) => AlbumScreen(albumId: a.id));
  }

  @override
  Widget build(BuildContext context) {
    ref.listen(playerStateProvider, (_, _) => _describe());
    final albums = _albums;
    if (albums == null || albums.isEmpty) {
      return const ShellFrame(title: 'Cover Flow', body: SizedBox.shrink());
    }
    final current = albums[_index];
    final palette = ref.watch(artPaletteProvider(current.art)).value;
    return ShellFrame(
      title: 'Cover Flow',
      palette: palette,
      backdrop: BackdropLayout.album,
      body: Builder(builder: (context) => _flow(context, albums, current)),
    );
  }

  Widget _flow(BuildContext context, List<Album> albums, Album current) {
    final theme = NeupodTheme.of(context);
    final modern = theme.style.family == StyleFamily.modern;
    final reduce = MediaQuery.disableAnimationsOf(context);
    final dur = _dragging || reduce ? Duration.zero : _slide;
    final type = NeupodType(theme.tier);

    final Color titleInk = modern ? theme.style.tokens.ink : _classicInk;
    final Color subInk = modern ? theme.style.tokens.ink2 : _classicInk2;
    final titleStyle = modern ? type.large(titleInk) : type.body(titleInk);
    final subStyle = modern ? type.body(subInk) : type.small(subInk);

    final label = _say(_index);
    final stage = Semantics(
      label: label,
      button: true,
      onTap: _open,
      excludeSemantics: true,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onHorizontalDragStart: (_) => setState(() => _dragging = true),
        onHorizontalDragUpdate: (d) {
          setState(() {
            _pos = (_pos - d.delta.dx / 70).clamp(
              0.0,
              (albums.length - 1).toDouble(),
            );
          });
        },
        onHorizontalDragEnd: (_) {
          setState(() {
            _dragging = false;
            _pos = _pos.roundToDouble();
          });
          _describe();
        },
        onTapUp: (d) {
          final x = d.localPosition.dx;
          final half = modern ? 110.0 : 100.0;
          if ((x - 300).abs() <= half) {
            _open();
          } else {
            _centre(x, modern, albums.length);
          }
        },
        child: Stack(
          fit: StackFit.expand,
          children: [
            if (!modern)
              const DecoratedBox(
                decoration: BoxDecoration(
                  gradient: LinearGradient(
                    begin: Alignment.topCenter,
                    end: Alignment.bottomCenter,
                    colors: [Color(0xff000000), Color(0xff1a1c20)],
                  ),
                ),
              ),
            TweenAnimationBuilder<double>(
              tween: Tween(end: _pos),
              duration: dur,
              curve: Curves.easeOut,
              builder: (context, pos, _) => Stack(
                clipBehavior: Clip.hardEdge,
                children: _covers(context, albums, pos, modern),
              ),
            ),
            Positioned(
              left: 20,
              right: 20,
              bottom: modern ? 22 : 18,
              child: ExcludeSemantics(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    NeupodText(
                      current.title,
                      titleStyle,
                      align: TextAlign.center,
                    ),
                    NeupodText(
                      current.artist,
                      subStyle,
                      align: TextAlign.center,
                    ),
                    if (modern)
                      NeupodText(
                        albumDetails(current, length: false).join(' · '),
                        type.small(theme.style.tokens.ink3),
                        align: TextAlign.center,
                      ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
    return stage;
  }

  static const _classicInk = Color(0xffffffff);
  static const _classicInk2 = Color(0xffa8a8a8);

  List<Widget> _covers(
    BuildContext context,
    List<Album> albums,
    double pos,
    bool modern,
  ) {
    final theme = NeupodTheme.of(context);
    final centre = pos.round();
    final out = <(double, Widget)>[];
    for (var i = centre - 3; i <= centre + 3; i++) {
      if (i < 0 || i >= albums.length) continue;
      final d = i - pos;
      final ad = d.abs();
      if (ad > 3.2) continue;
      out.add((
        ad,
        modern
            ? _modernCover(albums[i], d, theme)
            : _classicCover(albums[i], d, theme),
      ));
    }
    // Far to near, so the centre is on top.
    out.sort((a, b) => b.$1.compareTo(a.$1));
    return [for (final (_, w) in out) w];
  }

  /// A piecewise-linear value of |d| through [at] (index 0 is d = 0).
  static double _lerp(double ad, List<double> at) {
    final i = ad.floor().clamp(0, at.length - 2);
    final t = (ad - i).clamp(0.0, 1.0);
    return at[i] + (at[i + 1] - at[i]) * t;
  }

  // Classic: centre 200 px, sides 156 turned 58 degrees, as drawn.
  Widget _classicCover(Album a, double d, NeupodTheme theme) {
    final ad = d.abs();
    final sign = d < 0 ? -1.0 : 1.0;
    final size = _lerp(ad, const [200, 156, 156, 156]);
    final angle = _lerp(ad, const [0, 58, 58, 58]) * math.pi / 180;
    final cx = 300 + sign * _lerp(ad, _classicOffsets);
    const centreY = 134.0;
    final card = ArtFrame(
      size: size,
      use: ArtUse.nowPlaying,
      reflection: true,
      art: CoverArt(a.art, decodeWidth: size),
    );
    return Positioned(
      left: cx - size / 2,
      top: centreY - size / 2,
      child: Transform(
        alignment: Alignment.center,
        transform: Matrix4.identity()
          ..setEntry(3, 2, 0.0012)
          // The inner edge recedes: left covers turn one way, right the other.
          ..rotateY(-sign * angle),
        child: card,
      ),
    );
  }

  // Modern: flat covers 220 / 150 / 100 with radii 20 / 16 / 12.
  Widget _modernCover(Album a, double d, NeupodTheme theme) {
    final ad = d.abs();
    final sign = d < 0 ? -1.0 : 1.0;
    final size = _lerp(ad, const [220, 150, 100, 70]);
    final radius = _lerp(ad, const [20, 16, 12, 10]);
    final opacity = _lerp(ad, const [1, 0.85, 0.6, 0.3]);
    final cx = 300 + sign * _lerp(ad, _modernOffsets);
    const centreY = 140.0;
    final shadows = theme.style.art.shadows(ArtUse.nowPlaying);
    return Positioned(
      left: cx - size / 2,
      top: centreY - size / 2,
      child: Opacity(
        opacity: opacity,
        child: DecoratedBox(
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(radius),
            boxShadow: shadows,
          ),
          child: ClipRRect(
            borderRadius: BorderRadius.circular(radius),
            child: SizedBox.square(
              dimension: size,
              child: CoverArt(a.art, decodeWidth: size),
            ),
          ),
        ),
      ),
    );
  }
}
