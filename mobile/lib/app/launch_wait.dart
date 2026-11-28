// SPDX-License-Identifier: MPL-2.0

/// How long the first frame waits for the saved settings (and the platform's
/// accessibility settings) to be read.
const Duration launchReadLimit = Duration(seconds: 1);

/// Waits for the launch reads, but never longer than [limit]: a stalled read
/// opens the app in the defaults, and the notifiers apply the stored values
/// when they arrive, never over a change made meanwhile.
Future<void> awaitLaunchReads(
  Iterable<Future<void>> reads, {
  Duration limit = launchReadLimit,
}) => Future.wait(reads).timeout(limit, onTimeout: () => const []);
