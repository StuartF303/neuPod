# SPDX-License-Identifier: MPL-2.0
#
# Only used if the app ever falls back to CocoaPods; Flutter builds the plugin
# with Swift Package Manager (neupod_audio/Package.swift).
Pod::Spec.new do |s|
  s.name             = 'neupod_audio'
  s.version          = '0.1.0'
  s.summary          = "The neuPod app's audio engine."
  s.description      = 'AVAudioEngine playback with the system media controls, for the neuPod app.'
  s.homepage         = 'https://github.com/StuartF303/neuPod'
  s.license          = { :type => 'MPL-2.0', :file => '../LICENSE' }
  s.author           = 'The neuPod authors'
  s.source           = { :path => '.' }
  s.source_files     = 'neupod_audio/Sources/neupod_audio/**/*.swift'
  s.resource_bundles = { 'neupod_audio_privacy' => ['neupod_audio/Sources/neupod_audio/PrivacyInfo.xcprivacy'] }
  s.dependency 'Flutter'
  s.frameworks       = 'AVFoundation', 'MediaPlayer'
  s.platform         = :ios, '18.0'
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES' }
  s.swift_version    = '5.0'
end
