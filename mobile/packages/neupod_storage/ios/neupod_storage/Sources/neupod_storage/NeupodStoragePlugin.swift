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
///   resolved and `startAccessingSecurityScopedResource()` is held for the life
///   of the process, so the scanner (`dart:io`, any isolate) and the audio engine
///   can open `file://` URLs under the root. **Only this plugin starts or stops
///   security-scoped access**; the audio plugin never does.
/// - Nothing here writes, moves or deletes a file. The picker grants read-write
///   to the tree; read-only is enforced by this API having no write call.
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
  private var stored: [Stored] = []
  private var held: [String: Held] = [:]
  private var pendingPick: ((Result<StorageRoot?, Error>) -> Void)?
  private var walks: [String: Walk] = [:]
  private let walkQueue = DispatchQueue(label: "net.stuartfraser.neupod_storage.walk")

  public static func register(with registrar: FlutterPluginRegistrar) {
    let instance = NeupodStoragePlugin()
    instance.registrar = registrar
    instance.loadAndResolve()
    NeupodStorageApiSetup.setUp(binaryMessenger: registrar.messenger(), api: instance)
  }

  // MARK: Stored bookmarks

  private func loadAndResolve() {
    let raw = UserDefaults.standard.array(forKey: Self.defaultsKey) as? [[String: Any]] ?? []
    stored = raw.compactMap { dict in
      guard let id = dict["id"] as? String, let name = dict["name"] as? String,
        let bookmark = dict["bookmark"] as? Data
      else { return nil }
      return Stored(id: id, name: name, bookmark: bookmark)
    }
    var changed = false
    for i in stored.indices {
      if let fresh = resolve(index: i) {
        stored[i].bookmark = fresh
        changed = true
      }
    }
    if changed { save() }
  }

  /// Resolves stored[index] and holds access. Returns new bookmark data if the old one was stale.
  private func resolve(index: Int) -> Data? {
    let entry = stored[index]
    var stale = false
    guard
      let url = try? URL(
        resolvingBookmarkData: entry.bookmark, options: [], relativeTo: nil, bookmarkDataIsStale: &stale)
    else { return nil }  // Unresolvable (deleted, unplugged drive): the root shows as unavailable.
    let scoped = url.startAccessingSecurityScopedResource()
    held[entry.id] = Held(url: url, scoped: scoped)
    guard stale else { return nil }
    return try? url.bookmarkData(options: [], includingResourceValuesForKeys: nil, relativeTo: nil)
  }

  private func save() {
    let raw: [[String: Any]] = stored.map { ["id": $0.id, "name": $0.name, "bookmark": $0.bookmark] }
    UserDefaults.standard.set(raw, forKey: Self.defaultsKey)
  }

  private var documentsURL: URL {
    FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
  }

  private func root(for entry: Stored) -> StorageRoot {
    var available = false
    if let h = held[entry.id] {
      available = (try? h.url.checkResourceIsReachable()) ?? false
    }
    return StorageRoot(id: entry.id, displayName: entry.name, kind: .pickedFolder, available: available)
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
    completion(Result { try add(url: url, scoped: scoped) })
  }

  public func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
    pendingPick?(.success(nil))
    pendingPick = nil
  }

  func listRoots(completion: @escaping (Result<[StorageRoot], Error>) -> Void) {
    let docs = StorageRoot(
      id: Self.documentsId, displayName: "neuPod", kind: .appDocuments, available: true)
    completion(.success([docs] + stored.map(root(for:))))
  }

  func removeRoot(rootId: String) throws {
    if rootId == Self.documentsId {
      throw PigeonError(code: "not-removable", message: "the Documents folder is always a root", details: nil)
    }
    if let h = held.removeValue(forKey: rootId), h.scoped {
      h.url.stopAccessingSecurityScopedResource()
    }
    stored.removeAll { $0.id == rootId }
    save()
  }

  func startWalk(rootId: String) throws -> String {
    guard let root = url(forRoot: rootId), (try? root.checkResourceIsReachable()) == true else {
      throw PigeonError(code: "root-unavailable", message: "no access to \(rootId)", details: nil)
    }
    guard let walk = Walk(root: root) else {
      throw PigeonError(code: "root-unavailable", message: "cannot list \(rootId)", details: nil)
    }
    let id = UUID().uuidString
    walks[id] = walk
    return id
  }

  func nextWalkBatch(walkId: String, maxEntries: Int64, completion: @escaping (Result<WalkBatch, Error>) -> Void) {
    guard let walk = walks[walkId] else {
      completion(.failure(PigeonError(code: "no-walk", message: "unknown or finished walk \(walkId)", details: nil)))
      return
    }
    let limit = max(1, Int(maxEntries))
    walkQueue.async {
      let batch = walk.next(limit: limit)
      DispatchQueue.main.async {
        if batch.done { self.walks.removeValue(forKey: walkId) }
        completion(.success(batch))
      }
    }
  }

  func cancelWalk(walkId: String) throws {
    walks.removeValue(forKey: walkId)
  }

  func openFd(uri: String) throws -> Int64 {
    throw PigeonError(code: "unsupported", message: "iOS reads file:// URIs directly", details: nil)
  }

  func debugAddRoot(path: String) throws -> StorageRoot {
    #if DEBUG
      let url = URL(fileURLWithPath: path, isDirectory: true)
      let scoped = url.startAccessingSecurityScopedResource()
      return try add(url: url, scoped: scoped)
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

/// A pull-based recursive listing of one root. Used only on the walk queue.
final class Walk {
  private static let keys: [URLResourceKey] = [
    .isDirectoryKey, .isRegularFileKey, .fileSizeKey, .contentModificationDateKey,
    .ubiquitousItemDownloadingStatusKey,
  ]
  private static let icloudSuffix = ".icloud"

  private let root: URL
  private let enumerator: FileManager.DirectoryEnumerator
  /// Path components of the current item relative to the root, kept from `enumerator.level`
  /// so that symlinked prefixes (/var vs /private/var) never matter.
  private var stack: [String] = []

  init?(root: URL) {
    self.root = root
    // Hidden files are not skipped by the enumerator, so iCloud placeholders
    // (".Name.ext.icloud") are seen; other hidden names are skipped below.
    guard
      let e = FileManager.default.enumerator(
        at: root, includingPropertiesForKeys: Self.keys, options: [], errorHandler: { _, _ in true })
    else { return nil }
    enumerator = e
  }

  func next(limit: Int) -> WalkBatch {
    var out: [StorageEntry] = []
    var done = false
    // Coordinated metadata read of the tree, as Apple asks for iCloud folders.
    let coordinator = NSFileCoordinator(filePresenter: nil)
    var coordError: NSError?
    coordinator.coordinate(readingItemAt: root, options: [.immediatelyAvailableMetadataOnly], error: &coordError) { _ in
      while out.count < limit {
        guard let url = enumerator.nextObject() as? URL else {
          done = true
          return
        }
        let level = enumerator.level
        let name = url.lastPathComponent
        stack.removeLast(max(0, stack.count - (level - 1)))
        stack.append(name)
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
    if coordError != nil && out.isEmpty { done = true }
    return WalkBatch(entries: out, done: done)
  }
}
