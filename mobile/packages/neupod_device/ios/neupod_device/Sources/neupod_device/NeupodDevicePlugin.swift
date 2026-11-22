// SPDX-License-Identifier: MPL-2.0
import AVFoundation
import Flutter
import MediaPlayer
import UIKit
import os

/// Click-wheel haptics and the system media volume (docs/app/decisions/0007-haptics.md).
///
/// Haptics: one generator per kind, made with the view-attached initialisers of iOS 17.5 (the
/// plain `init(style:)` and `init()` are deprecated in the iOS 27 SDK), prepared when the wheel
/// is touched and again after each use, so the first tick is not late. The system decides whether
/// to play them (the Haptics setting, the app in the foreground).
///
/// Volume: iOS has no public API to set the system volume. As owner decision 10 says, the wheel
/// sets it through a hidden MPVolumeView's slider, the common workaround. Apple calls this
/// unsupported; if it stops working the fallback is the owner's to choose.
///
/// Every call is logged (subsystem net.stuartfraser.neupod, category device), so the simulator,
/// which has no haptics, still shows that the calls were made.
public class NeupodDevicePlugin: NSObject, FlutterPlugin, DeviceApi {
  private let log = Logger(subsystem: "net.stuartfraser.neupod", category: "device")

  private var selection: UISelectionFeedbackGenerator?
  private var rigid: UIImpactFeedbackGenerator?
  private var light: UIImpactFeedbackGenerator?
  private var medium: UIImpactFeedbackGenerator?
  private var soft: UIImpactFeedbackGenerator?
  private weak var generatorView: UIView?

  private var volumeView: MPVolumeView?

  /// iOS's volume buttons move the volume in 16 steps.
  private static let volumeSteps = 16

  public static func register(with registrar: FlutterPluginRegistrar) {
    let instance = NeupodDevicePlugin()
    DeviceApiSetup.setUp(binaryMessenger: registrar.messenger(), api: instance)
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

  func prepareHaptics() throws {
    makeGenerators()
    selection?.prepare()
    rigid?.prepare()
    light?.prepare()
    log.debug("haptics prepared")
  }

  func playHaptic(event: HapticEvent) throws {
    makeGenerators()
    switch event {
    case .detent:
      selection?.selectionChanged()
      selection?.prepare()
    case .buttonDown:
      rigid?.impactOccurred(intensity: 0.8)
      rigid?.prepare()
      light?.prepare()  // the release follows
    case .buttonUp:
      light?.impactOccurred(intensity: 0.5)
      light?.prepare()
    case .holdTookEffect:
      medium?.impactOccurred()
    case .endReached:
      soft?.impactOccurred()
    }
    log.debug("haptic \(String(describing: event), privacy: .public)")
  }

  // ---- Volume ---------------------------------------------------------------------------------

  /// The hidden volume view: in the window (it must be, for the slider to work and for the
  /// system HUD to stay away), off screen and nearly transparent.
  private func slider() -> UISlider? {
    if volumeView == nil || volumeView?.window == nil {
      guard let window = keyWindow() else { return nil }
      let v = MPVolumeView(frame: CGRect(x: -2000, y: -2000, width: 1, height: 1))
      v.alpha = 0.01
      v.isUserInteractionEnabled = false
      v.isAccessibilityElement = false
      v.accessibilityElementsHidden = true
      window.addSubview(v)
      volumeView = v
    }
    return volumeView?.subviews.compactMap { $0 as? UISlider }.first
  }

  private func state(level: Float) -> VolumeState {
    let n = Self.volumeSteps
    let step = Int((level * Float(n)).rounded())
    return VolumeState(
      level: Double(level), step: Int64(max(0, min(n, step))), steps: Int64(n), fixed: false)
  }

  func getVolume() throws -> VolumeState {
    _ = slider()  // in place before the first step
    return state(level: AVAudioSession.sharedInstance().outputVolume)
  }

  func stepVolume(direction: Int64) throws -> VolumeState {
    let n = Float(Self.volumeSteps)
    let now = slider()?.value ?? AVAudioSession.sharedInstance().outputVolume
    let step = (now * n).rounded() + (direction > 0 ? 1 : direction < 0 ? -1 : 0)
    let target = max(0, min(1, step / n))
    if let s = slider() {
      s.setValue(target, animated: false)
      s.sendActions(for: .valueChanged)
    }
    log.debug("volume step \(direction) -> \(target)")
    return state(level: target)
  }
}
