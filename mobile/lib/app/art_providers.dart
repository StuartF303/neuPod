// SPDX-License-Identifier: MPL-2.0

/// Wires the art cache (decision 0010) to the screens and the player.
library;

import 'package:flutter/foundation.dart';
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
    // iOS: never open a picture's file that iCloud has not put on the phone.
    open: storageArtOpener(
      storage,
      checkDownloaded: defaultTargetPlatform == TargetPlatform.iOS,
    ),
  );
});

/// The cached image file of an [ArtRef] at an [ArtSize], made on first ask;
/// null when none of its pictures decodes. A ready [ArtRef.path] passes
/// through. A cover that leaves the screen before its render starts
/// withdraws it (the render is dropped if nobody else waits); one that got
/// its file keeps it.
final artFileProvider = FutureProvider.autoDispose
    .family<String?, (ArtRef, ArtSize)>((ref, a) async {
      final ticket = ArtTicket();
      ref.onDispose(ticket.cancel);
      final path = await ref
          .watch(artCacheProvider)
          .file(a.$1, a.$2, ticket: ticket);
      if (path != null) ref.keepAlive();
      return path;
    });
