// SPDX-License-Identifier: MPL-2.0

/// Wires the art cache (decision 0009) to the screens and the player.
library;

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/data/art/art_cache.dart';
import 'package:neupod/domain/library_view/library_view.dart';

/// The art cache, in `art/` under the app's private support directory (kept
/// out of backups by the plugin, like the library database).
final artCacheProvider = Provider<ArtCache>((ref) {
  final storage = ref.watch(neupodStorageProvider);
  return ArtCache(
    directory: () async => '${await storage.appSupportPath()}/art',
    open: storageArtOpener(storage),
  );
});

/// The cached image file of an [ArtRef] at an [ArtSize], made on first ask;
/// null when none of its pictures decodes. A ready [ArtRef.path] passes
/// through.
final artFileProvider = FutureProvider.family<String?, (ArtRef, ArtSize)>(
  (ref, a) => ref.watch(artCacheProvider).file(a.$1, a.$2),
);
