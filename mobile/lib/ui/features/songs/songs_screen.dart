// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/ui/features/songs/songs_providers.dart';

/// Plain Songs list (phase 1): title over "artist · album", in the
/// firmware's sort order. Phase 2 restyles it.
class SongsScreen extends ConsumerWidget {
  const SongsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final songs = ref.watch(songsProvider);
    final scan = ref.watch(scanProvider);
    final scanning = scan != null && !scan.done;
    return Scaffold(
      appBar: AppBar(
        title: const Text('Songs'),
        actions: [
          TextButton(
            onPressed: scanning
                ? null
                : () => ref.read(scanProvider.notifier).start(),
            child: const Text('Scan'),
          ),
        ],
      ),
      body: SafeArea(
        child: Column(
          children: [
            if (scan != null) _ScanLine(scan),
            Expanded(
              child: switch (songs) {
                AsyncData(:final value) when value.isEmpty => const Center(
                  child: Text('No songs. Add a folder, then Scan.'),
                ),
                AsyncData(:final value) => ListView.builder(
                  itemCount: value.length,
                  itemBuilder: (context, i) => SongRow(track: value[i]),
                ),
                AsyncError(:final error) => Center(
                  child: Text('Could not read the library: $error'),
                ),
                _ => const Center(child: CircularProgressIndicator()),
              },
            ),
          ],
        ),
      ),
    );
  }
}

class _ScanLine extends StatelessWidget {
  const _ScanLine(this.scan);

  final ScanProgress scan;

  @override
  Widget build(BuildContext context) {
    final text = scan.error != null
        ? 'Scan stopped: ${scan.error}'
        : scan.done
        ? 'Scan finished: ${scan.filesFound} files'
        : 'Scanning: ${scan.filesFound} files found, ${scan.filesRead} read';
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Align(alignment: Alignment.centerLeft, child: Text(text)),
    );
  }
}

/// The words for a reason, at the right of a greyed row.
String reasonText(UnreadableReason reason, String codec) => switch (reason) {
  UnreadableReason.notDownloaded => 'Not downloaded',
  UnreadableReason.cannotOpen => "Can't open",
  UnreadableReason.cannotRead => "Can't read",
  UnreadableReason.wrongFormat => 'Not a ${codec.toUpperCase()} file',
  UnreadableReason.damaged => 'Damaged',
};

/// One song: the title over "artist · album". A file whose tags could not
/// be read is greyed with the reason at the right, as on the device.
class SongRow extends StatelessWidget {
  const SongRow({required this.track, super.key});

  final Track track;

  @override
  Widget build(BuildContext context) {
    final reason = track.unreadableReason;
    final subtitle = track.artistAndAlbum;
    final row = ListTile(
      title: Text(
        track.displayTitle,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
      ),
      subtitle: subtitle.isEmpty
          ? null
          : Text(subtitle, maxLines: 1, overflow: TextOverflow.ellipsis),
      trailing: reason == null ? null : Text(reasonText(reason, track.codec)),
    );
    return reason == null ? row : Opacity(opacity: 0.5, child: row);
  }
}
