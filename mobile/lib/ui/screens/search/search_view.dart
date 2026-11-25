// SPDX-License-Identifier: MPL-2.0

/// Search's two drawings (device view design, section 7): typing (field, live
/// results, letter ring, mode chips, hint) and the results (the field without
/// its caret, then Artists, Albums and Songs as an ordinary list). Pure
/// drawing of a [SearchSession].
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/components/search_entry.dart';
import 'package:neupod/ui/components/small_parts.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/screens/common/scroll_window.dart';
import 'package:neupod/ui/screens/search/search_session.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';

/// The hint under the chips while typing.
const String searchHint = 'Select: type · Menu: delete · Play: results';

/// Search, drawn from [session].
class SearchView extends StatelessWidget {
  /// Draws [session].
  const SearchView({
    required this.session,
    this.playState = PlayState.stopped,
    this.onOpen,
    this.withStatusBar = true,
    this.notice,
    super.key,
  });

  /// The model.
  final SearchSession session;

  /// What the status bar's play glyph shows.
  final PlayState playState;

  /// Says why there is nothing to list (`Loading library`, `Library
  /// unavailable`, `No matches`), in the results' place.
  final String? notice;

  /// Draw the status bar (goldens of the view alone); the shell's frame
  /// draws it for the screen.
  final bool withStatusBar;

  /// A row was opened by a tap or the screen reader.
  final void Function(SearchHit hit)? onOpen;

  @override
  Widget build(BuildContext context) {
    final typing = session.phase != SearchPhase.results;
    final text = session.text;
    final found = text.isEmpty ? null : '${session.count} found';
    final body = typing ? _typing(context) : _results(context);
    if (!withStatusBar) return body;
    return NeupodScaffold(
      statusBar: StatusBar(
        title: 'Search',
        playState: playState,
        context: found,
      ),
      body: body,
    );
  }

  Widget _typing(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final k = theme.tier.k;
    return ListGround(
      child: LayoutBuilder(
        builder: (context, c) {
          // Three live results as drawn; fewer when the larger text sizes
          // leave no room for the ring, chips and hint under them (device
          // view Open question 13).
          final fixed = (60 + 4 + 64 + 40 + 6 + 26) * k;
          final fit = ((c.maxHeight - fixed) / (SearchResultRow.height * k))
              .floor();
          final rows = fit.clamp(0, liveResultCount);
          final live = session.liveResults.take(rows).toList();
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              SearchFieldBox(text: session.text),
              SizedBox(
                height: rows * SearchResultRow.height * k,
                child: Column(
                  children: [
                    if (live.isEmpty && notice != null && rows > 0)
                      Expanded(child: _Notice(notice!)),
                    for (final hit in live)
                      SearchResultRow(
                        title: hit.title,
                        label: hit.label,
                        greyed: hit.reason != null,
                        onTap: () => onOpen?.call(hit),
                      ),
                  ],
                ),
              ),
              SizedBox(height: 4 * k),
              LetterRing(glyphs: session.ringGlyphs),
              ModeChips(
                labels: [
                  for (final chip in session.chips)
                    SearchSession.chipLabel(chip),
                ],
                active: session.activeChip,
                onTap: session.selectChip,
              ),
              SizedBox(height: 6 * k),
              const SearchHint(searchHint),
            ],
          );
        },
      ),
    );
  }

  Widget _results(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final k = theme.tier.k;
    final tier = theme.tier;
    final r = session.results;
    final selected = session.selected;

    // The list as entries (a heading or a hit), built into rows only when in
    // view: a library of 20,000 songs can match almost every search.
    final entries = <(String?, SearchHit?, int)>[];
    int? selectedItem;
    var index = 0;
    void group(String heading, List<SearchHit> hits) {
      if (hits.isEmpty) return;
      entries.add((heading, null, 0));
      for (final hit in hits) {
        if (index == selected) selectedItem = entries.length;
        entries.add((null, hit, index));
        index++;
      }
    }

    group('Artists', r.artists);
    group('Albums', r.albums);
    group('Songs', r.songs);

    Widget row(int i) {
      final (heading, hit, n) = entries[i];
      if (heading != null) return SectionHeading(heading);
      final song = hit!.kind == SearchKind.song;
      return NeupodRow(
        title: hit.title,
        subtitle: song ? hit.detail : null,
        value: hit.kind == SearchKind.album && hit.year != null
            ? '${hit.year}'
            : null,
        chevron: hit.kind == SearchKind.artist,
        disabledReason: hit.reason,
        selection: n == selected ? RowSelection.selected : RowSelection.none,
        position: '${n + 1} of ${session.count}',
        onTap: () => onOpen?.call(hit),
      );
    }

    double heightOf(int i) {
      final (heading, hit, _) = entries[i];
      if (heading != null) return 30 * k;
      return hit!.kind == SearchKind.song ? tier.row2Height : tier.rowHeight;
    }

    return ListGround(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SearchFieldBox(text: session.text, caret: false),
          Expanded(
            child: entries.isEmpty && notice != null
                ? _Notice(notice!)
                : LazyScrollWindow(
                    itemCount: entries.length,
                    heightOf: heightOf,
                    itemBuilder: row,
                    selected: selectedItem,
                  ),
          ),
        ],
      ),
    );
  }
}

/// A line saying why a list is empty, centred in its space.
class _Notice extends StatelessWidget {
  const _Notice(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    return Semantics(
      liveRegion: true,
      label: text,
      child: ExcludeSemantics(
        child: Center(
          child: NeupodText(
            text,
            NeupodType(theme.tier).body(theme.style.list.secondary),
            align: TextAlign.center,
          ),
        ),
      ),
    );
  }
}
