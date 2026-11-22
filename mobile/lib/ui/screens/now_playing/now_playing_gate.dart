// SPDX-License-Identifier: MPL-2.0

/// Now Playing's part of the design gate's switches: where the rating stars
/// go, and whether the system volume has been moving (owner decision 10).
/// Temporary, with the rest of the gate panel.
library;

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_options.dart';

/// The rating placement switch and the volume status line, for the gate panel.
class NowPlayingGateSection extends ConsumerWidget {
  /// The section.
  const NowPlayingGateSection({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final placement = ref.watch(ratingPlacementProvider);
    final failure = ref.watch(volumeFailureProvider);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            'Rating stars',
            style: TextStyle(fontWeight: FontWeight.w600),
          ),
          const SizedBox(height: 4),
          SegmentedButton<RatingPlacement>(
            showSelectedIcon: false,
            segments: const [
              ButtonSegment(
                value: RatingPlacement.page2,
                label: Text('1: page 2'),
              ),
              ButtonSegment(
                value: RatingPlacement.belowFormat,
                label: Text('2: + under format'),
              ),
            ],
            selected: {placement},
            onSelectionChanged: (s) =>
                ref.read(ratingPlacementProvider.notifier).set(s.first),
          ),
          const SizedBox(height: 8),
          const Text(
            'System volume (owner decision 10)',
            style: TextStyle(fontWeight: FontWeight.w600),
          ),
          Text(
            failure == null
                ? 'Every step worked.'
                : 'FAILED: ${failure.status?.name ?? 'error: ${failure.error}'}, '
                      '${failure.count} step(s). '
                      'The fallback is the owner\'s to choose.',
            style: TextStyle(
              color: failure == null ? null : const Color(0xFFB00020),
            ),
          ),
        ],
      ),
    );
  }
}
