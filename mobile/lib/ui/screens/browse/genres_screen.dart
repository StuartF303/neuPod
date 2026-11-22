// SPDX-License-Identifier: MPL-2.0

/// Genres, and a genre (All Songs, then its albums: `CollectionScreen`).
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/collection_screen.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// Genres.
class GenresScreen extends ConsumerWidget {
  /// The genres list.
  const GenresScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) =>
      LibraryLoader<List<Genre>>(
        load: (library) => library.genres(),
        builder: (context, genres) => ShellFrame(
          title: 'Genres',
          context: genres == null ? null : groupedCount(genres.length),
          body: genres == null
              ? const SizedBox.shrink()
              : BrowseListBody(
                  child: Builder(
                    builder: (context) {
                      final h = NeupodTheme.of(context).tier.rowHeight;
                      return NeupodList(
                        items: [
                          for (final g in genres)
                            NeupodListItem.row(
                              height: h,
                              title: g.name,
                              chevron: true,
                              initial: g.sortLetter,
                              onActivate: () => pushScreen(
                                ref,
                                g.name,
                                (_) => CollectionScreen.genre(g),
                              ),
                            ),
                        ],
                      );
                    },
                  ),
                ),
        ),
      );
}
