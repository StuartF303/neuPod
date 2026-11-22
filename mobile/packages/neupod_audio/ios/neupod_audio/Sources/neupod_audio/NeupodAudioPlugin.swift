// SPDX-License-Identifier: MPL-2.0

import Flutter
import UIKit

/// The Flutter side of the iOS engine: Pigeon's host API onto [AudioEngine],
/// with [NowPlaying] kept in step and every change sent to Dart.
public class NeupodAudioPlugin: NSObject, FlutterPlugin, AudioHostApi {
  private let engine: AudioEngine
  private let nowPlaying: NowPlaying
  private var flutterApi: AudioFlutterApi?

  @MainActor
  init(messenger: FlutterBinaryMessenger) {
    engine = AudioEngine()
    nowPlaying = NowPlaying(engine: engine)
    flutterApi = AudioFlutterApi(binaryMessenger: messenger)
    super.init()
    engine.onChange = { [weak self] in self?.changed() }
  }

  public static func register(with registrar: FlutterPluginRegistrar) {
    MainActor.assumeIsolated {
      let instance = NeupodAudioPlugin(messenger: registrar.messenger())
      AudioHostApiSetup.setUp(binaryMessenger: registrar.messenger(), api: instance)
      registrar.publish(instance)
    }
  }

  public func detachFromEngine(for registrar: FlutterPluginRegistrar) {
    AudioHostApiSetup.setUp(binaryMessenger: registrar.messenger(), api: nil)
    flutterApi = nil
    MainActor.assumeIsolated {
      nowPlaying.shutdown()
      engine.shutdown()
    }
  }

  @MainActor
  private func changed() {
    nowPlaying.update()
    guard let flutterApi else { return }
    let event = snapshot()
    Task { @MainActor in
      // No Dart listener yet (or any more): the next event catches up.
      try? await flutterApi.onEvent(event: event)
    }
  }

  @MainActor
  private func snapshot() -> EngineEvent {
    EngineEvent(
      queueId: engine.queueId,
      status: engine.status,
      playWhenReady: engine.playWhenReady,
      isPlaying: engine.isSounding,
      index: engine.index.map(Int64.init),
      itemId: engine.currentItem?.id,
      positionMs: engine.positionMs,
      durationMs: engine.durationMs,
      queueLength: Int64(engine.items.count),
      format: engine.format,
      errorMessage: engine.errorMessage)
  }

  // MARK: - AudioHostApi

  @MainActor
  func setQueue(
    queueId: String, items: [EngineItem], startIndex: Int64, startPositionMs: Int64
  ) async throws {
    engine.setQueue(
      items, queueId: queueId, startIndex: Int(startIndex), startPositionMs: startPositionMs)
  }

  @MainActor
  func insert(index: Int64, items: [EngineItem]) async throws {
    engine.insert(items, at: Int(index))
  }

  @MainActor
  func remove(index: Int64) async throws { engine.remove(at: Int(index)) }

  @MainActor
  func move(from: Int64, to: Int64) async throws { engine.move(from: Int(from), to: Int(to)) }

  @MainActor
  func clear() async throws { engine.clear() }

  @MainActor
  func play() async throws { engine.play() }

  @MainActor
  func pause() async throws { engine.pause() }

  @MainActor
  func seekTo(positionMs: Int64) async throws { engine.seek(toMs: positionMs) }

  @MainActor
  func skipTo(index: Int64) async throws { engine.skip(to: Int(index)) }

  @MainActor
  func skipToNext() async throws { engine.skipToNext() }

  @MainActor
  func skipToPrevious() async throws { engine.skipToPrevious() }

  @MainActor
  func getState() async throws -> EngineEvent { snapshot() }

  @MainActor
  func getSessionInfo() async throws -> SessionInfo { nowPlaying.sessionInfo() }
}
