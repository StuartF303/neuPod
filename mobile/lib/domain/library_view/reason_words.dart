// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/library/track.dart';

/// The words at the right of a greyed row for [reason]. [codec] is the
/// file's lower-case format name. `Damaged file` is the firmware's
/// (`app/runtime/library_service.c`: "Can't play: damaged file", shown
/// without its prefix, as `ui_frame.c` does); the rest are the app's own.
String unreadableReasonText(UnreadableReason reason, String codec) =>
    switch (reason) {
      UnreadableReason.notDownloaded => 'Not downloaded',
      UnreadableReason.cannotOpen => "Can't open",
      UnreadableReason.cannotRead => "Can't read",
      UnreadableReason.wrongFormat => 'Not a ${codec.toUpperCase()} file',
      UnreadableReason.damaged => 'Damaged file',
    };
