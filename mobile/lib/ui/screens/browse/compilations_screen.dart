// SPDX-License-Identifier: MPL-2.0

/// Compilations: albums flagged as compilations or by Various Artists, which
/// are not listed under each contributing artist.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/screens/browse/albums_screen.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';

/// Compilations.
class CompilationsScreen extends StatelessWidget {
  /// The compilations.
  const CompilationsScreen({super.key});

  @override
  Widget build(BuildContext context) => AlbumListScreen(
    title: 'Compilations',
    load: (library) => library.compilations(),
    subtitle: (a) => albumDetails(a, length: false).join(' · '),
  );
}
