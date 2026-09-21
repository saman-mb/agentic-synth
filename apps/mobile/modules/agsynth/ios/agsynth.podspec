require 'json'

package = JSON.parse(File.read(File.join(__dir__, '..', 'package.json')))

Pod::Spec.new do |s|
  s.name           = 'agsynth'
  s.version        = package['version']
  s.summary        = package['description']
  s.description    = package['description']
  s.license        = 'MIT'
  s.author         = 'Tambra'
  s.homepage       = 'https://github.com/saman-mb/agentic-synth'
  s.platforms      = { :ios => '15.1' }
  s.swift_version  = '5.9'
  s.source         = { :git => 'https://github.com/saman-mb/agentic-synth.git' }
  s.static_framework = true

  s.dependency 'ExpoModulesCore'
  s.dependency 'React-jsi'

  s.frameworks = 'AudioToolbox', 'AVFoundation'

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'USE_HEADERMAP' => 'YES',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++20',
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) AGENTIC_SYNTH_HAS_JSI=1 FOLLY_NO_CONFIG=1',
    'HEADER_SEARCH_PATHS' => [
      '"$(PODS_TARGET_SRCROOT)"',
      '"$(PODS_TARGET_SRCROOT)/../../../../../src"',
      '"$(PODS_TARGET_SRCROOT)/../../../../../src/capi"',
      '"$(PODS_ROOT)/Headers/Public/React-jsi"',
      '"$(PODS_ROOT)/Headers/Private/React-jsi"'
    ].join(' ')
  }

  s.source_files = [
    '*.{h,m,mm,swift}',
    '../../../../../src/capi/*.{h,cpp}',
    '../../../../../src/engine/**/*.{h,cpp}',
    '../../../../../src/jsi/host/*.{h,cpp}',
    '../../../../../src/jsi/jsi/*.{h,cpp}',
    '../../../../../src/jsi/audio/AudioStream.h',
    '../../../../../src/jsi/audio/AudioStreamRemoteIO.cpp'
  ]

  s.public_header_files = 'AgsynthBridge.h'
end
