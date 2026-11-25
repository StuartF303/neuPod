// SPDX-License-Identifier: MPL-2.0

import Flutter
import UIKit
import UniformTypeIdentifiers

/// Read-only folder access on iOS (decision 0004).
///
/// - The app's Documents folder is always a root (`UIFileSharingEnabled` and
///   `LSSupportsOpeningDocumentsInPlace` in the app's Info.plist make it visible
///   in Files and Finder). It needs no permission.
/// - Other folders are picked with `UIDocumentPickerViewController` and kept as
///   bookmarks (implicitly security-scoped on iOS). At launch every bookmark is
///   resolved (off the main thread) and `startAccessingSecurityScopedResource()`
///   is held for the life of the process, so the scanner (`dart:io`, any isolate)
///   and the audio engine can open `file://` URLs under the root. **Only this
///   plugin starts or stops security-scoped access**; the audio plugin never does.
/// - Nothing here writes, moves or deletes a file. The picker grants read-write
///   to the tree; read-only is enforced by this API having no write call.
///
/// Threading: all bookmark, access and walk state lives on `queue` (serial);
/// replies go back on the main thread.
public class NeupodStoragePlugin: NSObject, FlutterPlugin, NeupodStorageApi, UIDocumentPickerDelegate {
  static let documentsId = "documents"
  private static let defaultsKey = "net.stuartfraser.neupod_storage.roots.v1"

  private struct Stored {
    let id: String
    var name: String
    var bookmark: Data
  }

  private struct Held {
    let url: URL
    /// True if startAccessingSecurityScopedResource() succeeded and must be balanced.
    let scoped: Bool
  }

  private weak var registrar: FlutterPluginRegistrar?
  private let queue = DispatchQueue(label: "net.stuartfraser.neupod_storage")
  /// Download states are metadata reads with no plugin state: they run here,
  /// not behind a walk on `queue`.
  private let statusQueue = DispatchQueue(label: "net.stuartfraser.neupod_storage.status", attributes: .concurrent)
  // Touched only on `queue`.
  private var stored: [Stored] = []
  private var held: [String: Held] = [:]
  private var walks: [String: Walk] = [:]
  // Touched only on the main thread.
  private var pendingPick: ((Result<StorageRoot?, Error>) -> Void)?

  public static func register(with registrar: FlutterPluginRegistrar) {
    let instance = NeupodStoragePlugin()
    instance.registrar = registrar
    // Resolving bookmarks can touch the file system (an external drive, a
    // provider): never on the main thread at launch. Calls queue behind it.
    instance.queue.async { instance.loadAndResolve() }
    NeupodStorageApiSetup.setUp(binaryMessenger: registrar.messenger(), api: instance)
  }

  /// Runs [work] on `queue` and delivers its result on the main thread.
  private func onQueue<T>(_ completion: @escaping (Result<T, Error>) -> Void, _ work: @escaping () throws -> T) {
    queue.async {
      let result = Result { try work() }
      DispatchQueue.main.async { completion(result) }
    }
  }

  // MARK: Stored bookmarks (on `queue`)

  private func loadAndResolve() {
    let raw = UserDefaults.standard.array(forKey: Self.defaultsKey) as? [[String: Any]] ?? []
    stored = raw.compactMap { dict in
      guard let id = dict["id"] as? String, let name = dict["name"] as? String,
        let bookmark = dict["bookmark"] as? Data
      else { return nil }
      return Stored(id: id, name: name, bookmark: bookmark)
    }
    var changed = false
    for i in stored.indices where resolve(index: i) {
      changed = true
    }
    if changed { save() }
  }

  /// Resolves stored[index] and holds access. Returns true if the bookmark was stale and re-saved in `stored`.
  private func resolve(index: Int) -> Bool {
    let entry = stored[index]
    var stale = false
    guard
      let url = try? URL(
        resolvingBookmarkData: entry.bookmark, options: [], relativeTo: nil, bookmarkDataIsStale: &stale)
    else { return false }  // Unresolvable (deleted, unplugged drive): the root shows as unavailable.
    if let old = held.removeValue(forKey: entry.id), old.scoped { old.url.stopAccessingSecurityScopedResource() }
    let scoped = url.startAccessingSecurityScopedResource()
    held[entry.id] = Held(url: url, scoped: scoped)
    guard stale, let fresh = try? url.bookmarkData(options: [], includingResourceValuesForKeys: nil, relativeTo: nil)
    else { return false }
    stored[index].bookmark = fresh
    return true
  }

  private func save() {
    let raw: [[String: Any]] = stored.map { ["id": $0.id, "name": $0.name, "bookmark": $0.bookmark] }
    UserDefaults.standard.set(raw, forKey: Self.defaultsKey)
  }

  private var documentsURL: URL {
    FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
  }

  private func isReachable(_ id: String) -> Bool {
    guard let h = held[id] else { return false }
    return (try? h.url.checkResourceIsReachable()) ?? false
  }

  private func root(for entry: Stored) -> StorageRoot {
    StorageRoot(id: entry.id, displayName: entry.name, kind: .pickedFolder, available: isReachable(entry.id))
  }

  private func url(forRoot rootId: String) -> URL? {
    if rootId == Self.documentsId { return documentsURL }
    return held[rootId]?.url
  }

  /// Adds a folder the user has just granted (picker or test hook), or returns the root already holding it.
  private func add(url: URL, scoped: Bool) throws -> StorageRoot {
    let path = url.standardizedFileURL.resolvingSymlinksInPath().path
    for entry in stored {
      if let h = held[entry.id], h.url.standardizedFileURL.resolvingSymlinksInPath().path == path {
        if scoped { url.stopAccessingSecurityScopedResource() }  // Already held through the stored entry.
        return root(for: entry)
      }
    }
    let bookmark: Data
    do {
      bookmark = try url.bookmarkData(options: [], includingResourceValuesForKeys: nil, relativeTo: nil)
    } catch {
      if scoped { url.stopAccessingSecurityScopedResource() }
      throw PigeonError(code: "bookmark-failed", message: error.localizedDescription, details: nil)
    }
    let entry = Stored(id: UUID().uuidString, name: url.lastPathComponent, bookmark: bookmark)
    stored.append(entry)
    held[entry.id] = Held(url: url, scoped: scoped)
    save()
    return root(for: entry)
  }

  // MARK: NeupodStorageApi

  func pickFolder(completion: @escaping (Result<StorageRoot?, Error>) -> Void) {
    if pendingPick != nil {
      completion(.failure(PigeonError(code: "busy", message: "the folder picker is already open", details: nil)))
      return
    }
    guard let presenter = topViewController() else {
      completion(.failure(PigeonError(code: "no-activity", message: "no view controller to present from", details: nil)))
      return
    }
    let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.folder])
    picker.allowsMultipleSelection = false
    picker.delegate = self
    pendingPick = completion
    presenter.present(picker, animated: true)
  }

  public func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
    guard let completion = pendingPick else { return }
    pendingPick = nil
    guard let url = urls.first else {
      completion(.success(nil))
      return
    }
    let scoped = url.startAccessingSecurityScopedResource()
    onQueue(completion) { try self.add(url: url, scoped: scoped) }
  }

  public func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
    pendingPick?(.success(nil))
    pendingPick = nil
  }

  func listRoots(completion: @escaping (Result<[StorageRoot], Error>) -> Void) {
    onQueue(completion) {
      // A bookmark that did not resolve at launch (a drive plugged in since,
      // a provider that was slow) gets another chance on every listing.
      var changed = false
      for i in self.stored.indices where !self.isReachable(self.stored[i].id) {
        if self.resolve(index: i) { changed = true }
      }
      if changed { self.save() }
      let docs = StorageRoot(id: Self.documentsId, displayName: "neuPod", kind: .appDocuments, available: true)
      return [docs] + self.stored.map(self.root(for:))
    }
  }

  func removeRoot(rootId: String, completion: @escaping (Result<Void, Error>) -> Void) {
    onQueue(completion) {
      if rootId == Self.documentsId {
        throw PigeonError(code: "not-removable", message: "the Documents folder is always a root", details: nil)
      }
      // Cancel its walks before access ends.
      self.walks = self.walks.filter { $0.value.rootId != rootId }
      if let h = self.held.removeValue(forKey: rootId), h.scoped {
        h.url.stopAccessingSecurityScopedResource()
      }
      self.stored.removeAll { $0.id == rootId }
      self.save()
    }
  }

  func startWalk(rootId: String, completion: @escaping (Result<String, Error>) -> Void) {
    onQueue(completion) {
      guard let root = self.url(forRoot: rootId), (try? root.checkResourceIsReachable()) == true else {
        throw PigeonError(code: "root-unavailable", message: "no access to \(rootId)", details: nil)
      }
      guard let walk = Walk(rootId: rootId, root: root) else {
        throw PigeonError(code: "root-unavailable", message: "cannot list \(rootId)", details: nil)
      }
      let id = UUID().uuidString
      self.walks[id] = walk
      return id
    }
  }

  func nextWalkBatch(walkId: String, maxEntries: Int64, completion: @escaping (Result<WalkBatch, Error>) -> Void) {
    let limit = max(1, Int(maxEntries))
    onQueue(completion) {
      guard let walk = self.walks[walkId] else {
        throw PigeonError(code: "no-walk", message: "unknown, cancelled or finished walk \(walkId)", details: nil)
      }
      let batch = walk.next(limit: limit)
      if batch.done { self.walks.removeValue(forKey: walkId) }
      return batch
    }
  }

  func cancelWalk(walkId: String) throws {
    queue.async { self.walks.removeValue(forKey: walkId) }
  }

  // MARK: iCloud downloads (owner decision 7)

  /// Asks iOS to download a file that a walk reported as not downloaded. iOS
  /// fetches it in the background (its own networking, not the app's); the
  /// app reads nothing until a later walk sees the file on the device. This
  /// never writes, moves or deletes: the file's content is the user's, as it
  /// is in iCloud.
  func requestDownload(uri: String, completion: @escaping (Result<Void, Error>) -> Void) {
    onQueue(completion) {
      guard let url = URL(string: uri), url.isFileURL, self.isUnderRoot(url) else {
        throw PigeonError(code: "not-in-root", message: "not a file of a library folder", details: nil)
      }
      do {
        try FileManager.default.startDownloadingUbiquitousItem(at: ICloudItem.realURL(url))
      } catch {
        throw PigeonError(code: "download-failed", message: error.localizedDescription, details: nil)
      }
    }
  }

  func downloadStates(uris: [String], completion: @escaping (Result<[DownloadState], Error>) -> Void) {
    statusQueue.async {
      let states = uris.map { uri -> DownloadState in
        guard let url = URL(string: uri), url.isFileURL else { return .missing }
        return ICloudItem.downloadState(url)
      }
      DispatchQueue.main.async { completion(.success(states)) }
    }
  }

  /// Whether [url]'s folder is Documents or a held root, or inside one.
  /// Compares the folder, which exists even while the file is only a
  /// placeholder, with symlinks resolved (/var and /private/var).
  private func isUnderRoot(_ url: URL) -> Bool {
    func canonical(_ u: URL) -> String { u.standardizedFileURL.resolvingSymlinksInPath().path }
    let dir = canonical(url.deletingLastPathComponent())
    return ([documentsURL] + held.values.map { $0.url }).contains { root in
      let r = canonical(root)
      return dir == r || dir.hasPrefix(r.hasSuffix("/") ? r : r + "/")
    }
  }

  func openFd(uri: String, completion: @escaping (Result<Int64, Error>) -> Void) {
    completion(.failure(PigeonError(code: "unsupported", message: "iOS reads file:// URIs directly", details: nil)))
  }

  func appSupportPath() throws -> String {
    do {
      // Application Support/library, excluded from iCloud and device backups:
      // the library is rebuilt by rescanning (decision 0005).
      var dir = try FileManager.default.url(
        for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true
      ).appendingPathComponent("library", isDirectory: true)
      try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
      var values = URLResourceValues()
      values.isExcludedFromBackup = true
      try dir.setResourceValues(values)
      return dir.path
    } catch {
      throw PigeonError(code: "no-directory", message: error.localizedDescription, details: nil)
    }
  }

  func backedUpDataPath() throws -> String {
    // Application Support/stats, kept by iCloud and device backups (owner decision 20: ratings and play counts
    // survive a restore). The flag is set explicitly and read back, so a folder that was once excluded is fixed.
    // Excluding or including a folder applies to everything in it.
    var dir: URL
    do {
      dir = try FileManager.default.url(
        for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true
      ).appendingPathComponent("stats", isDirectory: true)
      try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
      var values = URLResourceValues()
      values.isExcludedFromBackup = false
      try dir.setResourceValues(values)
    } catch {
      throw PigeonError(code: "no-directory", message: error.localizedDescription, details: nil)
    }
    if try excluded(dir) {
      throw PigeonError(code: "no-directory", message: "\(dir.path) is still excluded from backup", details: nil)
    }
    return dir.path
  }

  func isExcludedFromBackup(path: String) throws -> Bool {
    try excluded(URL(fileURLWithPath: path))
  }

  private func excluded(_ url: URL) throws -> Bool {
    do {
      var u = url
      u.removeAllCachedResourceValues()
      return try u.resourceValues(forKeys: [.isExcludedFromBackupKey]).isExcludedFromBackup ?? false
    } catch {
      throw PigeonError(code: "no-file", message: error.localizedDescription, details: nil)
    }
  }

  func debugAddRoot(path: String) throws -> StorageRoot {
    #if DEBUG
      let url = URL(fileURLWithPath: path, isDirectory: true)
      let scoped = url.startAccessingSecurityScopedResource()
      return try queue.sync { try self.add(url: url, scoped: scoped) }
    #else
      throw PigeonError(code: "unsupported", message: "debugAddRoot is for debug builds", details: nil)
    #endif
  }

  // MARK: Helpers

  private func topViewController() -> UIViewController? {
    var top = registrar?.viewController
    if top == nil {
      top = UIApplication.shared.connectedScenes
        .compactMap { $0 as? UIWindowScene }
        .flatMap { $0.windows }
        .first { $0.isKeyWindow }?.rootViewController
    }
    while let presented = top?.presentedViewController { top = presented }
    return top
  }
}

/// How an iCloud file that is not on the device shows itself, from metadata
/// alone (never a read: reading a file that is not downloaded makes iOS fetch
/// it, or blocks until it has):
///
/// - a hidden placeholder `.Name.ext.icloud` in place of `Name.ext` (iCloud
///   Drive's older layout, still used for some containers);
/// - `ubiquitousItemDownloadingStatus` `notDownloaded`;
/// - the APFS dataless flag (`SF_DATALESS` in `st_flags`), which File Provider
///   sets on any file whose content is not local, including providers that
///   report no downloading status. `lstat` reads the inode only and does not
///   materialise the file.
enum ICloudItem {
  static let placeholderSuffix = ".icloud"
  /// `SF_DATALESS` in <sys/stat.h>, spelled out in case the SDK's Swift
  /// overlay does not import it.
  static let datalessFlag: UInt32 = 0x4000_0000

  /// The real file name for a placeholder name (`.a.flac.icloud` gives
  /// `a.flac`), or nil if [name] is not one.
  static func realName(ofPlaceholder name: String) -> String? {
    guard name.hasPrefix("."), name.hasSuffix(placeholderSuffix), name.count > 1 + placeholderSuffix.count
    else { return nil }
    return String(name.dropFirst().dropLast(placeholderSuffix.count))
  }

  /// The URL of the file a placeholder stands for, or [url] itself.
  static func realURL(_ url: URL) -> URL {
    guard let real = realName(ofPlaceholder: url.lastPathComponent) else { return url }
    return url.deletingLastPathComponent().appendingPathComponent(real)
  }

  static func placeholderURL(_ real: URL) -> URL {
    real.deletingLastPathComponent().appendingPathComponent("." + real.lastPathComponent + placeholderSuffix)
  }

  static func isDataless(_ path: String) -> Bool {
    var st = stat()
    guard lstat(path, &st) == 0 else { return false }
    return st.st_flags & datalessFlag != 0
  }

  /// Where the download of the file at [url] (a real URL or a placeholder's)
  /// stands. Metadata only.
  static func downloadState(_ url: URL) -> DownloadState {
    let real = realURL(url)
    let fm = FileManager.default
    let placeholder = fm.fileExists(atPath: placeholderURL(real).path)
    guard placeholder || fm.fileExists(atPath: real.path) else { return .missing }
    var u = real
    u.removeAllCachedResourceValues()
    let v = try? u.resourceValues(forKeys: [
      .ubiquitousItemDownloadingStatusKey, .ubiquitousItemIsDownloadingKey, .ubiquitousItemDownloadingErrorKey,
    ])
    if v?.ubiquitousItemIsDownloading == true { return .downloading }
    if v?.ubiquitousItemDownloadingError != nil { return .failed }
    if placeholder || v?.ubiquitousItemDownloadingStatus == .notDownloaded || isDataless(real.path) {
      return .notDownloaded
    }
    return .downloaded
  }
}

/// A pull-based recursive listing of one root. Used only on the plugin's queue.
final class Walk {
  private static let keys: [URLResourceKey] = [
    .isDirectoryKey, .fileSizeKey, .contentModificationDateKey, .ubiquitousItemDownloadingStatusKey,
  ]
  /// How long a page waits for its coordinated read to start before giving
  /// up on it (another process holding a write on the folder, a provider
  /// that does not answer). The walk then ends incomplete: the scan keeps
  /// what it had and never hangs.
  static let coordinationWait: TimeInterval = 15

  let rootId: String
  private let root: URL
  private let enumerator: FileManager.DirectoryEnumerator
  /// Path components of the current item relative to the root, kept from `enumerator.level`
  /// so that symlinked prefixes (/var vs /private/var) never matter.
  private var stack: [String] = []
  /// Set when part of the tree could not be listed; the walk is then not complete.
  private var incomplete = false
  private let errors: ErrorFlag

  /// The enumerator's error handler runs synchronously inside nextObject().
  private final class ErrorFlag { var hit = false }

  init?(rootId: String, root: URL) {
    self.rootId = rootId
    self.root = root
    let flag = ErrorFlag()
    errors = flag
    // Hidden files are not skipped by the enumerator, so iCloud placeholders
    // (".Name.ext.icloud") are seen; other hidden names are skipped below.
    // A placeholder downloaded while the walk runs may be seen twice (as the
    // placeholder and as the file); the scanner keys on the relative path, so
    // it is still one track.
    guard
      let e = FileManager.default.enumerator(
        at: root, includingPropertiesForKeys: Self.keys, options: [],
        errorHandler: { _, _ in
          flag.hit = true  // an unreadable folder: carry on, but the walk is not complete
          return true
        })
    else { return nil }
    enumerator = e
  }

  func next(limit: Int) -> WalkBatch {
    var out: [StorageEntry] = []
    var done = false
    // Coordinated metadata read of the tree, as Apple asks for iCloud folders.
    // `.immediatelyAvailableMetadataOnly`: never wait for a provider to
    // fetch anything. A watchdog cancels the coordination if it has not
    // started within `coordinationWait`; `cancel()` stops only a wait, never
    // a running accessor.
    let coordinator = NSFileCoordinator(filePresenter: nil)
    let watchdog = DispatchWorkItem { coordinator.cancel() }
    DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + Self.coordinationWait, execute: watchdog)
    var coordError: NSError?
    var ran = false
    coordinator.coordinate(readingItemAt: root, options: [.immediatelyAvailableMetadataOnly], error: &coordError) { _ in
      watchdog.cancel()
      ran = true
      while out.count < limit {
        guard let url = enumerator.nextObject() as? URL else {
          done = true
          return
        }
        let level = enumerator.level
        let name = url.lastPathComponent
        stack.removeLast(max(0, stack.count - (level - 1)))
        stack.append(name)
        if level > 64 {  // depth cap: a link loop or a pathological tree
          enumerator.skipDescendants()
          incomplete = true
          continue
        }
        let values = try? url.resourceValues(forKeys: Set(Self.keys))
        if values?.isDirectory == true {
          if name.hasPrefix(".") { enumerator.skipDescendants() }
          continue
        }
        var relName = name
        var fileURL = url
        var state = EntryState.local
        if name.hasPrefix(".") {
          // Hidden: only an iCloud placeholder is listed, under the name and
          // URL of the file it stands for (the URI then stays the same once
          // the file is downloaded).
          guard let real = ICloudItem.realName(ofPlaceholder: name) else { continue }
          relName = real
          fileURL = ICloudItem.realURL(url)
          state = .notDownloaded
        } else if values?.ubiquitousItemDownloadingStatus == .notDownloaded || ICloudItem.isDataless(url.path) {
          state = .notDownloaded
        }
        let rel = (stack.dropLast() + [relName]).joined(separator: "/")
        let size = Int64(values?.fileSize ?? 0)
        let mtime = Int64((values?.contentModificationDate?.timeIntervalSince1970 ?? 0) * 1000)
        out.append(
          StorageEntry(
            relPath: rel, uri: fileURL.absoluteString, size: state == .local ? size : 0, modifiedMs: mtime,
            state: state))
      }
    }
    watchdog.cancel()
    if coordError != nil || !ran {
      // The coordinated read failed: end the walk, and say it is incomplete.
      incomplete = true
      done = true
    }
    if errors.hit { incomplete = true }
    if done, (try? root.checkResourceIsReachable()) != true {
      incomplete = true  // the drive or provider went away: the enumerator just ran dry
    }
    return WalkBatch(entries: out, done: done, complete: done && !incomplete)
  }
}
