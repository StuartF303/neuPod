// SPDX-License-Identifier: MPL-2.0

import Foundation

/// Whether a file's bytes are on the phone, from metadata alone (owner
/// decision 7, decision 0004 "iCloud files not downloaded").
///
/// Opening a file that iCloud has evicted makes iOS fetch it, which would be
/// a download nobody tapped for. A queued song can be evicted after it was
/// queued, so the engine checks before it opens one (the current item, the
/// next one it pre-schedules, and the lock screen's embedded art) and treats
/// a file that is not on the phone as unplayable: the queue skips it.
///
/// The same three signs neupod_storage's walk uses (`ICloudItem` there; the
/// two plugins share no code): a `.Name.ext.icloud` placeholder beside it,
/// `ubiquitousItemDownloadingStatus` `notDownloaded`, or the APFS dataless
/// flag. `lstat` and resource values do not materialise the file.
enum LocalFile {
  /// `SF_DATALESS` in <sys/stat.h>.
  static let datalessFlag: UInt32 = 0x4000_0000

  static func isOnDevice(_ url: URL) -> Bool {
    let placeholder = url.deletingLastPathComponent()
      .appendingPathComponent("." + url.lastPathComponent + ".icloud")
    if FileManager.default.fileExists(atPath: placeholder.path) { return false }
    var st = stat()
    if lstat(url.path, &st) == 0, st.st_flags & datalessFlag != 0 { return false }
    var u = url
    u.removeAllCachedResourceValues()
    if let status = (try? u.resourceValues(forKeys: [.ubiquitousItemDownloadingStatusKey]))?
      .ubiquitousItemDownloadingStatus, status == .notDownloaded
    {
      return false
    }
    return true
  }

  /// The error a queue entry that is not on the phone fails with.
  static func notDownloadedError(_ url: URL) -> NSError {
    NSError(
      domain: "neupod_audio", code: 2,
      userInfo: [NSLocalizedDescriptionKey: "Not downloaded: \(url.lastPathComponent)"])
  }
}
