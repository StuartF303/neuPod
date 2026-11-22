// SPDX-License-Identifier: MPL-2.0

import AVFoundation
import QuartzCore

/// The iOS player (decision 0001): one AVAudioEngine with one AVAudioPlayerNode.
///
/// The current track and the next are scheduled back to back on the node as
/// segments of their AVAudioFiles, so the join between them is sample-accurate
/// when they share a processing format (sample rate and channel count). When
/// the next track's format differs, it is not pre-scheduled: the engine is
/// reconnected at the join, which can leave a short gap (decision 0001, mixed
/// sample rates; measured in phase 4).
///
/// Ogg Opus and Vorbis are not opened here yet: they need the bundled Xiph
/// decoders (owner decision 5, phase 4), which will feed PCM buffers to the
/// same node through `scheduleBuffer`. Today such a file fails to open and is
/// reported as unplayable.
///
/// All state is main-actor isolated; the node's completion callbacks hop back
/// to the main actor and are matched by `generation` so callbacks from
/// segments flushed by `stop()` are ignored.
@MainActor
final class AudioEngine {
  /// Called after every change of state, on the main actor.
  var onChange: (() -> Void)?

  private(set) var items: [EngineItem] = []
  private(set) var status: EngineStatus = .idle
  private(set) var playWhenReady = false
  private(set) var errorMessage: String?
  private(set) var currentFile: AVAudioFile?

  /// The id of the setQueue call that made the queue; null after clear().
  private(set) var queueId: String?

  /// The id of the current queue entry (ids are unique within a queue).
  private var currentId: String?

  private let engine = AVAudioEngine()
  private let node = AVAudioPlayerNode()
  private var connectedFormat: AVAudioFormat?

  /// A scheduled stretch of one file, in the node's timeline.
  private struct Segment {
    let id: String
    let file: AVAudioFile
    let startFrame: AVAudioFramePosition
    let frameCount: AVAudioFrameCount
    let timelineStart: AVAudioFramePosition
  }

  /// The current segment first, then the pre-scheduled next one, if any.
  private var scheduled: [Segment] = []
  private var timelineEnd: AVAudioFramePosition = 0
  private var generation = 0
  /// Position reported while the node is not rendering (paused, loading).
  private var heldPositionMs: Int64 = 0

  /// Where playback was (ms) when rendering last started, and when
  /// (CACurrentMediaTime). Once the system stops the engine (a route or
  /// format change) the node has no render time, so the position is
  /// extrapolated from here instead of falling back to [heldPositionMs].
  private var anchorMs: Int64 = 0
  private var anchorTime: CFTimeInterval?

  /// The node's schedule no longer matches the engine (it was stopped by
  /// the system or an interruption): play must reload from
  /// [heldPositionMs] rather than just resume the node.
  private var needsReload = false

  /// The outputs in use when rendering started or the route last changed.
  private var lastOutputs: Set<AVAudioSession.Port> = []

  private var observers: [NSObjectProtocol] = []

  /// Outputs whose disappearance means "unplugged": pause, never carry on
  /// out of the speaker.
  private static let personalOutputs: Set<AVAudioSession.Port> = [
    .headphones, .bluetoothA2DP, .bluetoothLE, .bluetoothHFP, .usbAudio, .airPlay, .carAudio,
  ]

  init() {
    engine.attach(node)
    let center = NotificationCenter.default
    observers.append(
      center.addObserver(
        forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main
      ) { [weak self] _ in
        MainActor.assumeIsolated { self?.engineConfigurationChanged() }
      })
    let session = AVAudioSession.sharedInstance()
    observers.append(
      center.addObserver(
        forName: AVAudioSession.interruptionNotification, object: session, queue: .main
      ) { [weak self] note in
        let began =
          (note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt)
          .flatMap(AVAudioSession.InterruptionType.init(rawValue:)) == .began
        MainActor.assumeIsolated { if began { self?.interrupted() } }
      })
    observers.append(
      center.addObserver(
        forName: AVAudioSession.routeChangeNotification, object: session, queue: .main
      ) { [weak self] note in
        let reason =
          (note.userInfo?[AVAudioSessionRouteChangeReasonKey] as? UInt)
          .flatMap(AVAudioSession.RouteChangeReason.init(rawValue:))
        let previous =
          (note.userInfo?[AVAudioSessionRouteChangePreviousRouteKey]
          as? AVAudioSessionRouteDescription)
          .map { Set($0.outputs.map(\.portType)) } ?? []
        MainActor.assumeIsolated { self?.routeChanged(reason: reason, previous: previous) }
      })
  }

  /// Stops playback and lets go of every system hook (plugin detach).
  func shutdown() {
    for observer in observers { NotificationCenter.default.removeObserver(observer) }
    observers = []
    onChange = nil
    flush()
    engine.stop()
    deactivateSession()
    playWhenReady = false
    needsReload = false
    anchorTime = nil
    status = .idle
  }

  // MARK: - Queries

  var index: Int? { currentId.flatMap { id in items.firstIndex { $0.id == id } } }

  var currentItem: EngineItem? { index.map { items[$0] } }

  var durationMs: Int64? {
    if let file = currentFile {
      return Int64(Double(file.length) * 1000 / file.processingFormat.sampleRate)
    }
    return currentItem?.durationMs
  }

  var isPlaying: Bool { playWhenReady && status == .ready }

  /// Sound is coming out now (not only asked for).
  var isSounding: Bool { isPlaying && !needsReload && engine.isRunning && node.isPlaying }

  var positionMs: Int64 {
    guard isPlaying else { return heldPositionMs }
    if engine.isRunning, node.isPlaying, let segment = scheduled.first,
      let renderTime = node.lastRenderTime,
      let playerTime = node.playerTime(forNodeTime: renderTime)
    {
      let played = min(
        max(playerTime.sampleTime - segment.timelineStart, 0),
        AVAudioFramePosition(segment.frameCount))
      let frame = segment.startFrame + played
      return Int64(Double(frame) * 1000 / segment.file.processingFormat.sampleRate)
    }
    if let anchorTime {
      let ms = anchorMs + Int64((CACurrentMediaTime() - anchorTime) * 1000)
      return min(ms, durationMs ?? ms)
    }
    return heldPositionMs
  }

  var format: EngineFormat? {
    guard let file = currentFile else { return nil }
    let description = file.fileFormat.streamDescription.pointee
    let codec: String
    var bits: Int64? = nil
    switch description.mFormatID {
    case kAudioFormatFLAC, kAudioFormatAppleLossless:
      codec = description.mFormatID == kAudioFormatFLAC ? "flac" : "alac"
      // Both formats carry the source bit depth in the flags (kAppleLosslessFormatFlag_*).
      bits = [1: 16, 2: 20, 3: 24, 4: 32][description.mFormatFlags]
    case kAudioFormatMPEGLayer3: codec = "mp3"
    case kAudioFormatMPEG4AAC: codec = "aac"
    case kAudioFormatLinearPCM:
      codec = "pcm"
      bits = Int64(description.mBitsPerChannel)
    default: codec = fourCC(description.mFormatID)
    }
    return EngineFormat(
      codec: codec,
      sampleRate: Int64(file.fileFormat.sampleRate),
      channelCount: Int64(file.fileFormat.channelCount),
      bitsPerSample: bits)
  }

  // MARK: - Queue

  func setQueue(_ newItems: [EngineItem], queueId: String, startIndex: Int, startPositionMs: Int64) {
    items = newItems
    guard !items.isEmpty else {
      clear()
      self.queueId = queueId
      notify()
      return
    }
    self.queueId = queueId
    load(min(max(startIndex, 0), items.count - 1), positionMs: startPositionMs)
  }

  func insert(_ newItems: [EngineItem], at index: Int) {
    items.insert(contentsOf: newItems, at: min(max(index, 0), items.count))
    if currentId == nil, !items.isEmpty {
      load(0, positionMs: 0)
    } else {
      queueChanged()
    }
  }

  func remove(at index: Int) {
    guard items.indices.contains(index) else { return }
    let removingCurrent = items[index].id == currentId
    let oldIndex = self.index
    items.remove(at: index)
    if removingCurrent, let oldIndex {
      if oldIndex < items.count {
        load(oldIndex, positionMs: 0)
      } else if let last = items.last {
        // The current track was the last: end on the new last track.
        currentId = last.id
        currentFile = nil
        clearPlayback(status: .ended)
      } else {
        clearPlayback(status: .idle)
      }
    } else {
      queueChanged()
    }
  }

  func move(from: Int, to: Int) {
    guard items.indices.contains(from), items.indices.contains(to) else { return }
    items.insert(items.remove(at: from), at: to)
    queueChanged()
  }

  func clear() {
    items = []
    queueId = nil
    clearPlayback(status: .idle)
  }

  // MARK: - Transport

  func play() {
    guard let index else { return }
    playWhenReady = true
    switch status {
    case .ended, .idle:
      // After the end, play starts the last track again; after an error it retries.
      load(index, positionMs: status == .ended ? 0 : heldPositionMs)
    case .ready where needsReload:
      // The engine was stopped under the node: reschedule from where it was.
      load(index, positionMs: heldPositionMs)
    case .ready:
      startRendering()
      notify()
    case .loading:
      notify()
    }
  }

  func pause() {
    guard playWhenReady else { return }
    heldPositionMs = positionMs
    playWhenReady = false
    anchorTime = nil
    node.pause()
    engine.pause()
    notify()
  }

  func seek(toMs positionMs: Int64) {
    guard let index else { return }
    load(index, positionMs: positionMs)
  }

  func skip(to index: Int) {
    guard items.indices.contains(index) else { return }
    load(index, positionMs: 0)
  }

  func skipToNext() {
    guard let index, index + 1 < items.count else { return }
    load(index + 1, positionMs: 0)
  }

  /// The skip-back rule: after three seconds restart the track, otherwise go
  /// to the previous one (the first track restarts).
  func skipToPrevious() {
    guard let index else { return }
    if positionMs > 3000 || index == 0 {
      load(index, positionMs: 0)
    } else {
      load(index - 1, positionMs: 0)
    }
  }

  // MARK: - Scheduling

  /// Opens items[index], schedules it from positionMs and pre-schedules the
  /// next item, then renders if the user asked to play.
  private func load(_ index: Int, positionMs: Int64) {
    flush()
    let item = items[index]
    currentId = item.id
    errorMessage = nil
    needsReload = false
    anchorTime = nil
    heldPositionMs = max(positionMs, 0)
    let file: AVAudioFile
    do {
      file = try open(item)
    } catch {
      currentFile = nil
      status = .idle
      errorMessage = error.localizedDescription
      notify()
      return
    }
    currentFile = file
    connect(file.processingFormat)
    let rate = file.processingFormat.sampleRate
    let start = min(AVAudioFramePosition(Double(heldPositionMs) * rate / 1000), file.length)
    heldPositionMs = Int64(Double(start) * 1000 / rate)
    schedule(id: item.id, file: file, from: start)
    scheduleNext()
    status = .ready
    if playWhenReady { startRendering() }
    notify()
  }

  private func open(_ item: EngineItem) throws -> AVAudioFile {
    guard let url = URL(string: item.uri), url.isFileURL else {
      throw NSError(
        domain: "neupod_audio", code: 1,
        userInfo: [NSLocalizedDescriptionKey: "Not a local file URL: \(item.uri)"])
    }
    // The root is kept accessible by neupod_storage (decision 0004); the audio
    // engine never starts or stops security-scoped access itself.
    return try AVAudioFile(forReading: url)
  }

  private func schedule(id: String, file: AVAudioFile, from start: AVAudioFramePosition) {
    let count = AVAudioFrameCount(clamping: max(file.length - start, 0))
    let segment = Segment(
      id: id, file: file, startFrame: start, frameCount: count, timelineStart: timelineEnd)
    timelineEnd += AVAudioFramePosition(count)
    scheduled.append(segment)
    let generation = self.generation
    if count == 0 {
      // Seeked to the very end: finish at once, on the next turn of the run loop.
      DispatchQueue.main.async { [weak self] in
        MainActor.assumeIsolated { self?.segmentFinished(generation: generation, id: id) }
      }
      return
    }
    node.scheduleSegment(
      file, startingFrame: start, frameCount: count, at: nil,
      completionCallbackType: .dataPlayedBack
    ) { [weak self] _ in
      DispatchQueue.main.async {
        MainActor.assumeIsolated { self?.segmentFinished(generation: generation, id: id) }
      }
    }
  }

  /// Pre-schedules the item after the current one when it can join gaplessly.
  private func scheduleNext() {
    guard scheduled.count == 1, let index, index + 1 < items.count,
      let connectedFormat
    else { return }
    let next = items[index + 1]
    guard let file = try? open(next) else { return }  // reported when it becomes current
    guard sameStream(file.processingFormat, connectedFormat) else { return }
    schedule(id: next.id, file: file, from: 0)
  }

  private func segmentFinished(generation: Int, id: String) {
    guard generation == self.generation, !needsReload, scheduled.first?.id == id else { return }
    scheduled.removeFirst()
    if let next = scheduled.first, items.contains(where: { $0.id == next.id }) {
      // The pre-scheduled next track is already playing: a gapless join.
      currentId = next.id
      currentFile = next.file
      heldPositionMs = 0
      setAnchor(0)
      scheduleNext()
      notify()
    } else if let index, index + 1 < items.count {
      // Not pre-scheduled (a different format, or the queue changed): load it.
      load(index + 1, positionMs: 0)
    } else {
      heldPositionMs = durationMs ?? heldPositionMs
      anchorTime = nil
      status = .ended
      node.stop()
      engine.stop()
      deactivateSession()
      notify()
    }
  }

  /// Keeps the pre-scheduled next segment in step with an edited queue. A
  /// scheduled segment cannot be withdrawn on its own, so if the wrong one is
  /// queued, the current track is rescheduled from where it is.
  private func queueChanged() {
    guard let index else {
      notify()
      return
    }
    let wanted = index + 1 < items.count ? items[index + 1].id : nil
    let queued = scheduled.count > 1 ? scheduled[1].id : nil
    if queued != nil && queued != wanted {
      load(index, positionMs: positionMs)
    } else {
      if queued == nil { scheduleNext() }
      notify()
    }
  }

  private func flush() {
    generation += 1
    node.stop()
    scheduled = []
    timelineEnd = 0
  }

  private func clearPlayback(status: EngineStatus) {
    flush()
    engine.stop()
    deactivateSession()
    anchorTime = nil
    needsReload = false
    currentId = status == .idle ? nil : currentId
    currentFile = status == .idle ? nil : currentFile
    heldPositionMs = status == .idle ? 0 : (durationMs ?? 0)
    errorMessage = nil
    self.status = status
    if status == .idle { playWhenReady = false }
    notify()
  }

  private func connect(_ format: AVAudioFormat) {
    if let connectedFormat, sameStream(connectedFormat, format) { return }
    engine.stop()
    engine.disconnectNodeOutput(node)
    engine.connect(node, to: engine.mainMixerNode, format: format)
    connectedFormat = format
    // A hint only (QA1631): ask the hardware for the file's rate so the
    // system need not resample. What was achieved is reported in phase 4.
    try? AVAudioSession.sharedInstance().setPreferredSampleRate(format.sampleRate)
  }

  private func sameStream(_ a: AVAudioFormat, _ b: AVAudioFormat) -> Bool {
    a.sampleRate == b.sampleRate && a.channelCount == b.channelCount
      && a.commonFormat == b.commonFormat && a.isInterleaved == b.isInterleaved
  }

  private func startRendering() {
    do {
      try activateSession()
      if !engine.isRunning {
        engine.prepare()
        try engine.start()
      }
      node.play()
      setAnchor(heldPositionMs)
      lastOutputs = Set(AVAudioSession.sharedInstance().currentRoute.outputs.map(\.portType))
    } catch {
      playWhenReady = false
      errorMessage = "Audio output unavailable: \(error.localizedDescription)"
    }
  }

  private var sessionConfigured = false

  private func activateSession() throws {
    let session = AVAudioSession.sharedInstance()
    if !sessionConfigured {
      // Long-form audio: background playback, AirPlay 2 routing (decision 0002).
      try session.setCategory(.playback, mode: .default, policy: .longFormAudio)
      sessionConfigured = true
    }
    try session.setActive(true)
  }

  /// Lets other apps' audio resume once the queue is cleared or has ended.
  private func deactivateSession() {
    try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
  }

  private func setAnchor(_ ms: Int64) {
    anchorMs = ms
    anchorTime = CACurrentMediaTime()
  }

  // MARK: - System events

  /// The engine stops itself when the route or the hardware format changes;
  /// reschedule from where it was (decision 0001, risks).
  ///
  /// Unplugging headphones posts both a route change and a configuration
  /// change, in either order. So: if a personal output (headphones,
  /// Bluetooth, USB, AirPlay, car) in use has gone and none has taken over,
  /// pause; otherwise restart one run-loop turn later, unless the route
  /// change paused meanwhile. Both handlers use the same test
  /// ([unplugged(lost:now:)]), so the order does not change the outcome as
  /// long as `currentRoute` already shows the new route when the first one
  /// runs.
  private func engineConfigurationChanged() {
    guard status == .ready, index != nil else { return }
    heldPositionMs = positionMs
    anchorTime = nil
    connectedFormat = nil
    invalidateSchedule()
    let now = Set(AVAudioSession.sharedInstance().currentRoute.outputs.map(\.portType))
    let lost = lastOutputs.subtracting(now)
    lastOutputs = now
    if playWhenReady, Self.unplugged(lost: lost, now: now) {
      playWhenReady = false
      notify()
      return
    }
    notify()
    DispatchQueue.main.async { [weak self] in
      MainActor.assumeIsolated {
        guard let self, self.needsReload, self.playWhenReady, self.status == .ready,
          let index = self.index
        else { return }
        self.load(index, positionMs: self.heldPositionMs)
      }
    }
  }

  /// Headphones unplugged (a personal output went away): pause, as Android
  /// does on becoming noisy. Other route changes need nothing here; the
  /// engine's configuration change handles a restart.
  private func routeChanged(reason: AVAudioSession.RouteChangeReason?, previous: Set<AVAudioSession.Port>) {
    let now = Set(AVAudioSession.sharedInstance().currentRoute.outputs.map(\.portType))
    defer { lastOutputs = now }
    guard reason == .oldDeviceUnavailable, playWhenReady,
      Self.unplugged(lost: previous.subtracting(now), now: now)
    else { return }
    heldPositionMs = positionMs
    anchorTime = nil
    playWhenReady = false
    invalidateSchedule()
    node.pause()
    if engine.isRunning { engine.pause() }
    notify()
  }

  /// A call or another app's audio. The system has stopped the engine, so
  /// the next play reloads from here. Resuming on its own when the
  /// interruption ends is phase 4.
  private func interrupted() {
    // Even when paused: the system stops the engine either way.
    heldPositionMs = positionMs
    anchorTime = nil
    playWhenReady = false
    invalidateSchedule()
    node.pause()
    notify()
  }

  /// A personal output went away and no personal output took over: the
  /// sound would otherwise move to the phone's speaker.
  private static func unplugged(lost: Set<AVAudioSession.Port>, now: Set<AVAudioSession.Port>)
    -> Bool
  {
    !lost.isDisjoint(with: personalOutputs) && now.isDisjoint(with: personalOutputs)
  }

  /// The node's schedule is no longer trusted (the system stopped the
  /// engine): the next play reloads, and completion callbacks the stop fires
  /// for the old segments are ignored, so they cannot advance or end the
  /// queue.
  private func invalidateSchedule() {
    needsReload = true
    generation += 1
  }

  private func notify() { onChange?() }
}

private func fourCC(_ code: AudioFormatID) -> String {
  let bytes = [24, 16, 8, 0].map { UInt8((code >> $0) & 0xff) }
  return String(bytes: bytes, encoding: .ascii)?.trimmingCharacters(in: .whitespaces) ?? "\(code)"
}
