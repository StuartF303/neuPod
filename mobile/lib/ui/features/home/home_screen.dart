// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/ui/features/library_setup/library_setup_screen.dart';

/// Scaffold placeholder for the Home screen: the app name and the size of the
/// library, read through [librarySummaryProvider].
class HomeScreen extends ConsumerWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final summary = ref.watch(librarySummaryProvider);
    final textTheme = Theme.of(context).textTheme;
    return Scaffold(
      body: SafeArea(
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text('neuPod', style: textTheme.headlineMedium),
              const SizedBox(height: 8),
              Text(summary.songCountLabel, style: textTheme.bodyLarge),
              const SizedBox(height: 24),
              // Phase 1 entry point; phase 2 replaces Home with the device view.
              TextButton(
                onPressed: () => Navigator.of(context).push(
                  MaterialPageRoute<void>(
                    builder: (_) => const LibrarySetupScreen(),
                  ),
                ),
                child: const Text('Folders'),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
