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
      MPNowPlayingInfoPropertyPlaybackRate: engine.isPlaying ? 1.0 : 0.0,
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
      command.addTarget { [weak self] _ in
        MainActor.assumeIsolated {
          guard let engine = self?.engine else { return .noActionableNowPlayingItem }
          action(engine)
          return .success
        }
      }
    }
    handle(c.playCommand) { $0.play() }
    handle(c.pauseCommand) { $0.pause() }
    // The headset's single press.
    handle(c.togglePlayPauseCommand) { $0.isPlaying ? $0.pause() : $0.play() }
    handle(c.nextTrackCommand) { $0.skipToNext() }
    handle(c.previousTrackCommand) { $0.skipToPrevious() }
    // Lock-screen scrubbing.
    c.changePlaybackPositionCommand.isEnabled = true
    c.changePlaybackPositionCommand.addTarget { [weak self] event in
      MainActor.assumeIsolated {
        guard let engine = self?.engine,
          let event = event as? MPChangePlaybackPositionCommandEvent
        else { return .commandFailed }
        engine.seek(toMs: Int64(event.positionTime * 1000))
        return .success
      }
    }
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
    if let artUri = item.artUri, let url = URL(string: artUri), url.isFileURL,
      let image = UIImage(contentsOfFile: url.path)
    {
      artwork = makeArtwork(image)
      return
    }
    guard let url = URL(string: item.uri), url.isFileURL else { return }
    let id = item.id
    Task { [weak self] in
      let asset = AVURLAsset(url: url)
      guard let metadata = try? await asset.load(.commonMetadata),
        let entry = AVMetadataItem.metadataItems(
          from: metadata, filteredByIdentifier: .commonIdentifierArtwork
        ).first,
        let data = try? await entry.load(.dataValue),
        let image = UIImage(data: data)
      else { return }
      guard let self, self.artworkItemId == id else { return }
      self.artwork = makeArtwork(image)
      self.update()
    }
  }
}

private func makeArtwork(_ image: UIImage) -> MPMediaItemArtwork {
  MPMediaItemArtwork(boundsSize: image.size) { _ in image }
}

private func fileName(_ uri: String) -> String {
  URL(string: uri)?.deletingPathExtension().lastPathComponent ?? uri
}
