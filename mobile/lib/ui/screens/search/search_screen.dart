// SPDX-License-Identifier: MPL-2.0

/// Search, typed with the wheel (owner decision 12): the wheel events the
/// shell hands to the top screen drive a [SearchSession]; the phone's keyboard
/// never comes up.
library;

import 'package:flutter/foundation.dart' show mapEquals;
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/text_entry/wheel_text_entry.dart' as entry;
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/browse/album_screen.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/collection_screen.dart';
import 'package:neupod/ui/screens/search/search_session.dart';
import 'package:neupod/ui/screens/search/search_view.dart';
import 'package:neupod/ui/screens/shell/screen_scope.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';

/// The library search Search runs after every edit: the library's artists,
/// albums and songs (read once when Search opens, in the library's collation
/// order), matched by a case-insensitive substring of the name, names that
/// start with the text first. Tests override it with their own.
final searchFunctionProvider = FutureProvider<SearchFunction>((ref) async {
  final library = await ref.watch(libraryViewProvider.future);
  final artists = [
    for (final a in await library.artists())
      SearchHit(kind: SearchKind.artist, id: a.id, title: a.name),
  ];
  final albums = [
    for (final a in await library.albums())
      SearchHit(
        kind: SearchKind.album,
        id: a.id,
        title: a.title,
        detail: a.artist,
        year: a.year,
      ),
  ];
  final songs = [
    for (final s in await library.songs())
      SearchHit(
        kind: SearchKind.song,
        id: s.id,
        title: s.title,
        detail: '${s.artist} · ${s.album}',
      ),
  ];
  // Lower-cased once, here, not on every key.
  List<(String, SearchHit)> fold(List<SearchHit> all) => [
    for (final h in all) (h.title.toLowerCase(), h),
  ];
  final foldedArtists = fold(artists);
  final foldedAlbums = fold(albums);
  final foldedSongs = fold(songs);

  List<SearchHit> match(List<(String, SearchHit)> all, String q) {
    final needle = q.toLowerCase();
    final starts = <SearchHit>[];
    final within = <SearchHit>[];
    for (final (name, hit) in all) {
      if (name.startsWith(needle)) {
        starts.add(hit);
      } else if (name.contains(needle)) {
        within.add(hit);
      }
    }
    return [...starts, ...within];
  }

  return (String query) => query.isEmpty
      ? SearchResults.none
      : SearchResults(
          artists: match(foldedArtists, query),
          albums: match(foldedAlbums, query),
          songs: match(foldedSongs, query),
        );
});

/// The Search screen.
class SearchScreen extends ConsumerStatefulWidget {
  /// Search; a row opens through [onOpen], by default the artist, the album,
  /// or playing the song.
  const SearchScreen({this.onOpen, this.initialText = '', super.key});

  /// The centre on a result, or a tap on one.
  final void Function(SearchHit hit)? onOpen;

  /// Text to start with.
  final String initialText;

  @override
  ConsumerState<SearchScreen> createState() => _SearchScreenState();
}

class _SearchScreenState extends ConsumerState<SearchScreen> {
  late final WheelBus _bus = ref.read(wheelBusProvider);
  late final SearchSession _session;
  SearchFunction _find = (query) => SearchResults.none;
  ScreenHandle? _screen;
  bool _claimed = false;
  bool _atEnd = false;
  bool _loading = true;
  Object? _loadError;
  Map<WheelButton, int> _savedHolds = const {};
  Map<WheelButton, int> _ownHolds = const {};

  @override
  void initState() {
    super.initState();
    _session = SearchSession(
      search: (q) => _find(q),
      initial: widget.initialText,
      onChange: _onChange,
    );
    _session.onOpen = _open;
    ref.listenManual(searchFunctionProvider, (_, next) {
      final f = next.value;
      if (f != null) {
        _find = f;
        _loading = false;
        _loadError = null;
        _session.refresh();
      } else if (next.hasError) {
        _loading = false;
        _loadError = next.error;
        if (mounted) setState(() {});
      } else {
        _loading = true;
        _loadError = null;
      }
    }, fireImmediately: true);
    // After a panel goes, say what this screen's wheel is again (the panel's
    // saved text is only a fallback).
    ref.listenManual(shellProvider.select((s) => s.panel != null), (was, isUp) {
      if (!isUp) _describe();
    });
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final handle = ScreenScope.maybeOf(context);
    if (handle != _screen) {
      _screen?.isTop.removeListener(_onTop);
      if (_screen?.wheel == _onWheel) _screen?.wheel = null;
      _screen = handle;
      handle?.wheel = _onWheel;
      handle?.isTop.addListener(_onTop);
      WidgetsBinding.instance.addPostFrameCallback((_) => _onTop());
    }
  }

  @override
  void dispose() {
    _screen?.isTop.removeListener(_onTop);
    if (_screen?.wheel == _onWheel) _screen?.wheel = null;
    _release();
    super.dispose();
  }

  bool get _isTop => _screen?.isTop.value ?? true;

  void _onTop() {
    if (!mounted) return;
    if (_isTop) {
      _claim();
    } else {
      _release();
    }
  }

  void _claim() {
    if (_claimed) return;
    _claimed = true;
    _savedHolds = Map.of(_bus.holdMs);
    // Menu held 600 ms clears the field (`CLEAR_HOLD_MS`).
    _bus.holdMs[WheelButton.menu] = entry.kClearHoldTime.inMilliseconds;
    _ownHolds = Map.of(_bus.holdMs);
    _describe();
  }

  void _release() {
    if (!_claimed) return;
    _claimed = false;
    // Only if they are still ours: another screen may have replaced them.
    if (mapEquals(_bus.holdMs, _ownHolds)) {
      _bus.holdMs
        ..clear()
        ..addAll(_savedHolds);
    }
  }

  void _onChange() {
    if (mounted) setState(() {});
    _describe();
  }

  void _describe() {
    if (!mounted || !_claimed || !_isTop) return;
    if (ref.read(shellProvider).panel != null) return;
    final playing = ref.read(playerProvider).state.isPlaying;
    if (_session.phase == SearchPhase.typing) {
      _bus.semantics.value = WheelSemantics(
        label: 'Letter ring',
        value:
            '${SearchSession.chipLabel(_session.chip)}, '
            '${_glyphWords(_session.currentGlyph)}',
        playing: playing,
      );
    } else {
      final all = _session.flatResults;
      final i = _session.selected;
      _bus.semantics.value = WheelSemantics(
        label: 'Results',
        value: i < all.length
            ? '${all[i].title}, ${i + 1} of ${all.length}'
            : '',
        playing: playing,
      );
    }
  }

  static String _glyphWords(String g) => switch (g) {
    ' ' => 'space',
    '' => '',
    _ => g,
  };

  Future<void> _open(SearchHit hit) async {
    final custom = widget.onOpen;
    if (custom != null) {
      custom(hit);
      return;
    }
    final library = await ref.read(libraryViewProvider.future);
    if (!mounted) return;
    switch (hit.kind) {
      case SearchKind.album:
        pushScreen(ref, hit.title, (_) => AlbumScreen(albumId: hit.id));
      case SearchKind.artist:
        final artist = await library.artist(hit.id);
        if (artist != null && mounted) {
          pushScreen(ref, artist.name, (_) => CollectionScreen.artist(artist));
        }
      case SearchKind.song:
        final song = await library.song(hit.id);
        if (song != null && mounted) await playFrom(ref, [song], song);
    }
  }

  /// The shell gives the top screen every wheel event; true when used. What
  /// is not taken, the shell does: Menu goes back (on an empty field, which
  /// is leaving Search), Previous and Next skip tracks and Play/Pause plays
  /// in the results.
  bool _onWheel(WheelEvent e) {
    if (!_isTop) return false;
    final feedback = ref.read(deviceFeedbackProvider);
    final typing = _session.phase == SearchPhase.typing;
    switch (e) {
      case WheelStep(:final steps):
        final moved = _session.wheel(steps);
        if (!typing) {
          if (moved) {
            _atEnd = false;
            feedback.detent();
          } else if (!_atEnd) {
            _atEnd = true;
            feedback.endReached();
          }
        } else {
          feedback.detent();
        }
        return true;
      case WheelHoldStart(button: WheelButton.menu):
        _session.menuHeld();
        return true;
      case WheelPress(:final button):
        return _onPress(button, typing);
      case _:
        return true;
    }
  }

  bool _onPress(WheelButton button, bool typing) {
    switch (button) {
      case WheelButton.centre:
        final before = _session.text;
        _session.centre();
        // At the 24-character cap a typed glyph is refused: say so, as a list
        // says it has hit its end.
        if (typing &&
            _session.text == before &&
            _session.atLimit &&
            _session.chip != entry.TextChip.delete) {
          ref.read(deviceFeedbackProvider).endReached();
        }
        return true;
      case WheelButton.previous:
        if (!typing) return false;
        _session.previous();
        return true;
      case WheelButton.next:
        if (!typing) return false;
        _session.next();
        return true;
      case WheelButton.menu:
        // Menu on an empty field leaves Search: the shell goes back (and on
        // its own, with nowhere to go, does nothing).
        if (typing && _session.text.isEmpty) return false;
        _session.menu();
        return true;
      case WheelButton.playPause:
        return _session.playPause();
    }
  }

  /// Why there is nothing to show, when there is nothing: still loading, the
  /// library could not be read, or no match (never silent).
  String? get _notice {
    if (_loadError != null) return 'Library unavailable';
    if (_loading) return 'Loading library';
    if (_session.text.isNotEmpty && _session.count == 0) return 'No matches';
    return null;
  }

  @override
  Widget build(BuildContext context) {
    final found = _session.text.isEmpty || _loading || _loadError != null
        ? null
        : '${_session.count} found';
    return ShellFrame(
      title: 'Search',
      context: found,
      body: SearchView(
        session: _session,
        withStatusBar: false,
        onOpen: _open,
        notice: _notice,
      ),
    );
  }
}
