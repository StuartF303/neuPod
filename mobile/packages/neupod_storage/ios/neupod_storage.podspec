# SPDX-License-Identifier: MPL-2.0
#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint neupod_storage.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'neupod_storage'
  s.version          = '0.0.1'
  s.summary          = 'Read-only folder access for the neuPod app (decision 0004).'
  s.description      = <<-DESC
Read-only folder access for the neuPod app (decision 0004).
                       DESC
  s.homepage         = 'https://github.com/StuartF303/neuPod'
  s.license          = { :type => 'MPL-2.0', :file => '../LICENSE' }
  s.author           = { 'neuPod contributors' => 'https://github.com/StuartF303/neuPod' }
  s.source           = { :path => '.' }
  s.source_files = 'neupod_storage/Sources/neupod_storage/**/*'
  s.dependency 'Flutter'
  s.platform = :ios, '18.0'

  # Flutter.framework does not contain a i386 slice.
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' }
  s.swift_version = '5.9'

  # If your plugin requires a privacy manifest, for example if it uses any
  # required reason APIs, update the PrivacyInfo.xcprivacy file to describe your
  # plugin's privacy impact, and then uncomment this line. For more information,
  # see https://developer.apple.com/documentation/bundleresources/privacy_manifest_files
  s.resource_bundles = {'neupod_storage_privacy' => ['neupod_storage/Sources/neupod_storage/PrivacyInfo.xcprivacy']}
end
