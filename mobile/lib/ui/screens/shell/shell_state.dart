// SPDX-License-Identifier: MPL-2.0

/// The navigation state of the device's screen: a stack of screens, a
/// centred panel over it, and the registry through which other screens
/// (Now Playing, Search) plug in.
///
/// For the engineer building Now Playing, the panel and Search:
///
/// - Register your screens once, before the first frame (for example in an
///   override of [shellRegistryProvider] or in `NeuPodApp`):
///   `ref.read(shellRegistryProvider).register(ShellDestination.nowPlaying,
///   (context) => const NowPlayingScreen())`.
/// - Open one with `ref.read(shellProvider.notifier).open(
///   ShellDestination.nowPlaying)`; push anything else with `push`.
/// - Show a centred panel with `showPanel(title: ..., message: ...)`; it
///   returns when Select or Menu (or a tap on OK) dismisses it.
/// - Build your screen from `ShellFrame` (status bar, 600x450 canvas, style)
///   and, for lists, `NeupodList`; take wheel events with
///   `ScreenScope.of(context).handle.wheel` (see `screen_scope.dart`).
library;

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/ui/screens/shell/stand_in_screen.dart';

/// Screens that other screens open and that the Browse screens do not build.
enum ShellDestination {
  /// Now Playing (Home's item while something is loaded, and after Select on
  /// a song).
  nowPlaying,

  /// Search, typed with the wheel (the Music menu's last item).
  search;

  /// The name on the screen.
  String get title => switch (this) {
    nowPlaying => 'Now Playing',
    search => 'Search',
  };
}

/// Which screen builds a [ShellDestination]. Until a screen registers, the
/// destination opens a stand-in.
class ShellRegistry {
  /// A registry with [builders] already registered.
  ShellRegistry([Map<ShellDestination, WidgetBuilder>? builders])
    : _builders = {...?builders};

  final Map<ShellDestination, WidgetBuilder> _builders;

  /// Registers (or replaces) the screen for [destination].
  void register(ShellDestination destination, WidgetBuilder builder) =>
      _builders[destination] = builder;

  /// Removes it again (tests).
  void unregister(ShellDestination destination) =>
      _builders.remove(destination);

  /// The registered builder, or null.
  WidgetBuilder? builderFor(ShellDestination destination) =>
      _builders[destination];

  /// Whether a real screen is registered.
  bool has(ShellDestination destination) => _builders.containsKey(destination);
}

/// The registry.
final shellRegistryProvider = Provider<ShellRegistry>((ref) => ShellRegistry());

/// One screen on the stack.
@immutable
class ShellPage {
  /// A page; [name] is for tests and semantics only.
  ShellPage({required this.name, required this.builder}) : key = UniqueKey();

  /// A name for debugging and tests (`Music`, `Songs`).
  final String name;

  /// Builds the screen at 600x450.
  final WidgetBuilder builder;

  /// Identifies the page's state while it is on the stack.
  final Key key;
}

/// A centred panel the shell is showing.
@immutable
class PanelRequest {
  /// A panel.
  const PanelRequest({
    required this.title,
    required this.message,
    this.okLabel = 'OK',
    this.onDismissed,
  });

  /// Something lost: the library update was stopped before it finished (UI
  /// styles design, the messages that move to the panel).
  const PanelRequest.libraryUpdateStopped({this.onDismissed})
    : title = 'Library update stopped',
      message =
          'Some songs may be missing. Update the library again to finish.',
      okLabel = 'OK';

  /// The title, in Body.
  final String title;

  /// The text under it.
  final String message;

  /// The button's label.
  final String okLabel;

  /// Called once, after the panel went away.
  final VoidCallback? onDismissed;
}

/// The stack and the panel.
@immutable
class ShellState {
  /// A state.
  const ShellState({required this.pages, this.panel, this.lastWasPush = true});

  /// Bottom first; the last is the top. Never empty after the root is set.
  final List<ShellPage> pages;

  /// The panel over everything, or null.
  final PanelRequest? panel;

  /// Whether the last change pushed (slides in) rather than popped (slides
  /// out); the transition reads it.
  final bool lastWasPush;

  /// The screen on top.
  ShellPage? get top => pages.isEmpty ? null : pages.last;

  /// Home is the only screen.
  bool get atRoot => pages.length <= 1;
}

/// The navigator of the device's screen.
class ShellController extends Notifier<ShellState> {
  ShellRegistry get _registry => ref.read(shellRegistryProvider);

  /// Completes when the panel on show is dismissed.
  Completer<void>? _panelDone;

  @override
  ShellState build() => const ShellState(pages: []);

  /// Sets the first screen (Home) when there is none.
  void setRoot(ShellPage page) {
    if (state.pages.isEmpty) state = ShellState(pages: [page]);
  }

  /// Puts [page] on top.
  void push(ShellPage page) {
    state = ShellState(pages: [...state.pages, page], panel: state.panel);
  }

  /// Goes back one screen; Home stays. Returns whether a screen went.
  bool pop() {
    if (state.pages.length <= 1) return false;
    state = ShellState(
      pages: state.pages.sublist(0, state.pages.length - 1),
      panel: state.panel,
      lastWasPush: false,
    );
    return true;
  }

  /// Goes back to Home.
  void popToRoot() {
    if (state.pages.length <= 1) return;
    state = ShellState(
      pages: [state.pages.first],
      panel: state.panel,
      lastWasPush: false,
    );
  }

  /// Opens a registered destination (or its stand-in).
  void open(ShellDestination destination) {
    final builder = _registry.builderFor(destination);
    push(
      ShellPage(
        name: destination.name,
        builder: builder ?? (_) => StandInScreen(destination.title),
      ),
    );
  }

  /// Shows a centred panel. Select or Menu, or a tap on OK, dismisses it.
  Future<void> showPanel({
    required String title,
    required String message,
    String okLabel = 'OK',
    VoidCallback? onDismissed,
  }) => showPanelRequest(
    PanelRequest(
      title: title,
      message: message,
      okLabel: okLabel,
      onDismissed: onDismissed,
    ),
  );

  /// Shows [request] (for example `PanelRequest.libraryUpdateStopped()`).
  Future<void> showPanelRequest(PanelRequest request) {
    _panelDone?.complete();
    final done = Completer<void>();
    _panelDone = done;
    state = ShellState(
      pages: state.pages,
      panel: request,
      lastWasPush: state.lastWasPush,
    );
    return done.future;
  }

  /// Dismisses the panel.
  void dismissPanel() {
    final request = state.panel;
    if (request == null) return;
    state = ShellState(pages: state.pages, lastWasPush: state.lastWasPush);
    _panelDone?.complete();
    _panelDone = null;
    request.onDismissed?.call();
  }
}

/// The shell's navigator.
final shellProvider = NotifierProvider<ShellController, ShellState>(
  ShellController.new,
);
