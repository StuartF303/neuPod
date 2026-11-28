// SPDX-License-Identifier: MPL-2.0
import AVFoundation
import Flutter
import MediaPlayer
import UIKit
import os

/// Click-wheel haptics and the system media volume (docs/app/decisions/0007-haptics.md).
///
/// Haptics: one generator per kind, made with the view-attached initialisers of iOS 17.5 (the
/// plain `init(style:)` and `init()` are deprecated in the iOS 27 SDK). All of them are prepared
/// when the wheel is touched, and each again after it is used, so the first tick is not late. The
/// system decides whether to play them (the Haptics setting, the app in the foreground).
///
/// Volume: iOS has no public API to set the system volume. As owner decision 10 says, the wheel
/// sets it through a hidden MPVolumeView's slider, the common workaround, which Apple calls
/// unsupported. The step reads the volume back from AVAudioSession after the change has had a
/// moment to land and reports whether it moved (`VolumeStatus`), so the app knows when the
/// workaround fails and the owner's fallback can apply.
///
/// Every call is logged at debug level (subsystem net.stuartfraser.neupod, category device), so the
/// simulator, which has no haptics, still shows that the calls were made.
public class NeupodDevicePlugin: NSObject, FlutterPlugin, DeviceApi {
  private let log = Logger(subsystem: "net.stuartfraser.neupod", category: "device")

  private var selection: UISelectionFeedbackGenerator?
  private var rigid: UIImpactFeedbackGenerator?
  private var light: UIImpactFeedbackGenerator?
  private var medium: UIImpactFeedbackGenerator?
  private var soft: UIImpactFeedbackGenerator?
  private weak var generatorView: UIView?

  private var volumeView: MPVolumeView?

  /// iOS's volume buttons move the volume in 16 steps (community knowledge, not documented).
  private static let volumeSteps = 16

  public static func register(with registrar: FlutterPluginRegistrar) {
    let instance = NeupodDevicePlugin()
    DeviceApiSetup.setUp(binaryMessenger: registrar.messenger(), api: instance)
    // Reduce Transparency: Flutter does not expose it, so read it here (a plain method channel;
    // Custom mode paints every part solid while it is on).
    let channel = FlutterMethodChannel(
      name: "neupod_device/accessibility", binaryMessenger: registrar.messenger())
    instance.accessibilityChannel = channel
    channel.setMethodCallHandler { call, result in
      if call.method == "reduceTransparency" {
        result(UIAccessibility.isReduceTransparencyEnabled)
      } else {
        result(FlutterMethodNotImplemented)
      }
    }
    NotificationCenter.default.addObserver(
      instance, selector: #selector(reduceTransparencyChanged),
      name: UIAccessibility.reduceTransparencyStatusDidChangeNotification, object: nil)
    // Put the hidden volume view in place as soon as there is a window, well before the first
    // step: a freshly added MPVolumeView needs a moment before its slider works.
    NotificationCenter.default.addObserver(
      instance, selector: #selector(sceneActive),
      name: UIScene.didActivateNotification, object: nil)
  }

  public func detachFromEngine(for registrar: FlutterPluginRegistrar) {
    NotificationCenter.default.removeObserver(self)
    accessibilityChannel?.setMethodCallHandler(nil)
    accessibilityChannel = nil
    DeviceApiSetup.setUp(binaryMessenger: registrar.messenger(), api: nil)
  }

  private var accessibilityChannel: FlutterMethodChannel?

  @objc private func reduceTransparencyChanged() {
    accessibilityChannel?.invokeMethod(
      "reduceTransparencyChanged", arguments: UIAccessibility.isReduceTransparencyEnabled)
  }

  @objc private func sceneActive() {
    makeGenerators()
    _ = slider()
  }

  private func keyWindow() -> UIWindow? {
    UIApplication.shared.connectedScenes
      .compactMap { $0 as? UIWindowScene }
      .flatMap { $0.windows }
      .first { $0.isKeyWindow }
  }

  // ---- Haptics --------------------------------------------------------------------------------

  private func makeGenerators() {
    guard let view = keyWindow() else { return }
    if generatorView === view, selection != nil { return }
    generatorView = view
    selection = UISelectionFeedbackGenerator(view: view)
    rigid = UIImpactFeedbackGenerator(style: .rigid, view: view)
    light = UIImpactFeedbackGenerator(style: .light, view: view)
    medium = UIImpactFeedbackGenerator(style: .medium, view: view)
    soft = UIImpactFeedbackGenerator(style: .soft, view: view)
  }

  private func prepareAll() {
    selection?.prepare()
    rigid?.prepare()
    light?.prepare()
    medium?.prepare()
    soft?.prepare()
  }

  func prepareHaptics() throws {
    makeGenerators()
    prepareAll()
    _ = slider()
    log.debug("haptics prepared")
  }

  func playHaptic(event: HapticEvent) throws {
    makeGenerators()
    switch event {
    case .detent:
      selection?.selectionChanged()
      selection?.prepare()
      soft?.prepare()  // the end of the list may follow
    case .buttonDown:
      rigid?.impactOccurred(intensity: 0.8)
      rigid?.prepare()
      light?.prepare()  // the release follows
      medium?.prepare()  // or a hold
    case .buttonUp:
      light?.impactOccurred(intensity: 0.5)
      light?.prepare()
    case .holdTookEffect:
      medium?.impactOccurred()
      medium?.prepare()
    case .endReached:
      soft?.impactOccurred()
      soft?.prepare()
    }
    log.debug("haptic \(String(describing: event), privacy: .public)")
  }

  // ---- Volume ---------------------------------------------------------------------------------

  /// The hidden volume view: a 1x1 view at alpha 0.01 inside the window (it must be on screen and
  /// in the window for the slider to work and for the system HUD to stay away).
  private func slider() -> UISlider? {
    if volumeView == nil || volumeView?.window == nil {
      guard let window = keyWindow() else { return nil }
      volumeView?.removeFromSuperview()
      let v = MPVolumeView(frame: CGRect(x: 0, y: 0, width: 1, height: 1))
      v.alpha = 0.01
      v.isUserInteractionEnabled = false
      v.isAccessibilityElement = false
      v.accessibilityElementsHidden = true
      window.addSubview(v)
      volumeView = v
    }
    return volumeView?.subviews.compactMap { $0 as? UISlider }.first
  }

  private func state(level: Float, status: VolumeStatus) -> VolumeState {
    let n = Self.volumeSteps
    let step = Int((level * Float(n)).rounded())
    return VolumeState(
      level: Double(level), step: Int64(max(0, min(n, step))), steps: Int64(n), fixed: false,
      status: status)
  }

  func getVolume() throws -> VolumeState {
    _ = slider()  // in place before the first step
    return state(level: AVAudioSession.sharedInstance().outputVolume, status: .ok)
  }

  func stepVolume(direction: Int64) async throws -> VolumeState {
    let session = AVAudioSession.sharedInstance()
    let before = session.outputVolume
    let n = Float(Self.volumeSteps)
    let step = (before * n).rounded() + (direction > 0 ? 1 : direction < 0 ? -1 : 0)
    let target = max(0, min(1, step / n))
    // UIKit work on the main thread.
    let moved: Bool = await MainActor.run {
      guard let s = self.slider() else { return false }
      s.setValue(target, animated: false)
      s.sendActions(for: .valueChanged)
      return true
    }
    if !moved {
      log.error("volume: no slider in MPVolumeView")
      return state(level: before, status: .noSlider)
    }
    // Let the change land, then read the system's own value back.
    try? await Task.sleep(nanoseconds: 150_000_000)
    let after = session.outputVolume
    let changed = abs(after - before) > 0.001
    let wanted = abs(target - before) > 0.001
    let status: VolumeStatus = (changed || !wanted) ? .ok : .noEffect
    log.debug("volume step \(direction) \(before) -> \(after) (\(String(describing: status)))")
    return state(level: after, status: status)
  }
}
