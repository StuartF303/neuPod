# 0009: The Android scan survives the app being closed

Status: Accepted (2026-11-23) · Owner: Android platform engineer

## Context

- Spec, "The library" (owner decision 8): the scan runs in the background, shows progress, can be cancelled, and
  survives the app being closed on Android. On iOS it resumes at the next launch, which is already done.
- Today the scan is Dart code in the app's Flutter engine: `LibraryScanController`
  (`mobile/lib/app/library_scan_providers.dart`) drives `LocalLibraryScanner`, which walks folders through
  neupod_storage (SAF) and writes through drift to the one database (decision 0005, single writer). When the user
  swipes the app away, `FlutterActivity` destroys its engine with it and the scan stops. The next launch starts it
  again, and unchanged files are skipped.
- The controller's rules must hold: one scan at a time, a restart waits for the previous scan's last write, Cancel
  keeps what was written.
- Minimum Android 10 (API 29); target and compile SDK 36; the owner's phone runs Android 17 (API 37). The CI emulator
  is API 29. The release manifest must not ask for INTERNET.

## What the documentation says

Checked 2026-11-23 (developer.android.com).

- **Foreground service types** (Android 14+): each foreground service declares a type and its permission. `dataSync`
  ("import or export operations", "local file processing") needs `FOREGROUND_SERVICE_DATA_SYNC`, an install-time
  permission, and has **no runtime prerequisites**. `mediaProcessing` (Android 15+) is for "operations on media
  assets, like converting media to different formats", which a tag scan is not. `shortService` is about 3 minutes.
- **Android 15 time limit**: `dataSync` (and `mediaProcessing`) may run 6 hours in 24 while the app is in the
  background. Then the system calls `Service.onTimeout(int, int)` and the service has a few seconds to call
  `stopSelf()`, or the app gets a `RemoteServiceException`. The timer resets when the user brings the app to the front,
  and a service started while the app is in front gets the full 6 hours after it goes to the background. Once the time
  is used up, starting another throws `ForegroundServiceStartNotAllowedException`.
- **Starting from the background** (Android 12+) throws `ForegroundServiceStartNotAllowedException`; a visible app may
  start one.
- **Notifications** (Android 13+): `POST_NOTIFICATIONS` is a runtime permission. "Apps don't need to request the
  `POST_NOTIFICATIONS` permission in order to launch a foreground service." Without it the service runs, and its notice
  is shown in the Task Manager, not the drawer. Media-session notifications are exempt, which is why playback
  (decision 0002) never asked for it. The documentation advises asking in context, after a user action.
- **Android 16** applies the job runtime quota to jobs that start while the app is visible and to jobs that run beside
  a foreground service. That covers WorkManager's expedited and ordinary work.
- **Android 17**: its behaviour changes cover none of foreground services, `dataSync` or task removal. Its background
  audio hardening applies only to apps that target API 37.
- **Swiping from Recents**: AOSP does not kill a process that has a foreground service when its task is removed. The
  activity is destroyed; the service, with `stopWithTask` false (the default), keeps running.

## Options considered

1. **Foreground service (`dataSync`) that keeps the app's own engine.** The service holds the process at foreground
   priority. When the activity goes away mid-scan, it hands its engine to a keeper instead of destroying it. The scan
   carries on in the engine it started in. The next activity takes the engine back.
2. **Foreground service that runs the scan in a second, headless engine** (a Dart entry point started by the service).
   It survives as well, but there are then two Dart isolates with two drift connections to one SQLite file. The single
   writer rule and the controller's "a restart waits for the previous write" would need cross-engine locking. The
   app's UI would have to learn the progress from the other engine over a channel. And neupod_storage and drift would
   have to start in an engine without an activity. That is a second app inside the app.
3. **WorkManager, expedited or long-running.** An expedited job is short, and Android 16 puts jobs started while the
   app is visible under the runtime quota. A long-running worker is a foreground service anyway
   (`setForeground` with a `dataSync` type), plus a new AndroidX dependency, and it still needs a Dart engine to run
   the scan: option 2's problems, with WorkManager on top.
4. **The PlaybackService (Media3) hosts it.** Its type is `mediaPlayback`, which is for playing media; holding it in
   the foreground to scan with nothing playing misuses the type (and the store's declaration). It would also tie the
   scan to the media session's lifecycle. Rejected.

## Decision

**Option 1.** It is the only one that keeps today's scan as it is: one engine, one controller, one database
connection, and the progress row in Settings > Library unchanged. Android only gains a service that keeps the process
and the engine alive.

- **Service.** `ScanService` (app module, `foregroundServiceType="dataSync"`, not exported). It does no work: the
  scan runs in Dart. It starts when the controller starts a scan (always while the app is in front: launch, return to
  front, Update Library, a folder added). It stops when the scan has stopped writing (done, failed or cancelled).
  `START_NOT_STICKY`: if the process dies anyway, the next launch scans again as it does today.
- **Keeping the engine.** `MainActivity.onDestroy` asks whether a scan is running. If one is,
  `shouldDestroyEngineWithHost()` returns false and the engine goes to `EngineKeeper`. `provideFlutterEngine()` hands
  it to the next activity, so reopening the app shows the same scan with the same progress. If the scan ends with no
  activity, the kept engine is destroyed: its plugins detach (the audio plugin's controller releases the playback
  service), and the process is left with nothing running. When no scan runs, the engine lives and dies with the
  activity, exactly as before.
- **Notification.** Channel "Library scan", low importance (no sound). It shows the firmware's words: title
  "Building library", text "N found · N read" (`scanProgressLabel`, the same text as Settings > Library), an
  indeterminate bar (the total is unknown until the walk ends, as on the firmware's sweeping bar), and a **Cancel**
  action. Tapping it opens the app. Dart sends the text, so the wording lives in one place. Updates are posted at most
  every 500 ms (Android drops a package's notification updates past a few a second), always with the newest text.
  Android 12+ defers a foreground service's notification by up to 10 seconds, so a scan of unchanged files (well under
  a second) never shows one.
- **Cancel.** The action starts the service with a cancel intent. The plugin asks Dart to cancel, and Dart runs the
  controller's own `cancel()` (the same path as the Select key in Settings > Library). Once the cancelled scan's last
  write has finished, Dart stops the service.
- **One scan at a time.** Unchanged: the controller still owns the scan. It tells the service (through
  `ScanKeepAlive`, a domain port; no-op on iOS and in tests): `started` when a scan starts, `progress` as it goes, and
  `stopped` only once the scan has stopped writing. A restart sends a second `started` (the service is already up) and
  no `stopped` in between. A cancel followed at once by a start leaves the service up for the new scan. Every
  `stopped` first waits for any scan still stopping (a cancel, a restart, a failure) to finish its last write.
- **POST_NOTIFICATIONS.** Declared, and asked for on Android 13+ only when the user started the scan (Update Library or
  a folder added). It is never asked at launch or on return to the front. Android stops showing the prompt after two
  refusals. Without the permission the scan still survives; its notice is in the Task Manager only. This is the one
  runtime permission added. The others (`FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_DATA_SYNC`) are install-time, and
  the release manifest still has no INTERNET.
- **Limits.** If the service cannot start (the app was not visible, or the 6-hour budget is spent), the scan runs in
  the app as before. On Android 15+, `onTimeout` stops the service and destroys a kept engine. An open app keeps
  scanning, and the next launch picks up from the database. A scan the user starts with the app in front resets the
  budget.
- **iOS** is unchanged. `scanServiceOverrides()` installs the service on Android only; everywhere else the port does
  nothing.

## Tests

- JVM (`android/app/src/test/.../ScanSessionTest.kt`, `./gradlew :app:testDebugUnitTest`, now run in CI's android job):
  the session (start once, stop once, update throttling with the newest text, nothing after stop) and the engine keeper
  (hand-over, destroy at scan end with no activity, the test hook).
- Dart (`test/app/scan_keep_alive_test.dart`): the controller's calls in order for done, failure, restart, cancel then
  start, the notification's Cancel, and app close. The service stops only after the cancelled scan's write has ended.
  Also covered: the channel messages and their firmware words, a failing channel, and Android-only installation.
- Instrumentation on the CI emulator (`integration_test/scan_service_test.dart`): a scan starts through the controller
  with the real service. The test calls `finishAndRemoveTask()` on the activity, as swiping it away does, and checks
  that the engine was kept and the service is running. The scan then completes (its marker file, done with no error,
  progress after the activity went), and the service stops. The scanner there is a stand-in: an emulator under
  instrumentation cannot grant a SAF folder. Debug builds only expose `debugFinishActivity`, `debugState` and
  `debugKeepEngineAfterScan`; the last keeps the engine after the scan so the test can report. Its limit: the system
  never kills an instrumented process, so it proves the engine and the scan outlive the activity, not that the
  foreground service keeps the process from being killed. That part is AOSP's documented behaviour, and it is checked
  on the owner's phone. If the engine were not kept, the test could not report, and the run would show up as the
  script's per-file timeout, not as a failed assertion.

## Consequences and risks

- A swiped-away app whose scan is still running keeps its whole engine in memory (tens of MB) until the scan ends.
  That is the cost of one engine, and it ends with the scan.
- OEM task killers that ignore foreground services (some vendors) still stop the scan; it resumes at the next launch.
- `dataSync` may be narrowed in a future Android. Google's migration guide points data transfers to user-initiated
  data transfer jobs, which need a network constraint and so do not fit a local scan. Revisit when a new target SDK
  changes the type.
- Owner check: the notification and the swipe-away on the Pixel 7 (Android 17) at the next device check.
