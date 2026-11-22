// SPDX-License-Identifier: MPL-2.0

import AVFoundation

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

  init() {
    engine.attach(node)
    NotificationCenter.default.addObserver(
      forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main
    ) { [weak self] _ in
      MainActor.assumeIsolated { self?.engineConfigurationChanged() }
    }
    let session = AVAudioSession.sharedInstance()
    NotificationCenter.default.addObserver(
      forName: AVAudioSession.interruptionNotification, object: session, queue: .main
    ) { [weak self] note in
      let began =
        (note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt)
        .flatMap(AVAudioSession.InterruptionType.init(rawValue:)) == .began
      MainActor.assumeIsolated { if began { self?.interrupted() } }
    }
    NotificationCenter.default.addObserver(
      forName: AVAudioSession.routeChangeNotification, object: session, queue: .main
    ) { [weak self] note in
      let reason =
        (note.userInfo?[AVAudioSessionRouteChangeReasonKey] as? UInt)
        .flatMap(AVAudioSession.RouteChangeReason.init(rawValue:))
      MainActor.assumeIsolated {
        // Headphones unplugged (the old device went away): pause, as on Android.
        if reason == .oldDeviceUnavailable { self?.pause() }
      }
    }
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

  var positionMs: Int64 {
    guard isPlaying, node.isPlaying, let segment = scheduled.first,
      let renderTime = node.lastRenderTime,
      let playerTime = node.playerTime(forNodeTime: renderTime)
    else { return heldPositionMs }
    let played = min(
      max(playerTime.sampleTime - segment.timelineStart, 0), AVAudioFramePosition(segment.frameCount))
    let frame = segment.startFrame + played
    return Int64(Double(frame) * 1000 / segment.file.processingFormat.sampleRate)
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

  func setQueue(_ newItems: [EngineItem], startIndex: Int, startPositionMs: Int64) {
    items = newItems
    guard !items.isEmpty else {
      clear()
      return
    }
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
      } else {
        clearPlayback(status: items.isEmpty ? .idle : .ended)
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
    guard generation == self.generation, scheduled.first?.id == id else { return }
    scheduled.removeFirst()
    if let next = scheduled.first, items.contains(where: { $0.id == next.id }) {
      // The pre-scheduled next track is already playing: a gapless join.
      currentId = next.id
      currentFile = next.file
      heldPositionMs = 0
      scheduleNext()
      notify()
    } else if let index, index + 1 < items.count {
      // Not pre-scheduled (a different format, or the queue changed): load it.
      load(index + 1, positionMs: 0)
    } else {
      heldPositionMs = durationMs ?? heldPositionMs
      status = .ended
      node.stop()
      engine.pause()
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

  // MARK: - System events

  /// The engine stops itself when the route or the hardware format changes;
  /// reschedule from where it was (decision 0001, risks).
  private func engineConfigurationChanged() {
    guard let index, status == .ready else { return }
    let position = positionMs
    connectedFormat = nil
    load(index, positionMs: position)
  }

  /// A call or another app's audio. Resuming after it ends is phase 4.
  private func interrupted() {
    guard playWhenReady else { return }
    heldPositionMs = positionMs
    playWhenReady = false
    node.pause()
    notify()
  }

  private func notify() { onChange?() }
}

private func fourCC(_ code: AudioFormatID) -> String {
  let bytes = [24, 16, 8, 0].map { UInt8((code >> $0) & 0xff) }
  return String(bytes: bytes, encoding: .ascii)?.trimmingCharacters(in: .whitespaces) ?? "\(code)"
}
