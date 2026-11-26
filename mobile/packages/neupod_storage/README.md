# neupod_storage

Read-only folder access for the neuPod app, as chosen in
[decision 0004](../../../docs/app/decisions/0004-folder-access.md). In-repo only (not published), MPL-2.0.

- **Android:** the system folder picker (`ACTION_OPEN_DOCUMENT_TREE`) with a persisted **read-only** grant; a recursive
  walk with one `DocumentsContract` query per directory; detached read-only file descriptors read with `pread`.
- **iOS:** the app's Documents folder (visible in Files and Finder through the app's Info.plist) plus folders picked in
  Files, kept as bookmarks whose security-scoped access this plugin alone starts and holds; iCloud files that are not
  downloaded are reported as `notDownloaded` and never read.
- **Android, the shared music collection:** MediaStore audio (`IS_MUSIC`) as one more root, turned on by the user
  (`addSharedCollection`), listed by one query and opened through `ContentResolver` like a folder's files
  ([decision 0012](../../../docs/app/decisions/0012-android-shared-collection.md)).
- No call writes, moves or deletes a file. The only permission is the collection's (`READ_MEDIA_AUDIO` on Android 13+,
  `READ_EXTERNAL_STORAGE` before), asked for when the user turns it on.

The platform API is in `pigeon/messages.dart`. After changing it:

    dart run pigeon --input pigeon/messages.dart

Tests: `flutter test` here (Dart side, including the `pread` reader on the host); on an emulator and a simulator,
`scripts/app/verify-folder-access-android.sh` and `scripts/app/verify-folder-access-ios.sh`.
