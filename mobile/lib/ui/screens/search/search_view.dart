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

/// The hint under the chips while typing.
const String searchHint = 'Select: type · Menu: delete · Play: results';

/// Search, drawn from [session].
class SearchView extends StatelessWidget {
  /// Draws [session].
  const SearchView({
    required this.session,
    this.playState = PlayState.stopped,
    this.onOpen,
    super.key,
  });

  /// The model.
  final SearchSession session;

  /// What the status bar's play glyph shows.
  final PlayState playState;

  /// A row was opened by a tap or the screen reader (the host takes pointer
  /// touches away in direct-touch candidate 1; the semantics action stays).
  final void Function(SearchHit hit)? onOpen;

  @override
  Widget build(BuildContext context) {
    final typing = session.phase != SearchPhase.results;
    final text = session.text;
    final found = text.isEmpty ? null : '${session.count} found';
    return NeupodScaffold(
      statusBar: StatusBar(
        title: 'Search',
        playState: playState,
        context: found,
      ),
      body: typing ? _typing(context) : _results(context),
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
                    for (final hit in live)
                      SearchResultRow(
                        title: hit.title,
                        label: hit.label,
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

    // The list as rows with their heights, to keep the selection in view.
    final items = <Widget>[];
    final heights = <double>[];
    int? selectedItem;
    var index = 0;
    void group(String heading, List<SearchHit> hits) {
      if (hits.isEmpty) return;
      items.add(SectionHeading(heading));
      heights.add(30 * k);
      for (final hit in hits) {
        final song = hit.kind == SearchKind.song;
        final isSel = index == selected;
        if (isSel) selectedItem = items.length;
        final i = index;
        items.add(
          NeupodRow(
            title: hit.title,
            subtitle: song ? hit.detail : null,
            value: hit.kind == SearchKind.album && hit.year != null
                ? '${hit.year}'
                : null,
            chevron: hit.kind == SearchKind.artist,
            selection: isSel ? RowSelection.selected : RowSelection.none,
            position: '${i + 1} of ${session.count}',
            onTap: () => onOpen?.call(hit),
          ),
        );
        heights.add(song ? tier.row2Height : tier.rowHeight);
        index++;
      }
    }

    group('Artists', r.artists);
    group('Albums', r.albums);
    group('Songs', r.songs);

    return ListGround(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SearchFieldBox(text: session.text, caret: false),
          Expanded(
            child: ScrollWindow(
              heights: heights,
              selected: selectedItem,
              children: items,
            ),
          ),
        ],
      ),
    );
  }
}
