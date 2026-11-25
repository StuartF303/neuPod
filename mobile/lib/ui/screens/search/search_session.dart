// SPDX-License-Identifier: MPL-2.0

/// Search's state, with no Flutter in it: the letter ring model
/// (`domain/text_entry`, a port of the firmware's) plus the two phases the
/// device view design draws, typing and results. The screen feeds it the
/// wheel's events and draws what it holds.
///
/// Controls, from the design ("Search, by the wheel"):
///
/// | | typing | results |
/// | --- | --- | --- |
/// | wheel | turns the ring | moves the selection |
/// | centre | types the glyph | opens the row |
/// | Previous, Next | along the mode chips | (the screen skips tracks) |
/// | Menu | deletes one; on an empty field leaves | back to typing, text kept |
/// | Menu held | clears the field | |
/// | Play/Pause | to the results | (the screen plays or pauses) |
library;

import 'package:neupod/domain/text_entry/wheel_text_entry.dart' as entry;

/// What a hit is.
enum SearchKind { artist, album, song }

/// One result: a name, what it is, and enough to open it.
class SearchHit {
  /// A hit.
  const SearchHit({
    required this.kind,
    required this.id,
    required this.title,
    this.detail,
    this.year,
    this.reason,
  });

  /// Artist, album or song.
  final SearchKind kind;

  /// The library's id for it.
  final String id;

  /// The name.
  final String title;

  /// For a song, `Artist · Album`; for an album, its artist.
  final String? detail;

  /// For an album, the year.
  final int? year;

  /// For a song that cannot play, why (`Damaged file`): its row is greyed
  /// with the reason at the right, and opening it does nothing.
  final String? reason;

  /// What it is, at the right of a live result in Small: `Artist`,
  /// `Album · 1991`, or a song's artist.
  String get label => switch (kind) {
    SearchKind.artist => 'Artist',
    SearchKind.album => year == null ? 'Album' : 'Album · $year',
    SearchKind.song => reason ?? detail ?? 'Song',
  };

  @override
  bool operator ==(Object other) =>
      other is SearchHit &&
      other.kind == kind &&
      other.id == id &&
      other.title == title &&
      other.detail == detail &&
      other.year == year &&
      other.reason == reason;

  @override
  int get hashCode => Object.hash(kind, id, title, detail, year, reason);
}

/// The matches for a query, grouped as the results list shows them. Each
/// group is in the library's collation order, as the library returns it.
class SearchResults {
  /// Results of the three kinds.
  const SearchResults({
    this.artists = const [],
    this.albums = const [],
    this.songs = const [],
  });

  /// No matches.
  static const SearchResults none = SearchResults();

  /// Matching artists.
  final List<SearchHit> artists;

  /// Matching albums.
  final List<SearchHit> albums;

  /// Matching songs.
  final List<SearchHit> songs;

  static final Expando<List<SearchHit>> _all = Expando<List<SearchHit>>();

  /// Every hit, artists first. Built once per result set.
  List<SearchHit> get all =>
      _all[this] ??= List.unmodifiable([...artists, ...albums, ...songs]);

  /// How many in all (`4 found`).
  int get count => artists.length + albums.length + songs.length;
}

/// Where Search gets its matches: the library's read-only view
/// (`LibraryView`), adapted. Synchronous, as the firmware's filter is: the
/// results follow every edit.
typedef SearchFunction = SearchResults Function(String query);

/// How many live results show while typing (as the design draws; device view
/// Open question 13 is about how many fit at the larger text sizes).
const int liveResultCount = 3;

/// The most characters a search takes: what the field shows without clipping the
/// end, where the caret is (the firmware leaves the limit to the caller).
const int maxLength = 24;

/// Which of Search's two drawings is showing, or that it was left.
enum SearchPhase { typing, results, left }

/// Search's model.
class SearchSession {
  /// A search over [search]. [onChange] is called after anything the screen
  /// must redraw.
  SearchSession({required this.search, String initial = '', this.onChange}) {
    _entry = entry.WheelTextEntry(
      initial: initial,
      maxChars: maxLength,
      search: true,
      onChange: _edited,
    );
    if (initial.isNotEmpty) _results = search(initial);
  }

  /// Where the matches come from.
  final SearchFunction search;

  /// Called after every change of state.
  final void Function()? onChange;

  late final entry.WheelTextEntry _entry;
  SearchResults _results = SearchResults.none;
  SearchPhase _phase = SearchPhase.typing;
  int _selected = 0;

  /// Typing, in the results, or left.
  SearchPhase get phase => _phase;

  /// The text typed so far.
  String get text => _entry.text;

  /// The mode chip in use.
  entry.TextChip get chip => _entry.chip;

  /// The chips on offer, in order.
  List<entry.TextChip> get chips => _entry.chips();

  /// The label of each chip (`abc`, `ABC`, `123`, `#!?`, `space`, `del`).
  static String chipLabel(entry.TextChip c) => switch (c) {
    entry.TextChip.lower => 'abc',
    entry.TextChip.upper => 'ABC',
    entry.TextChip.digits => '123',
    entry.TextChip.symbols => '#!?',
    entry.TextChip.space => 'space',
    entry.TextChip.delete => 'del',
  };

  /// Index of the active chip in [chips].
  int get activeChip => chips.indexOf(_entry.chip);

  /// The nine glyphs of the ring, the current one in the middle. An action
  /// chip (`space`, `del`) has one key: it shows alone, its neighbours blank.
  List<String> get ringGlyphs {
    final n = _entry.stripLength();
    return [
      for (var o = -4; o <= 4; o++)
        if (n <= 1)
          (o == 0 ? _entry.stripKey(0).$2 : '')
        else
          _entry.stripKey(((_entry.sel + o) % n + n) % n).$2,
    ];
  }

  /// The field is full: a typed glyph is refused ([maxLength]).
  bool get atLimit => _entry.chars >= maxLength;

  /// The glyph the centre button would type.
  String get currentGlyph => _entry.stripKey(_entry.sel).$2;

  /// The best matches, shown while typing.
  List<SearchHit> get liveResults =>
      _results.all.take(liveResultCount).toList();

  /// How many matches (`4 found`).
  int get count => _results.count;

  /// The full results, grouped.
  SearchResults get results => _results;

  /// The results as one list, as the selection walks it.
  List<SearchHit> get flatResults => _results.all;

  /// The selected hit's index in [flatResults], in the results phase.
  int get selected => _selected;

  /// Called when a row is opened (the centre in the results, a tap).
  void Function(SearchHit hit)? onOpen;

  /// Called when the screen is left (Menu on an empty field).
  void Function()? onLeave;

  void _edited(String text) {
    _results = text.isEmpty ? SearchResults.none : search(text);
    _selected = 0;
  }

  void _changed() => onChange?.call();

  /// The library changed under the results (it finished loading): search
  /// again for the text so far.
  void refresh() {
    final text = _entry.text;
    _results = text.isEmpty ? SearchResults.none : search(text);
    _selected = 0;
    _changed();
  }

  /// The wheel turned by [steps] (already accelerated). True when something
  /// moved: the ring, or the selection.
  bool wheel(int steps) {
    if (_phase == SearchPhase.left) return false;
    if (_phase == SearchPhase.typing) {
      final before = _entry.sel;
      _entry.event(entry.WheelEvent.scroll, steps);
      _changed();
      return _entry.sel != before || _entry.stripLength() > 1;
    }
    final n = _results.count;
    if (n == 0) return false;
    final to = (_selected + steps).clamp(0, n - 1);
    final moved = to != _selected;
    _selected = to;
    if (moved) _changed();
    return moved;
  }

  /// The centre button.
  void centre() {
    switch (_phase) {
      case SearchPhase.typing:
        _entry.event(entry.WheelEvent.select);
      case SearchPhase.results:
        final all = flatResults;
        if (_selected < all.length) onOpen?.call(all[_selected]);
      case SearchPhase.left:
        return;
    }
    _changed();
  }

  /// Previous: the chip before this one, while typing.
  void previous() {
    if (_phase != SearchPhase.typing) return;
    _entry.event(entry.WheelEvent.left);
    _changed();
  }

  /// Next: the chip after this one, while typing.
  void next() {
    if (_phase != SearchPhase.typing) return;
    _entry.event(entry.WheelEvent.right);
    _changed();
  }

  /// Taps chip [i] (direct touch).
  void selectChip(int i) {
    final all = chips;
    if (_phase != SearchPhase.typing || i < 0 || i >= all.length) return;
    _entry
      ..chip = all[i]
      ..sel = 0;
    _changed();
  }

  /// Menu pressed: delete one; on an empty field leave; in the results back
  /// to typing with the text kept.
  void menu() {
    switch (_phase) {
      case SearchPhase.typing:
        final r = _entry.event(entry.WheelEvent.menu);
        if (r == entry.WheelResult.cancelled) {
          _phase = SearchPhase.left;
          onLeave?.call();
        }
      case SearchPhase.results:
        _phase = SearchPhase.typing;
      case SearchPhase.left:
        return;
    }
    _changed();
  }

  /// Menu held for [entry.kClearHoldTime]: clear the field.
  void menuHeld() {
    if (_phase != SearchPhase.typing) return;
    _entry.clear();
    _changed();
  }

  /// Play/Pause: to the results while typing. True when this took it; in the
  /// results it is play or pause again, which the screen does.
  bool playPause() {
    if (_phase != SearchPhase.typing) return false;
    _phase = SearchPhase.results;
    _selected = 0;
    _changed();
    return true;
  }
}
