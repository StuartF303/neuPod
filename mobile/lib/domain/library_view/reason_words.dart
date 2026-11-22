// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/library/track.dart';

/// The words at the right of a greyed row for [reason]. [codec] is the
/// file's lower-case format name. Same wording as the Songs list.
String unreadableReasonText(UnreadableReason reason, String codec) =>
    switch (reason) {
      UnreadableReason.notDownloaded => 'Not downloaded',
      UnreadableReason.cannotOpen => "Can't open",
      UnreadableReason.cannotRead => "Can't read",
      UnreadableReason.wrongFormat => 'Not a ${codec.toUpperCase()} file',
      UnreadableReason.damaged => 'Damaged',
    };
