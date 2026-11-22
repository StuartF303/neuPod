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

/// A pull-based recursive listing of one root. Used only on the plugin's queue.
final class Walk {
  private static let keys: [URLResourceKey] = [
    .isDirectoryKey, .fileSizeKey, .contentModificationDateKey, .ubiquitousItemDownloadingStatusKey,
  ]
  private static let icloudSuffix = ".icloud"

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
    let coordinator = NSFileCoordinator(filePresenter: nil)
    var coordError: NSError?
    var ran = false
    coordinator.coordinate(readingItemAt: root, options: [.immediatelyAvailableMetadataOnly], error: &coordError) { _ in
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
        var state = EntryState.local
        if name.hasPrefix(".") {
          guard name.hasSuffix(Self.icloudSuffix), name.count > 1 + Self.icloudSuffix.count else { continue }
          relName = String(name.dropFirst().dropLast(Self.icloudSuffix.count))
          state = .notDownloaded
        } else if let status = values?.ubiquitousItemDownloadingStatus, status == .notDownloaded {
          state = .notDownloaded
        }
        let rel = (stack.dropLast() + [relName]).joined(separator: "/")
        let size = Int64(values?.fileSize ?? 0)
        let mtime = Int64((values?.contentModificationDate?.timeIntervalSince1970 ?? 0) * 1000)
        out.append(
          StorageEntry(
            relPath: rel, uri: url.absoluteString, size: state == .local ? size : 0, modifiedMs: mtime,
            state: state))
      }
    }
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
