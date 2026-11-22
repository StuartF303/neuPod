// SPDX-License-Identifier: MPL-2.0

/// Artists, A to Z (the firmware's sort). An artist is `CollectionScreen`:
/// All Songs first, then the albums with cover, year and song count, in year
/// order (as drawn; UI styles Open question 6).
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/collection_screen.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// Artists.
class ArtistsScreen extends ConsumerWidget {
  /// The artists list.
  const ArtistsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) =>
      LibraryLoader<List<Artist>>(
        load: (library) => library.artists(),
        builder: (context, artists) => ShellFrame(
          title: 'Artists',
          context: artists == null ? null : groupedCount(artists.length),
          body: artists == null
              ? const SizedBox.shrink()
              : BrowseListBody(
                  child: Builder(
                    builder: (context) {
                      final h = NeupodTheme.of(context).tier.rowHeight;
                      return NeupodList(
                        items: [
                          for (final a in artists)
                            NeupodListItem.row(
                              height: h,
                              title: a.name,
                              chevron: true,
                              initial: a.sortLetter,
                              onActivate: () => pushScreen(
                                ref,
                                a.name,
                                (_) => CollectionScreen.artist(a),
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
