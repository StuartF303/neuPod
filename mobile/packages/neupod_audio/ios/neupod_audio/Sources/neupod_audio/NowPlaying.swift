// SPDX-License-Identifier: MPL-2.0

import AVFoundation
import MediaPlayer
import UIKit

/// The lock screen, Control Centre and headset buttons (decision 0002):
/// MPNowPlayingInfoCenter shows the current item and MPRemoteCommandCenter
/// sends its commands to the engine. Republished after every engine change,
/// including an engine restart, so the lock screen never goes stale.
@MainActor
final class NowPlaying {
  private let engine: AudioEngine
  private var artworkItemId: String?
  private var artwork: MPMediaItemArtwork?
  /// Every addTarget registration, removed again by [shutdown].
  private var targets: [(MPRemoteCommand, Any)] = []

  init(engine: AudioEngine) {
    self.engine = engine
    registerCommands()
  }

  func update() {
    let center = MPNowPlayingInfoCenter.default()
    let commands = MPRemoteCommandCenter.shared()
    guard let item = engine.currentItem, let index = engine.index else {
      center.nowPlayingInfo = nil
      commands.nextTrackCommand.isEnabled = false
      commands.previousTrackCommand.isEnabled = false
      return
    }
    loadArtwork(for: item)
    var info: [String: Any] = [
      MPMediaItemPropertyTitle: item.title ?? fileName(item.uri),
      MPNowPlayingInfoPropertyElapsedPlaybackTime: Double(engine.positionMs) / 1000,
      MPNowPlayingInfoPropertyPlaybackRate: engine.isSounding ? 1.0 : 0.0,
      MPNowPlayingInfoPropertyDefaultPlaybackRate: 1.0,
      MPNowPlayingInfoPropertyMediaType: MPNowPlayingInfoMediaType.audio.rawValue,
      MPNowPlayingInfoPropertyPlaybackQueueIndex: index,
      MPNowPlayingInfoPropertyPlaybackQueueCount: engine.items.count,
    ]
    if let artist = item.artist { info[MPMediaItemPropertyArtist] = artist }
    if let album = item.album { info[MPMediaItemPropertyAlbumTitle] = album }
    if let duration = engine.durationMs {
      info[MPMediaItemPropertyPlaybackDuration] = Double(duration) / 1000
    }
    if let artwork { info[MPMediaItemPropertyArtwork] = artwork }
    center.nowPlayingInfo = info
    commands.nextTrackCommand.isEnabled = index + 1 < engine.items.count
    commands.previousTrackCommand.isEnabled = true
    commands.changePlaybackPositionCommand.isEnabled = engine.durationMs != nil
  }

  /// Unregisters the remote commands and clears the lock screen (plugin detach).
  func shutdown() {
    for (command, target) in targets {
      command.removeTarget(target)
      command.isEnabled = false
    }
    targets = []
    MPNowPlayingInfoCenter.default().nowPlayingInfo = nil
  }

  /// What the system shows, read back from MPNowPlayingInfoCenter.
  func sessionInfo() -> SessionInfo {
    let info = MPNowPlayingInfoCenter.default().nowPlayingInfo ?? [:]
    let c = MPRemoteCommandCenter.shared()
    var commands: [String] = []
    if c.playCommand.isEnabled { commands.append("play") }
    if c.pauseCommand.isEnabled { commands.append("pause") }
    if c.nextTrackCommand.isEnabled { commands.append("next") }
    if c.previousTrackCommand.isEnabled { commands.append("previous") }
    if c.changePlaybackPositionCommand.isEnabled { commands.append("seek") }
    func ms(_ key: String) -> Int64? { (info[key] as? Double).map { Int64($0 * 1000) } }
    return SessionInfo(
      title: info[MPMediaItemPropertyTitle] as? String,
      artist: info[MPMediaItemPropertyArtist] as? String,
      album: info[MPMediaItemPropertyAlbumTitle] as? String,
      durationMs: ms(MPMediaItemPropertyPlaybackDuration),
      positionMs: ms(MPNowPlayingInfoPropertyElapsedPlaybackTime),
      playbackRate: info[MPNowPlayingInfoPropertyPlaybackRate] as? Double,
      hasArtwork: info[MPMediaItemPropertyArtwork] != nil,
      commands: commands)
  }

  private func registerCommands() {
    let c = MPRemoteCommandCenter.shared()
    func handle(
      _ command: MPRemoteCommand, _ action: @escaping @MainActor (AudioEngine) -> Void
    ) {
      command.isEnabled = true
      let target = command.addTarget { [weak self] _ in
        MainActor.assumeIsolated {
          guard let engine = self?.engine else { return .noActionableNowPlayingItem }
          action(engine)
          return .success
        }
      }
      targets.append((command, target))
    }
    handle(c.playCommand) { $0.play() }
    handle(c.pauseCommand) { $0.pause() }
    // The headset's single press.
    handle(c.togglePlayPauseCommand) { $0.isPlaying ? $0.pause() : $0.play() }
    handle(c.nextTrackCommand) { $0.skipToNext() }
    handle(c.previousTrackCommand) { $0.skipToPrevious() }
    // Lock-screen scrubbing.
    c.changePlaybackPositionCommand.isEnabled = true
    let seekTarget = c.changePlaybackPositionCommand.addTarget { [weak self] event in
      MainActor.assumeIsolated {
        guard let engine = self?.engine,
          let event = event as? MPChangePlaybackPositionCommandEvent
        else { return .commandFailed }
        engine.seek(toMs: Int64(event.positionTime * 1000))
        return .success
      }
    }
    targets.append((c.changePlaybackPositionCommand, seekTarget))
    // The device has no skip-by-interval buttons; previous and next stay.
    c.skipForwardCommand.isEnabled = false
    c.skipBackwardCommand.isEnabled = false
    c.seekForwardCommand.isEnabled = false
    c.seekBackwardCommand.isEnabled = false
  }

  /// Art from the item's artUri (the app's art cache), else embedded in the
  /// file, loaded once per item.
  private func loadArtwork(for item: EngineItem) {
    guard artworkItemId != item.id else { return }
    artworkItemId = item.id
    artwork = nil
    let id = item.id
    let artURL = item.artUri.flatMap(URL.init(string:)).flatMap { $0.isFileURL ? $0 : nil }
    // Embedded art only from a file on the phone (owner decision 7).
    let fileURL = URL(string: item.uri).flatMap { $0.isFileURL && LocalFile.isOnDevice($0) ? $0 : nil }
    // Decoding the image (and reading the file's metadata) stays off the
    // main thread; the result is applied only if the item is still current.
    Task.detached(priority: .utility) { [weak self] in
      var image: UIImage? = nil
      if let artURL { image = UIImage(contentsOfFile: artURL.path) }
      if image == nil, let fileURL {
        let asset = AVURLAsset(url: fileURL)
        if let metadata = try? await asset.load(.commonMetadata),
          let entry = AVMetadataItem.metadataItems(
            from: metadata, filteredByIdentifier: .commonIdentifierArtwork
          ).first,
          let data = try? await entry.load(.dataValue)
        {
          image = UIImage(data: data)
        }
      }
      guard let image else { return }
      let artwork = makeArtwork(image)
      await MainActor.run {
        guard let self, self.artworkItemId == id else { return }
        self.artwork = artwork
        self.update()
      }
    }
  }
}

private func makeArtwork(_ image: UIImage) -> MPMediaItemArtwork {
  MPMediaItemArtwork(boundsSize: image.size) { _ in image }
}

private func fileName(_ uri: String) -> String {
  URL(string: uri)?.deletingPathExtension().lastPathComponent ?? uri
}
